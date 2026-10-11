#pragma once
#include "options/options.hpp"
struct experimental_settings : options::opts
{
    options::value<int> flow_mode{b,"experimental-flow-mode",0};
    options::value<int> flow_quality{b,"experimental-flow-quality",1};
    options::value<double> fusion_strength{b,"experimental-fusion-strength",.25};
    options::value<bool> analyzer{b,"experimental-analyzer-enabled",false};
    options::value<bool> recording{b,"experimental-analyzer-recording",false};
    options::value<double> recording_seconds{b,"experimental-recording-seconds",30.};
    experimental_settings() : opts("neuralnet-tracker") {}
};
