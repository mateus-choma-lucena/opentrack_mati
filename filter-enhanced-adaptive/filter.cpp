#include "filter.hpp"
#include "enhanced/widgets.hpp"
#include <QDialogButtonBox>
#include <QVBoxLayout>

void enhanced_adaptive::filter(const double* input, double* output)
{ filter_sample(input, output, { enhanced::clock::now(), {} }); }

void enhanced_adaptive::filter_sample(const double* input, double* output, const tracking_sample_info& sample)
{ double target[6]; filter_sample_for_mapping(input,output,target,sample); }

void enhanced_adaptive::filter_sample_for_mapping(const double* input, double* output, double* target, const tracking_sample_info& sample)
{
    const enhanced::adaptive_parameters parameters{ *settings.adaptive_enabled, *settings.confidence_aware,
        *settings.rotation_smoothing, *settings.translation_smoothing, *settings.responsiveness, *settings.jitter };
    enhanced::pose measurement;
    std::copy_n(input, 6, measurement.begin());
    std::lock_guard lock(mutex);
    const auto result = state.update(measurement, sample.captured, parameters, sample.confidence);
    std::copy(result.begin(), result.end(), output);
    const auto guarded=state.mapping_target();
    std::copy(guarded.begin(),guarded.end(),target);
    debug_delta.store(std::abs(enhanced::delta(result[3], measurement[3], 3)));
}

enhanced_adaptive_dialog::enhanced_adaptive_dialog()
{
    setWindowTitle(tr("Enhanced Adaptive Filter"));
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form); enhanced_ui::adaptive(form, settings);
    debug = new QLabel; layout->addWidget(debug);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { save(); close(); });
    connect(buttons, &QDialogButtonBox::rejected, this, [this] { reload(); close(); });
    connect(&timer, &QTimer::timeout, this, [this] {
        debug->setVisible(*settings.debug);
        debug->setText(filter ? tr("Yaw smoothing correction: %1 degrees").arg(filter->activity(), 0, 'f', 3) : tr("Filter offline"));
    });
    timer.start(250);
}

OPENTRACK_DECLARE_FILTER(enhanced_adaptive, enhanced_adaptive_dialog, enhanced_adaptive_metadata)
