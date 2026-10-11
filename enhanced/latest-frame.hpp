#pragma once
#include "telemetry.hpp"
#include <condition_variable>
#include <utility>

namespace enhanced {
// One pending frame plus the producer and consumer buffers. Swapping ownership reuses storage.
template<class Frame> class latest_frame
{
    std::mutex mutex;
    std::condition_variable changed;
    Frame pending{};
    timestamp captured{};
    bool ready = false, stopped = false;
public:
    bool publish(Frame& producer, timestamp time)
    {
        std::lock_guard lock(mutex);
        if (stopped) return false;
        const bool replaced = ready;
        std::swap(pending, producer);
        captured = time; ready = true; changed.notify_one();
        return replaced;
    }
    bool take(Frame& consumer, timestamp& time, std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(mutex);
        changed.wait_for(lock, timeout, [&] { return ready || stopped; });
        if (stopped || !ready) return false;
        std::swap(consumer, pending); time = captured; ready = false;
        return true;
    }
    void clear() { std::lock_guard lock(mutex); ready = false; }
    void stop() { std::lock_guard lock(mutex); stopped = true; ready = false; changed.notify_all(); }
};
} // namespace enhanced
