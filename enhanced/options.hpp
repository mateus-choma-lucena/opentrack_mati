#pragma once
#include "options/options.hpp"

// Additive profile keys; older profiles keep their original behavior.
struct enhanced_settings : options::opts
{
    options::value<bool> adaptive_enabled{b, "adaptive-enabled", false};
    options::value<double> rotation_smoothing{b, "rotation-smoothing", 1.};
    options::value<double> translation_smoothing{b, "translation-smoothing", 1.};
    options::value<double> responsiveness{b, "responsiveness", 1.};
    options::value<double> jitter{b, "jitter-protection", 1.};
    options::value<bool> confidence_aware{b, "confidence-aware", false};
    options::value<bool> debug{b, "debug", false};
    options::value<bool> prediction_enabled{b, "prediction-enabled", false};
    options::value<double> prediction_ms{b, "prediction-ms", 8.};
    options::value<double> max_rotation{b, "prediction-max-rotation", 3.};
    options::value<double> max_translation{b, "prediction-max-translation", .5};
    enhanced_settings() : opts("enhanced-tracking") {}
};
