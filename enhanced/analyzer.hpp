#pragma once
#include "motion.hpp"
#include <atomic>
#include <memory>
#include <vector>

namespace enhanced {
struct analysis_sample
{
    timestamp captured{}, validated{}, published{}, output{};
    pose raw{}, accepted{}, filtered{}, predicted{}, flow_correction{};
    bool raw_valid=false, valid=false, holding=false, output_available=false;
    double reliability=0, interval_ms=0, inference_ms=0, processing_ms=0;
    flow_observation flow;
    std::uint64_t center_epoch=0;
};
struct analysis_snapshot
{
    std::vector<analysis_sample> samples;
    std::uint64_t dropped=0, overwritten=0;
    bool recording=false;
};
// Producers always try_lock. Slow graph rendering/export may drop telemetry;
// it never stalls tracking. Allocate/configure only from the UI thread.
class analyzer_buffer
{
    struct storage { std::array<analysis_sample,512> live; std::array<analysis_sample,4096> record; };
    mutable std::mutex mutex_;
    std::unique_ptr<storage> storage_;
    std::atomic<bool> enabled_{false};
    std::atomic<std::uint64_t> dropped_{0};
    std::size_t cursor_=0,count_=0,record_count_=0;
    std::uint64_t overwritten_=0;
    bool recording_=false, requested_=false;
    timestamp record_start_{};
    double seconds_=30;
    template<class F> void each_recent(F f)
    {
        for (std::size_t n=0;n<std::min(count_,std::size_t(16));++n) f(storage_->live[(cursor_+512-1-n)%512]);
        if(recording_) for (std::size_t n=0;n<std::min(record_count_,std::size_t(16));++n) f(storage_->record[record_count_-1-n]);
    }
public:
    void configure(bool live, bool record, double seconds)
    {
        std::lock_guard lock(mutex_);
        if ((live || record) && !storage_) storage_=std::make_unique<storage>();
        seconds_=std::isfinite(seconds)?std::clamp(seconds,1.,50.):30.;
        if (record && !requested_) { record_count_=0; record_start_={}; recording_=true; }
        if (!record) recording_=false;
        if (recording_ && record_start_!=timestamp{} && milliseconds(record_start_,clock::now())>=seconds_*1000) recording_=false;
        requested_=record; enabled_.store(live || recording_);
    }
    void push(const analysis_sample& sample)
    {
        if (!enabled_.load()) return;
        std::unique_lock lock(mutex_,std::try_to_lock);
        if (!lock.owns_lock()) { ++dropped_; return; }
        if (!storage_ || sample.captured==timestamp{} || sample.published<sample.captured) { ++dropped_; return; }
        if (count_ && sample.captured<=storage_->live[(cursor_+511)%512].captured) { ++dropped_; return; }
        storage_->live[cursor_]=sample; cursor_=(cursor_+1)%512;
        if (count_==512) ++overwritten_; else ++count_;
        if (recording_)
        {
            if (record_start_==timestamp{}) record_start_=sample.captured;
            if (record_count_==4096 || milliseconds(record_start_,sample.captured)>=seconds_*1000) recording_=false;
            else storage_->record[record_count_++]=sample;
        }
    }
    void output(timestamp captured, const pose& filtered, const predicted_pose& prediction, timestamp output)
    {
        if (!enabled_.load()) return;
        std::unique_lock lock(mutex_,std::try_to_lock);
        if (!lock.owns_lock()) { ++dropped_; return; }
        if (!storage_) return;
        bool found=false;
        each_recent([&](analysis_sample& s) {
            if (s.validated!=captured || s.holding || !s.valid || output<captured) return;
            // Replace the latest output for this measured sample. This records
            // intermediate-frame assistance without appending duplicate samples
            // or adding an offset to an already predicted output.
            found=true;
            s.filtered=filtered; s.predicted=prediction.value; s.flow_correction=prediction.flow_correction;
            s.output=output; s.output_available=true;
        });
        if (!found) ++dropped_;
    }
    analysis_snapshot read(bool recording=false) const
    {
        std::lock_guard lock(mutex_); analysis_snapshot result;
        result.dropped=dropped_.load(); result.overwritten=overwritten_; result.recording=recording_;
        if (!storage_) return result;
        if (recording) result.samples.assign(storage_->record.begin(),storage_->record.begin()+record_count_);
        else { result.samples.reserve(count_); for(std::size_t i=0;i<count_;++i) result.samples.push_back(storage_->live[(cursor_+512-count_+i)%512]); }
        return result;
    }
};
}
