#pragma once
#include "motion.hpp"

namespace enhanced {
// Neutral points intentionally match the displayed defaults, not a proprietary formula.
inline double amplification_gain(int percent)
{
    const double p = std::clamp(percent, 0, 100);
    return std::exp2(p <= 42 ? (p-42)*2./42. : (p-42)*2./58.);
}
inline double speed_response(int percent)
{
    const double p = std::clamp(percent, 0, 100);
    return p <= 55 ? std::exp2((p-55)*3./55.) : std::pow(5., (p-55)/45.);
}
struct head_control_parameters
{
    bool enabled = false, per_axis = false;
    int amplification = 42, speed = 55;
    std::array<int,6> axes{42,42,42,42,42,42};
};

// One state per axis; no frame queue. A convex exponential step cannot overshoot its target.
class response_filter
{
    pose output{}, previous{}, velocity{};
    timestamp last{};
    bool initialized = false;
    std::optional<double> lag_ms;
public:
    void reset() { *this = {}; }
    void seed(const pose& value, timestamp time, std::optional<pose> measurement={})
    { output=value; previous=measurement.value_or(value); velocity={}; last=time; initialized=true; lag_ms.reset(); }
    std::optional<double> estimated_lag_ms() const { return lag_ms; }
    pose update(const pose& input, timestamp time, double response)
    {
        if (!finite(input)) return output;
        if (!initialized) { seed(input,time); return output; }
        if (time <= last) return output;
        const double dt = std::chrono::duration<double>(time-last).count();
        last=time;
        if (dt > .25) { previous=input; velocity={}; return output; }
        double lowest=60.;
        for (std::size_t i=0;i<6;++i)
        {
            const double v=delta(input[i],previous[i],i)/dt;
            velocity[i] += alpha(8.,dt)*(v-velocity[i]);
            const double cutoff=std::clamp((12.+.15*std::abs(velocity[i]))*std::clamp(response,.125,5.),1.5,60.);
            lowest=std::min(lowest,cutoff);
            output[i] += alpha(cutoff,dt)*delta(input[i],output[i],i);
            if (i>=3) output[i]=std::remainder(output[i],360.);
        }
        previous=input; lag_ms=1000./(6.283185307179586*lowest);
        return output;
    }
};

class head_movement_controls
{
    // Gain and response settings slew independently of pose smoothing/prediction.
    struct controls {
        pose gains{1,1,1,1,1,1};
        double gain=1., response=1., blend=0.;
        bool operator==(const controls&) const = default;
    } current, from, target;
    timestamp transition_start{};
    bool initialized=false, fallback_seeded=false;
    response_filter fallback;
    head_controls_snapshot diagnostic;

    void advance(timestamp now)
    {
        const double progress=std::clamp(milliseconds(transition_start,now)/120.,0.,1.);
        const double w=progress*progress*(3.-2.*progress);
        current.gain=std::lerp(from.gain,target.gain,w);
        current.response=std::lerp(from.response,target.response,w);
        current.blend=std::lerp(from.blend,target.blend,w);
        for (std::size_t i=0;i<6;++i) current.gains[i]=std::lerp(from.gains[i],target.gains[i],w);
    }
public:
    void reset_motion() { fallback.reset(); fallback_seeded=false; diagnostic.estimated_lag_ms.reset(); }
    void configure(timestamp now, head_control_parameters p)
    {
        if (initialized) advance(now);
        else { initialized=true; transition_start=now; }
        controls desired;
        if (p.enabled)
        {
            desired.gain=amplification_gain(p.amplification);
            desired.response=speed_response(p.speed);
            desired.blend=std::clamp(p.speed,0,100)==55 ? 0. : 1.;
            for (std::size_t i=0;i<6;++i) desired.gains[i]=p.per_axis ? amplification_gain(p.axes[i]) : desired.gain;
        }
        if (!(desired==target)) { from=current; target=desired; transition_start=now; }
        diagnostic.enabled=p.enabled;
        diagnostic.amplification=std::clamp(p.amplification,0,100); diagnostic.speed=std::clamp(p.speed,0,100);
        diagnostic.transitioning=!(current==target);
        diagnostic.gain=current.gain; diagnostic.gains=current.gains; diagnostic.responsiveness=current.response;
        diagnostic.shared_response=diagnostic.fallback_response=false;
        diagnostic.estimated_lag_ms.reset();
    }
    double response_multiplier() const { return current.response; }
    pose respond(const pose& input, const pose& selected, timestamp captured, bool shared, std::optional<double> shared_lag={})
    {
        if (shared)
        {
            diagnostic.shared_response=diagnostic.enabled || diagnostic.transitioning;
            diagnostic.estimated_lag_ms=shared_lag; reset_fallback(); return selected;
        }
        if (current.blend==0 && target.blend==0) { reset_fallback(); return selected; }
        if (!fallback_seeded) { fallback.seed(selected,captured,input); fallback_seeded=true; }
        const auto filtered=fallback.update(input,captured,current.response);
        diagnostic.fallback_response=current.blend>0;
        diagnostic.estimated_lag_ms=fallback.estimated_lag_ms();
        pose result=selected;
        for (std::size_t i=0;i<6;++i)
        {
            result[i] += current.blend*delta(filtered[i],selected[i],i);
            if (i>=3 && current.blend>0) result[i]=std::remainder(result[i],360.);
        }
        return result;
    }
    pose amplify(const pose& mapped) const
    {
        pose result=mapped;
        for (std::size_t i=0;i<6;++i) result[i]*=current.gains[i];
        return result;
    }
    head_controls_snapshot snapshot(const pose& raw, const pose& processed, timestamp captured, timestamp submitted, bool valid)
    {
        diagnostic.raw=raw; diagnostic.processed=processed; diagnostic.captured=captured; diagnostic.submitted=submitted; diagnostic.valid=valid;
        return diagnostic;
    }
private:
    void reset_fallback() { fallback.reset(); fallback_seeded=false; }
};
} // namespace enhanced
