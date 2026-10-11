#include "ftnoir_tracker_neuralnet.h"
#include "enhanced/widgets.hpp"
#include "enhanced/head-controls-ui.hpp"
#include <QGroupBox>
#include <QTabWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QGridLayout>
#include <QFileDialog>
#include <QSaveFile>
#include <QTextStream>
#include <QScrollArea>

namespace neuralnet_tracker_ns {
enhanced::snapshot NeuralNetTracker::diagnostics() const
{
    auto result = telemetry_.read();
    // Validity reflects the sample actually available to the output pipeline.
    QMutexLocker lock(&mtx_);
    if (*nn2_settings_.enabled)
    {
        nn2_result_=nn2_pipeline_.poll(enhanced::clock::now(),capture_valid_.load());
        result.neuralnet=nn2_result_.diagnostics;
        result.neuralnet.stabilization_active=result.neuralnet.stabilization_requested && result.adaptive_filter;
        result.valid=nn2_result_.valid && result.neuralnet.enabled;
        return result;
    }
    result.neuralnet.enabled=false;
    result.valid = tracking_valid_ && !enhanced::stale(published_frame_time_, enhanced::clock::now(),
        low_latency_ ? std::clamp(*settings_.max_frame_age, 5., 500.) : 1000.);
    return result;
}

void NeuralNetDialog::make_enhanced_controls()
{
    auto* group = new QGroupBox(tr("Enhanced"), this);
    auto* outer = new QVBoxLayout(group);
    auto* tabs = new QTabWidget(group);
    tabs->setObjectName(QStringLiteral("enhancedTabs"));
    outer->addWidget(tabs);
    auto page = [tabs](const QString& title) {
        auto* widget = new QWidget;
        auto* form = new QFormLayout(widget);
        auto* scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setWidget(widget);
        tabs->addTab(scroll, title); return form;
    };
    auto* performance = page(tr("Performance"));
    auto* low_latency = enhanced_ui::check(performance, tr("Latest-frame capture worker (restart tracking to apply)"), settings_.low_latency);
    low_latency->setToolTip(tr("Uses one pending frame. Older pending frames are replaced; inference quality and model resolution remain unchanged."));
    enhanced_ui::number(performance, tr("Maximum host frame age"), settings_.max_frame_age, 5, 500, tr(" ms"));
    enhanced_ui::check(performance, tr("Automatically reconnect camera"), settings_.reconnect);
    auto* retries = new QSpinBox;
    retries->setRange(0, 10); performance->addRow(tr("Maximum consecutive retries"), retries);
    tie_setting(settings_.reconnect_attempts, retries);

    auto* nn2=page(tr("NeuralNet 2.0"));
    auto* master=enhanced_ui::check(nn2,tr("Enable NeuralNet 2.0"),nn2_settings_.enabled);
    master->setObjectName(QStringLiteral("neuralnet2Enabled"));
    master->setToolTip(tr("Optional temporal validation and recovery. Disabled by default; existing profiles keep their original processing."));
    enhanced_ui::check(nn2,tr("Pose Validation"),nn2_settings_.validation)->setToolTip(
        tr("Rejects corrupt poses and isolated jumps using acquisition time and short pose history. A continuous second sample can confirm fast movement."));
    enhanced_ui::check(nn2,tr("Motion-Aware Stabilization"),nn2_settings_.stabilization)->setToolTip(
        tr("Shares motion state with the enabled Enhanced Adaptive Filter. Select that filter and enable it to activate smoothing; no additional tracker smoother is stacked."));
    enhanced_ui::check(nn2,tr("Tracking Recovery"),nn2_settings_.recovery)->setToolTip(
        tr("Time-based TRACKING / SUSPECT / LOST / REACQUIRING states. Short holds retain the original timestamp; prediction stops during holds and recovery."));
    enhanced_ui::check(nn2,tr("Reliability-Based Protection"),nn2_settings_.reliability)->setToolTip(
        tr("Uses observed continuity and frame age for conservative stationary-history protection. Estimated reliability is not calibrated NeuralNet confidence."));
    enhanced_ui::number(nn2,tr("Spike Rejection Strength"),nn2_settings_.spike_strength,.25,3)->setToolTip(
        tr("Higher values detect smaller isolated jumps. Continuous movement is confirmed without permanently clamping it."));
    enhanced_ui::number(nn2,tr("Recovery Responsiveness"),nn2_settings_.responsiveness,.25,3)->setToolTip(
        tr("Scales confirmation time and bounded quaternion/translation recovery speed. 1.0 is the conservative default."));
    enhanced_ui::number(nn2,tr("Hold Duration"),nn2_settings_.hold_ms,0,250,tr(" ms"))->setToolTip(
        tr("Maximum hold since the last good pose and the first failed observation. 0 disables holding. Camera disconnect never holds a pose."));
    auto* advanced=new QGroupBox(tr("Advanced Settings"));
    advanced->setObjectName(QStringLiteral("neuralnet2Advanced"));
    advanced->setCheckable(true); advanced->setChecked(false);
    auto* advanced_widget=new QWidget(advanced);
    auto* advanced_layout=new QVBoxLayout(advanced); advanced_layout->addWidget(advanced_widget);
    auto* advanced_form=new QFormLayout(advanced_widget); advanced_widget->setVisible(false);
    connect(advanced,&QGroupBox::toggled,advanced_widget,&QWidget::setVisible);
    nn2->addRow(advanced);
    enhanced_ui::number(advanced_form,tr("Angular velocity limit"),nn2_settings_.angular_velocity,90,3600,tr(" degrees/s"));
    enhanced_ui::number(advanced_form,tr("Translation velocity limit"),nn2_settings_.translation_velocity,10,1000,tr(" cm/s"));
    enhanced_ui::number(advanced_form,tr("Suspect to lost timeout"),nn2_settings_.suspect_ms,20,500,tr(" ms"));
    enhanced_ui::number(advanced_form,tr("Reacquisition confirmation"),nn2_settings_.confirmation_ms,0,250,tr(" ms"));
    enhanced_ui::number(advanced_form,tr("Recovery blend duration"),nn2_settings_.blend_ms,20,500,tr(" ms"));
    enhanced_ui::number(advanced_form,tr("Maximum pose age (serial capture)"),nn2_settings_.max_age_ms,5,1000,tr(" ms"))->setToolTip(
        tr("Serial mode age limit. Latest-frame mode uses Maximum host frame age from Performance. All timestamps start at host receipt, not sensor exposure."));

    enhanced_ui::adaptive(page(tr("Adaptive Smoothness")), enhanced_settings_);

    auto* camera = page(tr("PS3 Eye Camera"));
    camera_modes_label_ = new QLabel;
    camera_modes_label_->setWordWrap(true);
    camera->addRow(camera_modes_label_);
    auto* controls = new QPushButton(tr("Exposure / gain / automatic exposure"));
    camera->addRow(controls);
    connect(controls, &QPushButton::clicked, this, &NeuralNetDialog::camera_settings);

    auto* prediction = page(tr("Prediction"));
    enhanced_ui::check(prediction, tr("Enable motion prediction after filtering"), enhanced_settings_.prediction_enabled);
    enhanced_ui::number(prediction, tr("Prediction horizon"), enhanced_settings_.prediction_ms, 0, 15, tr(" ms"));
    enhanced_ui::number(prediction, tr("Maximum angular extrapolation"), enhanced_settings_.max_rotation, 0, 10, tr(" degrees"));
    enhanced_ui::number(prediction, tr("Maximum translation extrapolation"), enhanced_settings_.max_translation, 0, 2, tr(" cm"));

    auto* diagnostics = page(tr("Diagnostics"));
    enhanced_ui::check(diagnostics, tr("Show performance measurements"), settings_.diagnostics);
    diagnostics_label_ = new QLabel;
    diagnostics_label_->setWordWrap(true);
    diagnostics_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    diagnostics->addRow(diagnostics_label_);
    auto* export_csv = new QPushButton(tr("Export diagnostics snapshot (CSV)"));
    export_csv->setObjectName(QStringLiteral("exportDiagnosticsCsv"));
    diagnostics->addRow(export_csv);
    connect(export_csv, &QPushButton::clicked, this, [this] {
        if (!tracker_) return;
        const auto filename = QFileDialog::getSaveFileName(this, tr("Export diagnostics"), "enhanced-diagnostics.csv", tr("CSV files (*.csv)"));
        if (filename.isEmpty()) return;
        QSaveFile file(filename);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
        const auto d = tracker_->diagnostics();
        const auto number = [](std::optional<double> value) { return value ? QString::number(*value, 'f', 4) : QString(); };
        QTextStream stream(&file);
        stream << "width,height,requested_fps,capture_delivery_fps,inference_pipeline_fps,inference_ms,host_frame_age_ms,host_processing_ms,p50_ms,p95_ms,p99_ms,captured,processed,queue_replacements,stale_frames,backend_sequence_gaps,reconnections,tracking_valid,adaptive_active,prediction_ms,prediction_degrees,prediction_cm,application_ms,application_p50_ms,application_p95_ms,application_p99_ms,output_samples,head_enabled,head_transitioning,amplification_percent,effective_global_gain,speed_percent,effective_responsiveness,shared_response,fallback_response,estimated_smoothing_time_constant_ms,sample_capture_monotonic_ms,output_monotonic_ms,head_output_valid";
        stream << ",nn2_enabled,nn2_tracking_state,nn2_estimated_reliability,nn2_rejected_total,nn2_spikes_total,nn2_reacquisitions_total,nn2_loss_duration_ms,nn2_motion,nn2_raw_timestamp_ms,nn2_validated_timestamp_ms,nn2_capture_to_pose_age_ms,nn2_validation_ms,nn2_recovery_active,nn2_raw_valid,nn2_output_valid,nn2_holding,nn2_stabilization_requested,nn2_stabilization_active";
        stream << ",head_per_axis";
        for (const auto* prefix : {"effective_gain", "raw", "protocol", "mapped_target", "response_cutoff_hz", "movement_limit"})
            for (const auto* axis : {"x","y","z","yaw","pitch","roll"}) stream << ',' << prefix << '_' << axis;
        stream << '\n';
        stream << d.width << ',' << d.height << ',' << d.requested_fps << ',' << number(d.capture_fps) << ',' << number(d.inference_fps) << ','
            << number(d.inference_ms) << ',' << number(d.frame_age_ms) << ',' << number(d.processing_ms) << ','
            << number(d.p50_ms) << ',' << number(d.p95_ms) << ',' << number(d.p99_ms) << ','
            << qulonglong(d.captured) << ',' << qulonglong(d.processed) << ',' << qulonglong(d.overwritten) << ',' << qulonglong(d.stale_frames) << ','
            << (d.backend_drops ? QString::number(qulonglong(*d.backend_drops)) : QString()) << ','
            << qulonglong(d.reconnects) << ',' << d.valid << ',' << d.adaptive_filter << ',' << d.prediction_ms << ',' << d.prediction_rotation << ',' << d.prediction_translation << ','
            << number(d.application_ms) << ',' << number(d.application_p50_ms) << ',' << number(d.application_p95_ms) << ',' << number(d.application_p99_ms) << ',' << qulonglong(d.output_samples) << ','
            << d.head.enabled << ',' << d.head.transitioning << ',' << d.head.amplification << ',' << d.head.gain << ',' << d.head.speed << ',' << d.head.responsiveness << ','
            << d.head.shared_response << ',' << d.head.fallback_response << ',' << number(d.head.estimated_lag_ms) << ','
            << enhanced_ui::stamp_text(d.head.captured) << ',' << enhanced_ui::stamp_text(d.head.submitted) << ',' << d.head.valid;
        const auto& n=d.neuralnet;
        stream << ',' << n.enabled << ',' << enhanced::name(n.state) << ',' << n.reliability << ',' << qulonglong(n.rejected) << ','
            << qulonglong(n.spikes) << ',' << qulonglong(n.reacquisitions) << ',' << n.loss_duration_ms << ',' << enhanced::name(n.motion) << ','
            << enhanced_ui::stamp_text(n.raw_time) << ',' << enhanced_ui::stamp_text(n.validated_time) << ',' << number(n.capture_to_pose_ms) << ','
            << n.validation_ms << ',' << n.recovery_active << ',' << n.raw_valid << ',' << n.output_valid << ',' << n.holding << ','
            << n.stabilization_requested << ',' << n.stabilization_active;
        stream << ',' << d.head.per_axis;
        for (const auto& values : {d.head.gains,d.head.raw,d.head.processed,d.head.mapped_target,d.head.response_cutoffs,d.head.limits}) for (double value : values) stream << ',' << value;
        stream << '\n';
        stream.flush();
        if (!file.commit()) qWarning() << "Could not save diagnostics CSV:" << file.errorString();
    });
    auto* note = new QLabel(tr("Frame age and latency begin at host receipt. Processing ends at pose publication; application latency ends after the first protocol submission of each sample. Camera buffering, exposure, USB/output transit and display latency are unavailable."));
    note->setWordWrap(true); diagnostics->addRow(note);
    ui_.gridLayout->removeWidget(ui_.resolution_display);
    ui_.gridLayout->removeWidget(ui_.buttonBox);
    ui_.gridLayout->addWidget(group, 7, 0);
    ui_.gridLayout->addWidget(ui_.resolution_display, 8, 0);
    ui_.gridLayout->addWidget(ui_.buttonBox, 9, 0);
}
} // namespace neuralnet_tracker_ns
