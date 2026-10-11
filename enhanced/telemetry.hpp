#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>

namespace enhanced {
using clock = std::chrono::steady_clock;
using timestamp = clock::time_point;
inline double milliseconds(timestamp a, timestamp b) { return std::chrono::duration<double, std::milli>(b-a).count(); }
inline bool stale(timestamp captured, timestamp now, double limit_ms)
{
    return captured == timestamp{} || now < captured || milliseconds(captured, now) > limit_ms;
}

// Fixed storage; sorting happens only when the diagnostics consumer requests a snapshot.
template<std::size_t Capacity = 256> class window
{
    std::array<double, Capacity> values{};
    std::size_t count = 0, cursor = 0;
public:
    void add(double value)
    {
        if (!std::isfinite(value) || value < 0) return;
        values[cursor] = value;
        cursor = (cursor+1)%Capacity;
        count = std::min(count+1, Capacity);
    }
    std::optional<double> mean() const
    {
        if (!count) return {};
        double sum = 0;
        for (std::size_t i=0; i<count; ++i) sum += values[i];
        return sum/count;
    }
    std::optional<double> percentile(double fraction) const
    {
        if (!count) return {};
        auto sorted = values;
        std::sort(sorted.begin(), sorted.begin()+count);
        return sorted[std::size_t(std::ceil(std::clamp(fraction, 0., 1.)*(count-1)))];
    }
};

struct head_controls_snapshot
{
    bool enabled = false, per_axis = false, transitioning = false, shared_response = false, fallback_response = false, valid = false;
    int amplification = 42, speed = 55;
    double gain = 1., responsiveness = 1.;
    std::array<double, 6> gains{1,1,1,1,1,1}, raw{}, processed{};
    std::array<double,6> mapped_target{}, response_cutoffs{}, limits{};
    std::optional<double> estimated_lag_ms;
    timestamp captured{}, submitted{}; // monotonic host timestamps, not wall-clock time
};

enum class tracking_state { tracking, suspect, lost, reacquiring };
enum class motion_state { stationary, slow, rapid, stopping, reversing, unknown };
inline const char* name(tracking_state state)
{
    switch (state) { case tracking_state::tracking: return "TRACKING"; case tracking_state::suspect: return "SUSPECT";
    case tracking_state::lost: return "LOST"; case tracking_state::reacquiring: return "REACQUIRING"; }
    return "LOST";
}
inline const char* name(motion_state state)
{
    switch (state) { case motion_state::stationary: return "stationary"; case motion_state::slow: return "slow";
    case motion_state::rapid: return "rapid"; case motion_state::stopping: return "stopping";
    case motion_state::reversing: return "reversing"; case motion_state::unknown: return "unavailable"; } return "unavailable";
}
struct neuralnet_snapshot
{
    bool enabled = false, raw_valid = false, output_valid = false, holding = false, recovery_active = false;
    bool stabilization_requested = false, stabilization_active = false;
    tracking_state state = tracking_state::lost;
    motion_state motion = motion_state::unknown;
    double reliability = 0, loss_duration_ms = 0, validation_ms = 0;
    std::optional<double> capture_to_pose_ms;
    timestamp raw_time{}, validated_time{};
    std::uint64_t rejected = 0, spikes = 0, reacquisitions = 0;
};

struct flow_observation
{
    bool enabled=false, valid=false;
    timestamp captured{}, anchor{};
    double dx=0, dy=0, speed_px_s=0, reliability=0, drift_px=0, cpu_ms=0;
    unsigned points=0, rejected=0;
    std::uint64_t resets=0;
};
struct snapshot
{
    int width = 0, height = 0, requested_fps = 0;
    std::optional<double> capture_fps, inference_fps, inference_ms, frame_age_ms, processing_ms;
    std::optional<double> interval_ms, interval_p95_ms, p50_ms, p95_ms, p99_ms;
    std::optional<double> application_ms, application_p50_ms, application_p95_ms, application_p99_ms;
    std::uint64_t output_samples = 0;
    std::uint64_t captured = 0, processed = 0, overwritten = 0, stale_frames = 0, reconnects = 0;
    std::optional<std::uint64_t> backend_drops;
    bool valid = false, connected = false, internal_filter = false, adaptive_filter = false;
    double prediction_ms = 0, prediction_rotation = 0, prediction_translation = 0;
    head_controls_snapshot head;
    neuralnet_snapshot neuralnet;
    flow_observation flow;
};

class telemetry
{
    mutable std::mutex mutex;
    snapshot state;
    window<> capture_intervals, processing_intervals, latencies, application_latencies;
    timestamp previous_capture{}, previous_process{}, latest_capture{}, latest_process{};
    timestamp previous_output_capture{};
public:
    void capture(timestamp time, int width, int height, int requested_fps,
                 std::optional<std::uint64_t> drops = {})
    {
        std::lock_guard lock(mutex);
        if (previous_capture != timestamp{} && time > previous_capture)
            capture_intervals.add(milliseconds(previous_capture, time));
        previous_capture = latest_capture = time;
        state.width = width; state.height = height; state.requested_fps = requested_fps;
        state.connected = true; state.backend_drops = drops; ++state.captured;
    }
    void processed(timestamp captured, timestamp started, timestamp finished, double inference_ms,
                   bool valid, bool internal_filter)
    {
        std::lock_guard lock(mutex);
        if (previous_process != timestamp{} && finished > previous_process)
            processing_intervals.add(milliseconds(previous_process, finished));
        previous_process = latest_process = finished;
        state.inference_ms = inference_ms;
        state.frame_age_ms = milliseconds(captured, started);
        state.processing_ms = milliseconds(captured, finished);
        latencies.add(*state.processing_ms);
        state.valid = valid; state.internal_filter = internal_filter; ++state.processed;
    }
    void discarded(bool too_old) { std::lock_guard lock(mutex); if (too_old) ++state.stale_frames; else ++state.overwritten; }
    void output(timestamp captured, timestamp finished)
    {
        std::lock_guard lock(mutex);
        // Count first protocol submission per sample, excluding duplicate output polls.
        if (captured == timestamp{} || captured <= previous_output_capture || finished < captured) return;
        previous_output_capture = captured;
        state.application_ms = milliseconds(captured, finished);
        application_latencies.add(*state.application_ms); ++state.output_samples;
    }
    void disconnected()
    {
        std::lock_guard lock(mutex);
        state.connected = state.valid = false;
        state.flow.valid=false;
        state.prediction_ms = state.prediction_rotation = state.prediction_translation = 0;
        previous_capture = previous_process = {};
        capture_intervals = {}; processing_intervals = {};
    }
    void reconnected() { std::lock_guard lock(mutex); ++state.reconnects; state.connected = true; }
    void prediction(double horizon, double rotation, double translation, bool filter = false)
    {
        std::lock_guard lock(mutex);
        state.prediction_ms = horizon; state.prediction_rotation = rotation; state.prediction_translation = translation;
        state.adaptive_filter = filter;
    }
    void head_controls(const head_controls_snapshot& value)
    { std::lock_guard lock(mutex); state.head = value; }
    void neuralnet(const neuralnet_snapshot& value)
    { std::lock_guard lock(mutex); state.neuralnet = value; }
    void flow(const flow_observation& value) { std::lock_guard lock(mutex); state.flow=value; }
    flow_observation flow() const { std::lock_guard lock(mutex); return state.flow; }
    snapshot read(timestamp now = clock::now()) const
    {
        std::lock_guard lock(mutex);
        auto result = state;
        result.interval_ms = capture_intervals.mean();
        result.interval_p95_ms = capture_intervals.percentile(.95);
        if (result.connected && !stale(latest_capture, now, 1000))
            if (auto interval = result.interval_ms; interval && *interval > 0) result.capture_fps = 1000/ *interval;
        if (!stale(latest_process, now, 1000))
            if (auto interval = processing_intervals.mean(); interval && *interval > 0) result.inference_fps = 1000/ *interval;
        result.p50_ms = latencies.percentile(.50); result.p95_ms = latencies.percentile(.95); result.p99_ms = latencies.percentile(.99);
        result.application_p50_ms = application_latencies.percentile(.50);
        result.application_p95_ms = application_latencies.percentile(.95);
        result.application_p99_ms = application_latencies.percentile(.99);
        return result;
    }
};
} // namespace enhanced
