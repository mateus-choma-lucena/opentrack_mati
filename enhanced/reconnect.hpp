#pragma once
#include "telemetry.hpp"

namespace enhanced {
class reconnect_state
{
    unsigned attempts = 0;
    timestamp next{};
public:
    bool due(timestamp now, unsigned maximum)
    {
        if (attempts >= maximum || now < next) return false;
        const auto delay = std::chrono::milliseconds(250u << std::min(attempts, 4u));
        ++attempts; next = now+delay;
        return true;
    }
    void recovered() { attempts = 0; next = {}; }
    unsigned retries() const { return attempts; }
};
} // namespace enhanced
