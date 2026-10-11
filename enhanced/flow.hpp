#pragma once
#include "telemetry.hpp"

namespace enhanced {
// Image-space motion only. It is deliberately not a 3D pose or calibrated confidence.
struct flow_assistance
{
    flow_observation observation;
    double strength=0;
};
// Flow can only damp the one existing predictor; no second extrapolator or
// accumulated pose correction. A fresh absolute measurement cancels assistance.
inline double flow_damping(const flow_assistance& f, timestamp sample, timestamp now)
{
    const auto& v=f.observation;
    if (!v.enabled || !v.valid || v.anchor!=sample || v.captured<=sample ||
        stale(v.captured,now,30) || stale(sample,now,50) || !std::isfinite(v.speed_px_s) ||
        !std::isfinite(v.reliability) || !std::isfinite(f.strength)) return 1;
    // Stationary/inconsistent image evidence weakens extrapolation, never drives
    // axis direction. Moving image features cannot safely imply head rotation.
    const double stationary=1-std::clamp(v.speed_px_s/20.,0.,1.);
    return 1-.30*std::clamp(f.strength,0.,1.)*std::clamp(v.reliability,0.,1.)*stationary;
}
}
