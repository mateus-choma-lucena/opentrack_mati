#pragma once
#include "telemetry.hpp"
#include <limits>

namespace enhanced {
using pose = std::array<double, 6>; // cm, then degrees, matching the plugin API
inline double delta(double a, double b, std::size_t axis) { return axis < 3 ? a-b : std::remainder(a-b, 360.); }
inline bool finite(const pose& value) { return std::all_of(value.begin(), value.end(), [](double x) { return std::isfinite(x); }); }
inline double alpha(double cutoff, double dt) { return -std::expm1(-6.283185307179586*cutoff*dt); }

struct adaptive_parameters
{
    bool enabled = false, confidence_aware = false;
    double rotation_smoothing = 1., translation_smoothing = 1.;
    double responsiveness = 1., jitter = 1.;
};

class adaptive_filter
{
    pose output{}, previous{}, velocity{};
    timestamp last{};
    bool initialized = false, lost = false, recovery_phase = false;
    unsigned rejected = 0;
public:
    void reset() { *this = {}; }
    void tracking_lost() { lost = true; velocity = {}; }
    pose update(const pose& input, timestamp time, adaptive_parameters p, std::optional<double> confidence = {})
    {
        if (!finite(input) || (p.confidence_aware && confidence && (!std::isfinite(*confidence) || *confidence <= 0)))
        { lost = true; velocity = {}; return output; }
        if (!p.enabled) { output = previous = input; last = time; initialized = true; lost = recovery_phase = false; return output; }
        if (!initialized) { output = previous = input; last = time; initialized = true; return output; }
        if (time <= last) return output;
        const double interval = std::chrono::duration<double>(time-last).count();
        const bool recovering = lost || interval > .25;
        const double dt = std::clamp(interval, .001, .05);
        last = time;
        if (recovering) { previous = input; velocity = {}; lost = false; recovery_phase = true; return output; }
        bool outlier = false;
        for (std::size_t i=0; i<6; ++i)
        {
            const double change = std::abs(delta(input[i], previous[i], i));
            outlier |= change > (i<3 ? 50. : 120.) || (change > (i<3 ? 5. : 25.) && change/dt > (i<3 ? 500. : 1500.));
        }
        if (outlier && ++rejected < 3) return output;
        const bool forced_recovery = outlier;
        recovery_phase |= forced_recovery;
        rejected = 0;
        const double confidence_scale = p.confidence_aware && confidence ? std::clamp(*confidence, .05, 1.) : 1.;
        for (std::size_t i=0; i<6; ++i)
        {
            const double speed = delta(input[i], previous[i], i)/dt;
            velocity[i] += alpha(8., dt)*(speed-velocity[i]);
            const double strength = std::clamp(i<3 ? p.translation_smoothing : p.rotation_smoothing, 0., 5.);
            const double base = 1.5/(.25+strength);
            const double cutoff = std::clamp(base + std::clamp(p.responsiveness, 0., 5.)*std::abs(velocity[i])*.35, base, 60.);
            double difference = delta(input[i], output[i], i);
            const double band = std::clamp(p.jitter, 0., 5.)*(i<3 ? .015 : .08);
            if (std::abs(speed) < (i<3 ? .5 : 2.))
                difference = std::copysign(std::max(0., std::abs(difference)-band), difference);
            double step = alpha(cutoff*confidence_scale, dt)*difference;
            // Bound reacquisition of persistent extreme measurements without freezing forever.
            if (forced_recovery || recovery_phase) step = std::clamp(step, -(i<3 ? 100. : 360.)*dt, (i<3 ? 100. : 360.)*dt);
            output[i] += step;
            if (i>=3) output[i] = std::remainder(output[i], 360.);
        }
        bool close = true;
        for (std::size_t i=0; i<6; ++i) close &= std::abs(delta(input[i], output[i], i)) < (i<3 ? .1 : 1.);
        if (close) recovery_phase = false;
        previous = input;
        return output;
    }
};

struct prediction_parameters
{
    bool enabled = false;
    double horizon_ms = 8., max_rotation = 3., max_translation = .5;
};
struct predicted_pose { pose value{}; double horizon_ms = 0, rotation = 0, translation = 0; };

class predictor
{
    pose previous{}, velocity{}, noise{};
    timestamp last{};
    bool initialized = false, ready = false;
public:
    void reset() { *this = {}; }
    void update(const pose& input, timestamp time, bool valid)
    {
        if (!valid || !finite(input)) { reset(); return; }
        if (initialized && time <= last) return;
        const double dt = std::chrono::duration<double>(time-last).count();
        if (!initialized || dt > .15)
        { previous = input; velocity = noise = {}; last = time; initialized = true; ready = false; return; }
        ready = dt >= .001;
        if (ready)
            for (std::size_t i=0; i<6; ++i)
            {
                const double v = delta(input[i], previous[i], i)/dt;
                const bool reversal = v*velocity[i] < 0;
                const bool implausible = std::abs(v) > (i<3 ? 200. : 720.);
                noise[i] += alpha(5., dt)*(std::abs(v-velocity[i])-noise[i]);
                velocity[i] = reversal || implausible ? 0. : velocity[i]+alpha(15., dt)*(v-velocity[i]);
            }
        previous = input; last = time;
    }
    predicted_pose predict(timestamp now, prediction_parameters p, std::optional<double> confidence = {}, std::optional<pose> base = {}) const
    {
        predicted_pose result{base.value_or(previous)};
        if (!p.enabled || !ready || stale(last, now, 100) || (confidence && (!std::isfinite(*confidence) || *confidence <= 0))) return result;
        // Fixed horizon from the measurement, never accumulates across repeated output polls.
        const double horizon = std::clamp(p.horizon_ms, 0., 15.);
        const double conf = confidence ? std::clamp(*confidence, 0., 1.) : 1.;
        for (std::size_t i=0; i<6; ++i)
        {
            const double attenuation = std::abs(velocity[i])/(std::abs(velocity[i])+noise[i]+(i<3 ? .5 : 2.));
            const double limit = std::clamp(i<3 ? p.max_translation : p.max_rotation, 0., i<3 ? 2. : 10.);
            const double offset = std::clamp(velocity[i]*horizon*.001*attenuation*conf, -limit, limit);
            result.value[i] += offset;
            if (i>=3) result.value[i] = std::remainder(result.value[i], 360.);
            auto& maximum = i<3 ? result.translation : result.rotation;
            maximum = std::max(maximum, std::abs(offset));
        }
        result.horizon_ms = result.rotation > 0 || result.translation > 0 ? horizon : 0;
        return result;
    }
};
} // namespace enhanced
