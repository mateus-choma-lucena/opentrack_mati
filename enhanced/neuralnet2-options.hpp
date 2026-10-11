#pragma once
#include "options/options.hpp"
#include "neuralnet2.hpp"

// Same tracker bundle, exclusively additive nn2-* keys. Safe defaults for old INIs.
struct neuralnet2_settings : options::opts
{
    options::value<bool> enabled{b,"nn2-enabled",false};
    options::value<bool> validation{b,"nn2-pose-validation",true};
    options::value<bool> stabilization{b,"nn2-motion-stabilization",true};
    options::value<bool> recovery{b,"nn2-tracking-recovery",true};
    options::value<bool> reliability{b,"nn2-reliability-protection",true};
    options::value<double> spike_strength{b,"nn2-spike-strength",1.};
    options::value<double> responsiveness{b,"nn2-recovery-responsiveness",1.};
    options::value<double> hold_ms{b,"nn2-hold-ms",50.};
    options::value<double> angular_velocity{b,"nn2-angular-velocity-deg-s",1080.};
    options::value<double> translation_velocity{b,"nn2-translation-velocity-cm-s",250.};
    options::value<double> suspect_ms{b,"nn2-suspect-ms",100.};
    options::value<double> confirmation_ms{b,"nn2-confirmation-ms",60.};
    options::value<double> blend_ms{b,"nn2-blend-ms",120.};
    options::value<double> max_age_ms{b,"nn2-max-pose-age-ms",150.};
    neuralnet2_settings() : opts("neuralnet-tracker") {}
    enhanced::neuralnet_parameters parameters() const
    {
        return {*enabled,*validation,*stabilization,*recovery,*reliability,*spike_strength,*responsiveness,*hold_ms,
            *suspect_ms,*confirmation_ms,*blend_ms,*angular_velocity,*translation_velocity,*max_age_ms};
    }
};
