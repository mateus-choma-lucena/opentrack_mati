#pragma once
#include "motion.hpp"
#include <utility>

namespace enhanced {
struct neuralnet_parameters
{
    bool enabled = false, validation = true, stabilization = true, recovery = true, reliability_protection = true;
    double spike_strength = 1, responsiveness = 1, hold_ms = 50, suspect_ms = 100;
    double confirmation_ms = 60, blend_ms = 120, angular_velocity = 1080, translation_velocity = 250;
    double max_age_ms = 150;
};
struct neuralnet_result
{
    pose value{};
    timestamp captured{};
    bool valid = false, accepted = false, holding = false, prediction_allowed = false;
    neuralnet_snapshot diagnostics;
};

// R = Ry(-yaw) Rz(pitch) Rx(-roll), the NeuralNet plugin's existing convention.
// Recovery uses shortest-arc quaternion interpolation, never independent Euler averaging.
namespace neuralnet_rotation {
using quaternion = std::array<double,4>;
inline quaternion multiply(const quaternion& a, const quaternion& b)
{
    return {a[0]*b[0]-a[1]*b[1]-a[2]*b[2]-a[3]*b[3],
        a[0]*b[1]+a[1]*b[0]+a[2]*b[3]-a[3]*b[2],
        a[0]*b[2]-a[1]*b[3]+a[2]*b[0]+a[3]*b[1],
        a[0]*b[3]+a[1]*b[2]-a[2]*b[1]+a[3]*b[0]};
}
inline quaternion from_pose(const pose& p)
{
    constexpr double half_rad = 3.14159265358979323846/360;
    const double y=-p[3]*half_rad, z=p[4]*half_rad, x=-p[5]*half_rad;
    return multiply(multiply({std::cos(y),0,std::sin(y),0},{std::cos(z),0,0,std::sin(z)}),
        {std::cos(x),std::sin(x),0,0});
}
inline double dot(const quaternion& a, const quaternion& b)
{ double d=0; for (std::size_t i=0;i<4;++i) d+=a[i]*b[i]; return d; }
inline double distance(const pose& a, const pose& b)
{ return 360/3.14159265358979323846*std::acos(std::clamp(std::abs(dot(from_pose(a),from_pose(b))),0.,1.)); }
inline void blend(pose& output, const pose& target, double fraction)
{
    auto a=from_pose(output), b=from_pose(target);
    double d=dot(a,b);
    if (d<0) { for (auto& v:b) v=-v; d=-d; }
    d=std::clamp(d,0.,1.);
    const double angle=std::acos(d), sine=std::sin(angle);
    const double u=sine>1e-6 ? std::sin((1-fraction)*angle)/sine : 1-fraction;
    const double v=sine>1e-6 ? std::sin(fraction*angle)/sine : fraction;
    quaternion q{}; double norm=0;
    for (std::size_t i=0;i<4;++i) { q[i]=u*a[i]+v*b[i]; norm+=q[i]*q[i]; }
    for (auto& e:q) e/=std::sqrt(norm);
    const auto [w,x,y,z]=q;
    constexpr double deg=180/3.14159265358979323846;
    output[3]=deg*std::atan2(2*(x*z-w*y),1-2*(y*y+z*z));
    output[4]=deg*std::asin(std::clamp(2*(x*y+w*z),-1.,1.));
    output[5]=deg*std::atan2(2*(y*z-w*x),1-2*(x*x+z*z));
}
}

// Single inference-thread owner, protected by the tracker's pose mutex for polls/reset.
// Fixed history storage and no look-ahead queue. Counters last for a tracking session.
class neuralnet_pipeline
{
    struct entry { pose value{}, velocity{}; timestamp time{}; };
    std::array<entry,5> history{};
    std::size_t count=0, cursor=0;
    entry reference{}, candidate{};
    timestamp seen{}, loss_start{}, recovery_start{}, blend_start{}, last_good_now{}, quality_time{};
    bool have_reference=false, have_candidate=false, have_output=false, blending=false, enabled=false;
    neuralnet_result result;
    neuralnet_snapshot stats;
    neuralnet_parameters config;

    double median(std::size_t axis) const
    {
        std::array<double,5> values{};
        for (std::size_t i=0;i<count;++i) values[i]=history[i].velocity[axis];
        std::sort(values.begin(),values.begin()+count);
        return count ? values[count/2] : 0;
    }
    double motion_velocity(const pose& input, timestamp captured, std::size_t axis, double instantaneous) const
    {
        if (count<3) return instantaneous;
        timestamp oldest=captured;
        for (std::size_t i=0;i<count;++i)
        {
            oldest=std::min(oldest,history[i].time);
            // Large intentional changes and abrupt stops use immediate velocity.
            if (std::abs(delta(input[axis],history[i].value[axis],axis))>(axis<3 ? .05 : .5)) return instantaneous;
        }
        if (milliseconds(oldest,captured)<35) return instantaneous;
        // Theil-Sen slope on at most six observations distinguishes alternating
        // stationary noise from a consistent slow drift without buffering output.
        std::array<double,15> slopes{}; std::size_t n=0;
        for (std::size_t i=0;i<count;++i)
            for (std::size_t j=i+1;j<=count;++j)
            {
                const auto time=j==count ? captured : history[j].time;
                const double value=j==count ? input[axis] : history[j].value[axis];
                const double dt=milliseconds(history[i].time,time)*.001;
                if (dt!=0) slopes[n++]=delta(value,history[i].value[axis],axis)/dt;
            }
        std::sort(slopes.begin(),slopes.begin()+n);
        return n ? slopes[n/2] : instantaneous;
    }
    static bool plausible(const pose& p)
    {
        if (!finite(p)) return false;
        for (std::size_t i=0;i<6;++i) if (std::abs(p[i])>(i<3 ? 1000. : 3600.)) return false;
        return true;
    }
    bool discontinuity(const pose& input, const entry& base, timestamp time, bool robust) const
    {
        const double dt=milliseconds(base.time,time)*.001;
        if (dt<=0) return true;
        for (std::size_t i=0;i<6;++i)
        {
            const double change=std::abs(delta(input[i],base.value[i],i));
            const double slack=(i<3 ? .5 : 3.)/config.spike_strength;
            const double limit=i<3 ? config.translation_velocity : config.angular_velocity;
            if (change>slack+limit*dt) return true;
            // Stationary-history residuals can identify a moderate isolated jump.
            // Enable only with positive observable evidence; low reliability never locks out recovery.
            if (robust && config.reliability_protection && count>=3 && milliseconds(history[(cursor+history.size()-count)%history.size()].time,base.time)>=35 && stats.reliability>.5 &&
                std::abs(median(i))<(i<3 ? .5 : 3.) &&
                change>(i<3 ? 3. : 18.)/config.spike_strength+limit*dt*.2) return true;
        }
        return false;
    }
    void classify(const pose& input, timestamp captured)
    {
        pose velocity{};
        double rotation=0, translation=0; bool reversal=false;
        const double dt=have_reference ? milliseconds(reference.time,captured)*.001 : 0;
        if (dt>0 && dt<.25)
            for (std::size_t i=0;i<6;++i)
            {
                velocity[i]=delta(input[i],reference.value[i],i)/dt;
                const double movement=motion_velocity(input,captured,i,velocity[i]);
                auto& speed=i<3 ? translation : rotation;
                speed=std::max(speed,std::abs(movement));
                reversal |= movement*reference.velocity[i]<0 && std::abs(movement)>(i<3 ? 2. : 15.) &&
                    std::abs(reference.velocity[i])>(i<3 ? 2. : 15.);
            }
        const bool stationary=rotation<3 && translation<.6;
        const bool stopping=stationary && (stats.motion==motion_state::rapid || stats.motion==motion_state::slow);
        stats.motion=reversal ? motion_state::reversing : stopping ? motion_state::stopping :
            stationary ? motion_state::stationary : (rotation>90 || translation>15 ? motion_state::rapid : motion_state::slow);
        reference={input,velocity,captured}; have_reference=true;
        history[cursor]=reference; cursor=(cursor+1)%history.size(); count=std::min(count+1,history.size());
    }
    void missing(timestamp now, bool connected)
    {
        if (loss_start==timestamp{}) loss_start=now;
        if (!connected || !have_output || !config.recovery || milliseconds(loss_start,now)>=config.suspect_ms ||
            stats.state==tracking_state::lost || stats.state==tracking_state::reacquiring)
            stats.state=tracking_state::lost;
        else stats.state=tracking_state::suspect;
        blending=false; recovery_start={};
        stats.raw_valid=false;
        stats.motion=motion_state::unknown;
        if (quality_time!=timestamp{} && now>quality_time) stats.reliability*=std::exp(-milliseconds(quality_time,now)/80.);
        quality_time=now;
        const bool hold=connected && config.recovery && have_output && stats.state==tracking_state::suspect &&
            milliseconds(loss_start,now)<config.hold_ms && milliseconds(last_good_now,now)<config.hold_ms;
        result.valid=result.holding=hold; result.accepted=result.prediction_allowed=false;
        stats.holding=hold; stats.output_valid=hold;
        stats.loss_duration_ms=milliseconds(loss_start,now);
    }
    void refresh(timestamp now)
    {
        if (loss_start!=timestamp{} && !stats.raw_valid && quality_time!=timestamp{} && now>quality_time)
        { stats.reliability*=std::exp(-milliseconds(quality_time,now)/80.); quality_time=now; }
        stats.recovery_active=stats.state==tracking_state::reacquiring;
        stats.output_valid=result.valid; stats.holding=result.holding;
        stats.stabilization_requested=config.enabled && config.stabilization;
        if (loss_start!=timestamp{}) stats.loss_duration_ms=std::max(0.,milliseconds(loss_start,now));
        result.diagnostics=stats;
    }
public:
    std::optional<std::pair<pose,timestamp>> validated_measurement(timestamp now) const
    {
        if (!enabled || !have_reference || !stats.raw_valid || stale(reference.time,now,config.max_age_ms)) return {};
        return std::pair{reference.value,reference.time};
    }
    void validation_time(double ms) { stats.validation_ms=ms; result.diagnostics.validation_ms=ms; }
    void publication_time(timestamp now)
    {
        if (stats.raw_time!=timestamp{} && now>=stats.raw_time) stats.capture_to_pose_ms=milliseconds(stats.raw_time,now);
        result.diagnostics.capture_to_pose_ms=stats.capture_to_pose_ms;
    }
    void reset()
    {
        const auto counters=stats;
        *this={}; stats.rejected=counters.rejected; stats.spikes=counters.spikes; stats.reacquisitions=counters.reacquisitions;
    }
    neuralnet_result poll(timestamp now, bool connected=true)
    {
        if (enabled && stats.state==tracking_state::reacquiring && stale(stats.validated_time,now,config.max_age_ms)) missing(now,connected);
        if (enabled && result.valid && !result.holding && stale(result.captured,now,config.max_age_ms) && loss_start==timestamp{})
            loss_start=std::min(now,last_good_now+std::chrono::duration_cast<clock::duration>(std::chrono::duration<double,std::milli>(config.max_age_ms)));
        if (enabled && (!connected || (result.holding &&
            (milliseconds(loss_start,now)>=config.hold_ms || milliseconds(last_good_now,now)>=config.hold_ms)) ||
            (!result.holding && result.valid && stale(result.captured,now,config.max_age_ms)))) missing(now,connected);
        if (enabled && loss_start!=timestamp{} && stats.state==tracking_state::suspect &&
            milliseconds(loss_start,now)>=config.suspect_ms) missing(now,connected);
        refresh(now); return result;
    }
    template<class Transform>
    neuralnet_result update(std::optional<pose> input, timestamp captured, timestamp now,
                           neuralnet_parameters p, bool connected, Transform transform)
    {
        if (enabled!=p.enabled) reset();
        enabled=p.enabled; config=p;
        auto safe=[](double v,double fallback,double lo,double hi) { return std::isfinite(v) ? std::clamp(v,lo,hi) : fallback; };
        config.spike_strength=safe(p.spike_strength,1,.25,3);
        config.responsiveness=safe(p.responsiveness,1,.25,3);
        config.angular_velocity=safe(p.angular_velocity,1080,90,3600);
        config.translation_velocity=safe(p.translation_velocity,250,10,1000);
        config.hold_ms=safe(p.hold_ms,50,0,250); config.suspect_ms=safe(p.suspect_ms,100,20,500);
        config.confirmation_ms=safe(p.confirmation_ms,60,0,250); config.blend_ms=safe(p.blend_ms,120,20,500);
        config.max_age_ms=safe(p.max_age_ms,150,5,1000);
        if (!config.recovery && stats.state==tracking_state::reacquiring)
        { stats.state=tracking_state::lost; blending=false; recovery_start={}; }
        stats.enabled=enabled; stats.raw_time=input ? captured : timestamp{};
        stats.capture_to_pose_ms=input && captured!=timestamp{} && now>=captured ? std::optional<double>(milliseconds(captured,now)) : std::nullopt;
        result.accepted=result.holding=result.prediction_allowed=false;
        if (!enabled)
        {
            result.valid=connected && input && finite(*input); result.value=input.value_or(pose{}); result.captured=captured;
            result.accepted=result.prediction_allowed=result.valid; stats.raw_valid=result.valid;
            stats.state=result.valid ? tracking_state::tracking : tracking_state::lost;
            refresh(now); return result;
        }
        const bool malformed=input && !plausible(*input);
        const bool bad_time=captured==timestamp{} || captured<=seen || stale(captured,now,config.max_age_ms);
        if (captured>seen) seen=captured;
        if (!connected || !input || malformed || bad_time)
        {
            if (input && (malformed || bad_time)) ++stats.rejected;
            have_candidate=false; missing(now,connected); refresh(now); return result;
        }
        // Advance wall-time transitions even if no output consumer polled while face was absent.
        if (stats.state==tracking_state::reacquiring && stale(stats.validated_time,now,config.max_age_ms)) missing(now,true);
        if (have_output && stats.state!=tracking_state::reacquiring && stale(result.captured,now,config.max_age_ms))
        {
            if (loss_start==timestamp{}) loss_start=std::min(now,last_good_now+
                std::chrono::duration_cast<clock::duration>(std::chrono::duration<double,std::milli>(config.max_age_ms)));
            missing(now,true);
        }
        if (loss_start!=timestamp{} && stats.state==tracking_state::suspect && milliseconds(loss_start,now)>=config.suspect_ms)
            stats.state=tracking_state::lost;
        bool spike=config.validation && have_reference && stats.state!=tracking_state::lost &&
            stats.state!=tracking_state::reacquiring && discontinuity(*input,reference,captured,true);
        if (spike && have_candidate && milliseconds(candidate.time,captured)<=80 &&
            !discontinuity(*input,candidate,captured,false))
        {
            // A second continuous observation confirms a changed position. The first
            // suspicious sample remains rejected; no buffering or permanent clamping.
            spike=false; have_reference=false; count=cursor=0;
        }
        if (spike)
        {
            candidate={*input,{},captured}; have_candidate=true; ++stats.rejected; ++stats.spikes;
            missing(now,true); refresh(now); return result;
        }
        have_candidate=false;
        if (stats.state==tracking_state::lost && have_output && config.recovery)
        {
            stats.state=tracking_state::reacquiring; recovery_start=now; ++stats.reacquisitions;
            have_reference=false; count=cursor=0;
        }
        if (stats.state==tracking_state::reacquiring && have_reference && config.validation &&
            discontinuity(*input,reference,captured,false))
        {
            ++stats.rejected; ++stats.spikes; missing(now,true); refresh(now); return result;
        }
        const double dt=have_reference ? std::max(0.,milliseconds(reference.time,captured)*.001) : .013333;
        classify(*input,captured);
        stats.raw_valid=true; stats.validated_time=captured; result.accepted=true;
        const double freshness=1-std::clamp(milliseconds(captured,now)/config.max_age_ms,0.,1.)*.5;
        stats.reliability+=(freshness-stats.reliability)*alpha(3.,std::min(dt,.1));
        quality_time=now;
        if (stats.state==tracking_state::reacquiring && milliseconds(recovery_start,now)<config.confirmation_ms/config.responsiveness)
        {
            result.valid=false; refresh(now); return result;
        }
        pose target=transform(*input);
        if (!plausible(target)) { ++stats.rejected; result.accepted=false; missing(now,true); refresh(now); return result; }
        if (stats.state==tracking_state::reacquiring && have_output)
        {
            if (!blending) { blending=true; blend_start=now; }
            const double step_time=std::clamp(dt,0.,.05);
            const double remaining=std::max(20.,config.blend_ms/config.responsiveness-milliseconds(blend_start,now));
            const double fraction=std::clamp(step_time*1000/remaining,0.,1.);
            const double distance=neuralnet_rotation::distance(result.value,target);
            const double rotation_fraction=distance>1e-6 ? std::min(fraction,720*config.responsiveness*step_time/distance) : 1.;
            neuralnet_rotation::blend(result.value,target,rotation_fraction);
            double translation_distance=0;
            for (std::size_t i=0;i<3;++i) translation_distance+=std::pow(target[i]-result.value[i],2);
            translation_distance=std::sqrt(translation_distance);
            const double translation_fraction=translation_distance>1e-6 ? std::min(fraction,100*config.responsiveness*step_time/translation_distance) : 1.;
            for (std::size_t i=0;i<3;++i) result.value[i]+=translation_fraction*(target[i]-result.value[i]);
            if (neuralnet_rotation::distance(result.value,target)<.25 && translation_distance<.05)
            { result.value=target; stats.state=tracking_state::tracking; blending=false; }
        }
        else { result.value=target; stats.state=tracking_state::tracking; }
        result.valid=true; result.captured=captured; have_output=true; last_good_now=now;
        if (stats.state==tracking_state::tracking) { loss_start={}; stats.loss_duration_ms=0; }
        result.prediction_allowed=stats.state==tracking_state::tracking;
        refresh(now); return result;
    }
    neuralnet_result update(std::optional<pose> input, timestamp captured, timestamp now, neuralnet_parameters p, bool connected=true)
    { return update(input,captured,now,p,connected,[](const pose& value) { return value; }); }
};
} // namespace enhanced
