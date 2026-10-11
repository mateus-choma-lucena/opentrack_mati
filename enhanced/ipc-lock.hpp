#pragma once
#include <atomic>
#include <cstdint>

namespace enhanced {
// Shared-memory words are naturally aligned on Windows/Linux. No process-local mutex.
static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free);
inline bool try_frame_lock(std::uint32_t& word)
{
    std::uint32_t unlocked = 0;
    return std::atomic_ref<std::uint32_t>(word).compare_exchange_strong(unlocked, 1, std::memory_order_acquire);
}
inline void unlock_frame(std::uint32_t& word) { std::atomic_ref<std::uint32_t>(word).store(0, std::memory_order_release); }
inline std::uint32_t frame_sequence(std::uint32_t& word) { return std::atomic_ref<std::uint32_t>(word).load(std::memory_order_acquire); }
inline void publish_sequence(std::uint32_t& word, std::uint32_t sequence) { std::atomic_ref<std::uint32_t>(word).store(sequence, std::memory_order_release); }
} // namespace enhanced
