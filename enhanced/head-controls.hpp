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

inline double adjusted_mapping_output(double mapped, double gain, double limit)
{ return std::clamp(mapped*gain, -std::abs(limit), std::abs(limit)); }

// Convergence in mapped protocol units, not wrapped physical head angles.
class output_response
{
    pose output{}, previous_reference{};
    pose baseline_cutoffs{12,12,12,12,12,12}, effective_cutoffs{12,12,12,12,12,12};
    timestamp last{}, last_sample{};
    bool initialized=false, lost=false;
public:
    void reset() { *this={}; }
    void tracking_lost() { lost=true; }
    void constrain(const pose& limits) { for (std::size_t i=0;i<6;++i) output[i]=std::clamp(output[i],-limits[i],limits[i]); }
    const pose& cutoffs() const { return effective_cutoffs; }
    void seed(const pose& value, timestamp now) { output=value; last=now; initialized=true; }
    pose update(const pose& target, const pose& reference, const pose& unamplified_target,
                timestamp now, timestamp sample, double multiplier)
    {
        if (!finite(target)||!finite(reference)||!finite(unamplified_target)) { tracking_lost(); return output; }
        if (!initialized) { seed(reference,now); previous_reference=reference; last_sample=sample; return output; }
        if (now<=last) return output;
        const double dt=std::chrono::duration<double>(now-last).count(); last=now;
        if (lost || dt>.25) { lost=false; previous_reference=reference; last_sample=sample; return output; }
        // Observe the selected filter's response in mapped coordinates. It is a parallel
        // reference, never an extra serial smoothing pass. Invalid/non-exponential ratios
        // retain the last usable cutoff; a passthrough uses the documented 12 Hz baseline.
        if (sample>last_sample)
        {
            const double frame_dt=std::chrono::duration<double>(sample-last_sample).count();
            if (last_sample!=timestamp{}&&frame_dt>0&&frame_dt<=.25)
                for (std::size_t i=0;i<6;++i)
                {
                    const double distance=unamplified_target[i]-previous_reference[i];
                    if (std::abs(distance)<1e-6) continue;
                    const double fraction=(reference[i]-previous_reference[i])/distance;
                    if (fraction>1e-5&&fraction<1.-1e-5)
                        baseline_cutoffs[i]=std::clamp(-std::log1p(-fraction)/(6.283185307179586*frame_dt),.4,60.);
                    else if (fraction>=1.-1e-5&&fraction<=1.+1e-5) baseline_cutoffs[i]=12.;
                }
            previous_reference=reference; last_sample=sample;
        }
        for (std::size_t i=0;i<6;++i)
        {
            effective_cutoffs[i]=std::clamp(baseline_cutoffs[i]*std::clamp(multiplier,.125,5.),.05,60.);
            output[i] += alpha(effective_cutoffs[i],dt)*(target[i]-output[i]);
        }
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
    bool initialized=false, response_seeded=false, recovering=false;
    pose last_processed{};
    output_response response;
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
    void reset_motion() { response.reset(); response_seeded=false; recovering=false; diagnostic.estimated_lag_ms.reset(); }
    void tracking_lost() { response.tracking_lost(); recovering=response_seeded; }
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
        diagnostic.per_axis=p.per_axis;
        diagnostic.amplification=std::clamp(p.amplification,0,100); diagnostic.speed=std::clamp(p.speed,0,100);
        diagnostic.transitioning=!(current==target);
        diagnostic.gain=current.gain; diagnostic.gains=current.gains; diagnostic.responsiveness=current.response;
        diagnostic.shared_response=diagnostic.fallback_response=false;
        diagnostic.estimated_lag_ms.reset();
    }
    bool response_active() const { return current.blend>0 || target.blend>0; }
    pose process(const pose& mapped_reference, const pose& mapped_target, timestamp now, timestamp captured,
                 const pose& limits, bool guarded=false)
    {
        pose reference=mapped_reference, destination=mapped_target;
        const bool transform=diagnostic.enabled||diagnostic.transitioning;
        for (std::size_t i=0;i<6;++i) if (transform)
        {
            reference[i]=adjusted_mapping_output(reference[i],current.gains[i],limits[i]);
            destination[i]=adjusted_mapping_output(destination[i],current.gains[i],limits[i]);
        }
        diagnostic.limits=limits; diagnostic.mapped_target=destination;
        if (!response_active()) { reset_motion(); diagnostic.response_cutoffs={}; last_processed=reference; return reference; }
        if (recovering) { recovering=false; return last_processed; }
        if (!response_seeded) { response.seed(reference,now); response_seeded=true; }
        if (transform) response.constrain(limits);
        const auto filtered=response.update(destination,mapped_reference,mapped_target,now,captured,current.response);
        diagnostic.shared_response=guarded; diagnostic.fallback_response=current.blend>0;
        diagnostic.response_cutoffs=response.cutoffs();
        diagnostic.estimated_lag_ms=1000./(6.283185307179586 * *std::min_element(response.cutoffs().begin(),response.cutoffs().end()));
        pose result=reference;
        for (std::size_t i=0;i<6;++i) {
            result[i]=std::lerp(reference[i],filtered[i],current.blend);
            if (transform) result[i]=std::clamp(result[i],-limits[i],limits[i]);
        }
        last_processed=result;
        return result;
    }
    head_controls_snapshot snapshot(const pose& raw, const pose& processed, timestamp captured, timestamp submitted, bool valid)
    {
        diagnostic.raw=raw; diagnostic.processed=processed; diagnostic.captured=captured; diagnostic.submitted=submitted; diagnostic.valid=valid;
        return diagnostic;
    }
};
} // namespace enhanced
