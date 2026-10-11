#pragma once

#include "telemetry.hpp"
#include <atomic>
#include <string>
#include <vector>

namespace enhanced::calibration {
using axes = std::array<double, 6>; // X/Y/Z cm, yaw/pitch/roll degrees
struct observation {
    axes physical{};
    timestamp captured{}, received{};
    std::uint64_t center_epoch = 0;
    bool valid = false;
    std::optional<double> confidence, reliability;
};

// One pipeline producer, one UI consumer. No allocation, lock, or statistics in push().
class stream {
    std::array<observation, 1024> ring{};
    std::atomic<unsigned> read{0}, write{0};
    std::atomic<bool> enabled{false}, overflow{false};
public:
    void enable(bool value) { enabled.store(value, std::memory_order_release); }
    bool active() const { return enabled.load(std::memory_order_acquire); }
    void push(const observation& value) {
        if (!active()) return;
        const auto w = write.load(std::memory_order_relaxed);
        const auto next = unsigned((w+1)%ring.size());
        if (next == read.load(std::memory_order_acquire)) { overflow.store(true); return; }
        ring[w] = value;
        write.store(unsigned(next), std::memory_order_release);
    }
    std::vector<observation> drain() {
        std::vector<observation> result;
        if (overflow.exchange(false)) result.push_back({}); // Force a retry after a missed segment.
        auto r = read.load(std::memory_order_relaxed);
        const auto w = write.load(std::memory_order_acquire);
        while (r != w) { result.push_back(ring[r]); r = unsigned((r+1)%ring.size()); }
        read.store(r, std::memory_order_release);
        return result;
    }
};

inline double percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const double index = std::clamp(p, 0., 1.)*(values.size()-1);
    const auto lo = std::size_t(index), hi = std::min(lo+1, values.size()-1);
    return values[lo] + (values[hi]-values[lo])*(index-lo);
}
inline double difference(double value, double reference, int axis) {
    return axis < 3 ? value-reference : std::remainder(value-reference, 360.);
}
struct jitter_stats {
    axes median{}, mad{}, p95{}, drift{}, deadzone{};
    double interval_p05_ms=0, interval_p50_ms=0, interval_p95_ms=0, valid_hz=0;
    double reliability=0, valid_seconds=0;
    unsigned valid_samples=0, rejected_samples=0;
    bool ambiguous_motion=false;
};
struct range {
    double magnitude=0, reliability=0, seconds=0;
    unsigned samples=0;
    bool comfort_confirmed=false;
};
struct point { double input=0, output=0; };
enum class response_style { legacy=0, natural, balanced, responsive };
inline const char* style_name(response_style style) {
    switch(style) { case response_style::natural:return "Natural"; case response_style::balanced:return "Balanced";
        case response_style::responsive:return "Responsive"; default:return "Legacy power"; }
}
struct racing_template { const char* name; double yaw, pitch; response_style style; const char* reason; };
inline constexpr std::array<racing_template,5> racing_templates{{
    {"Le Mans Ultimate GT3",65,25,response_style::balanced,"Moderate cockpit targets and a softer center are starting choices for deliberate glances."},
    {"Le Mans Ultimate Hypercar",55,20,response_style::natural,"Smaller suggested cockpit targets keep the initial mapping conservative."},
    {"Assetto Corsa GT / Road",70,30,response_style::balanced,"Moderate yaw and pitch targets with a softer center provide an editable starting point."},
    {"Assetto Corsa Drift",100,35,response_style::responsive,"A larger requested yaw target and earlier response may suit frequent side glances."},
    {"Generic Racing",60,25,response_style::natural,"Conservative targets and a near-linear response are a starting point when the game behavior is unknown."}
}};
struct axis_result {
    range positive, negative;
    double deadzone=0, shape=2, target_positive=0, target_negative=0;
    std::vector<point> positive_points, negative_points;
    bool calibrated=false;
    response_style style=response_style::legacy;
    double sensitivity_positive=1, sensitivity_negative=1, output_gain=1;
    bool manual_points=false;
};
struct record {
    static constexpr int schema=2;
    std::array<axis_result, 6> axis{};
    jitter_stats jitter;
    std::string date, use_case="General head tracking";
};

enum class state { idle, collecting, retry, ready };
class measurement {
    std::vector<observation> samples;
    timestamp last{};
    std::uint64_t epoch=0;
    bool have_epoch=false;
    unsigned rejected=0;
    double quality_sum=0;
    int axis=-1;
    axes neutral{};
    state status_=state::idle;
    std::string error_;
    void fail(const char* text) { status_=state::retry; error_=text; samples.clear(); }
public:
    static constexpr double minimum_seconds=2.;
    static constexpr unsigned minimum_samples=60;
    void begin(int selected_axis, axes baseline={}) {
        *this={}; axis=selected_axis; neutral=baseline; status_=state::collecting;
        samples.reserve(2048);
    }
    state status() const { return status_; }
    const std::string& error() const { return error_; }
    unsigned count() const { return unsigned(samples.size()); }
    double duration() const { return samples.size()<2 ? 0 : milliseconds(samples.front().captured, samples.back().captured)*.001; }
    void add(const observation& sample) {
        if (status_!=state::collecting && status_!=state::ready) return;
        if (have_epoch && sample.center_epoch!=epoch) { fail("Recenter detected. Restart calibration from neutral."); return; }
        if (!sample.valid || stale(sample.captured,sample.received,250.)) { fail("Tracking lost or stale. Return to the requested position and retry."); return; }
        for (int i=0;i<6;++i) if (!std::isfinite(sample.physical[i]) || std::abs(sample.physical[i])>(i<3?1000.:180.)) {
            fail("Invalid physical pose. Retry after tracking recovers."); return;
        }
        if (sample.captured==last) return; // Repeated output polls are not measurements.
        if (sample.captured<last) { fail("Timestamp moved backwards. Retry."); return; }
        if (last!=timestamp{} && milliseconds(last,sample.captured)>250.) { fail("Capture gap. Retry this measurement."); return; }
        have_epoch=true; epoch=sample.center_epoch; last=sample.captured;
        const auto acceptable=[](std::optional<double> q) { return !q || (std::isfinite(*q) && *q>=.65 && *q<=1.); };
        if (!acceptable(sample.confidence) || !acceptable(sample.reliability)) {
            ++rejected;
            if (!samples.empty() && milliseconds(samples.back().captured,sample.captured)>250.) fail("Reliability too low. Retry after tracking settles.");
            return;
        }
        if (!samples.empty()) {
            const double dt=milliseconds(samples.back().captured,sample.captured)*.001;
            for (int i=0;i<6;++i) {
                const double step=std::abs(difference(sample.physical[i],samples.back().physical[i],i));
                if (step>(i<3?3.:12.) && step/dt>(i<3?100.:300.)) {
                    fail("Quick or accidental movement detected. Move gently and retry."); return;
                }
            }
        }
        // Bounded UI-side storage. A long run must be explicitly restarted.
        if (samples.size()==4096) { fail("Measurement timed out. Retry."); return; }
        samples.push_back(sample);
        quality_sum += std::min(sample.confidence.value_or(1.),sample.reliability.value_or(1.));
        if (count()>=minimum_samples && duration()>=minimum_seconds) {
            if (quality_sum/(count()+rejected)<.65) { fail("Too many unreliable observations. Retry after tracking settles."); return; }
            status_=state::ready;
        }
    }
    jitter_stats stationary() const {
        jitter_stats result;
        result.valid_samples=count(); result.rejected_samples=rejected; result.valid_seconds=duration();
        if (status_!=state::ready) return result;
        result.reliability=quality_sum/count()*double(count())/(count()+rejected);
        result.valid_hz=(count()-1)/duration();
        std::vector<double> intervals;
        for (std::size_t k=1;k<samples.size();++k) intervals.push_back(milliseconds(samples[k-1].captured,samples[k].captured));
        result.interval_p05_ms=percentile(intervals,.05); result.interval_p50_ms=percentile(intervals,.5); result.interval_p95_ms=percentile(intervals,.95);
        for (int i=0;i<6;++i) {
            std::vector<double> positions, deviations, first, last_positions;
            for (std::size_t k=0;k<samples.size();++k) {
                const double value=difference(samples[k].physical[i],samples.front().physical[i],i);
                positions.push_back(value);
                if (k<samples.size()/3) first.push_back(value);
                if (k>=samples.size()*2/3) last_positions.push_back(value);
            }
            const double median=percentile(positions,.5);
            result.median[i]=samples.front().physical[i]+median;
            if (i>=3) result.median[i]=std::remainder(result.median[i],360.);
            for (double p:positions) deviations.push_back(std::abs(p-median));
            result.mad[i]=percentile(deviations,.5); result.p95[i]=percentile(deviations,.95);
            result.drift[i]=percentile(last_positions,.5)-percentile(first,.5);
            // Drift or a large envelope could be deliberate motion; never inflate the noise estimate to hide it.
            result.ambiguous_motion |= std::abs(result.drift[i])>std::max(i<3?.15:.75,3*result.mad[i]) || result.p95[i]>(i<3?.5:2.5);
            result.deadzone[i]=std::min(i<3?.5:2.,std::max(3*result.mad[i],result.p95[i])*1.2);
        }
        return result;
    }
    std::optional<std::pair<int,range>> movement(bool comfort) const {
        if (status_!=state::ready || axis<0 || axis>=6 || !comfort) return {};
        std::vector<double> values;
        for (const auto& sample:samples) values.push_back(difference(sample.physical[axis],neutral[axis],axis));
        const double median=percentile(values,.5), sign=median<0?-1.:1.;
        for (auto& v:values) v*=sign;
        const double magnitude=percentile(values,.5); // Held endpoint's median, never a brief maximum.
        const double spread=percentile(values,.95)-percentile(values,.05);
        if (magnitude<(axis<3?.3:3.) || spread>std::max(axis<3?.3:2.,magnitude*.2) || percentile(values,.05)<magnitude*.7) return {};
        range measured{magnitude,quality_sum/count()*double(count())/(count()+rejected),duration(),count(),true};
        return std::pair{sign<0?-1:1,measured};
    }
};

inline bool generate(axis_result& result) {
    const double limit=std::min(result.positive.magnitude,result.negative.magnitude);
    const auto measured=[](const range& r) {
        return r.comfort_confirmed && std::isfinite(r.magnitude) && r.magnitude>0 && r.magnitude<=600 &&
            std::isfinite(r.reliability) && r.reliability>=.65 && r.reliability<=1 &&
            std::isfinite(r.seconds) && r.seconds>=measurement::minimum_seconds && r.samples>=measurement::minimum_samples;
    };
    if (!measured(result.positive) || !measured(result.negative) || !std::isfinite(limit) || limit<=0 ||
        !std::isfinite(result.deadzone) || result.deadzone<0 || result.deadzone>=limit*.4 ||
        !std::isfinite(result.shape) || result.shape<1.2 || result.shape>3 ||
        !std::isfinite(result.target_positive) || !std::isfinite(result.target_negative) ||
        result.target_positive<=0 || result.target_positive>600 || result.target_negative<=0 || result.target_negative>600 ||
        !std::isfinite(result.output_gain) || result.output_gain<.25 || result.output_gain>4 ||
        !std::isfinite(result.sensitivity_positive) || result.sensitivity_positive<.5 || result.sensitivity_positive>2 ||
        !std::isfinite(result.sensitivity_negative) || result.sensitivity_negative<.5 || result.sensitivity_negative>2 ||
        int(result.style)<0 || int(result.style)>3) return false;
    const auto branch=[&](double range_value,double target,double sensitivity) {
        std::vector<point> points{{0,0}};
        if (result.deadzone>0) points.push_back({result.deadzone,0});
        // A double-width first interval avoids Catmull-Rom's tiny initial
        // reversal for cubic shaping at an endpoint with a repeated zero tangent.
        for (int k=2;k<=32;++k) {
            const double u=double(k)/32;
            const double exponent=(result.style==response_style::natural || result.style==response_style::responsive)?1.+result.shape*.1:result.shape;
            const double progress=std::pow(u,exponent);
            const double early=sensitivity*(result.style==response_style::responsive?2.5:1.);
            const double shaped=early*progress/(1+(early-1)*progress);
            points.push_back({result.deadzone+(range_value-result.deadzone)*u,target/result.output_gain*shaped});
        }
        return points;
    };
    result.positive_points=branch(result.positive.magnitude,result.target_positive,result.sensitivity_positive);
    result.negative_points=branch(result.negative.magnitude,result.target_negative,result.sensitivity_negative);
    result.calibrated=true;
    result.manual_points=false;
    return true;
}
} // namespace enhanced::calibration
