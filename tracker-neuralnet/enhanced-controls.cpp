#include "ftnoir_tracker_neuralnet.h"
#include "enhanced/widgets.hpp"
#include <QGroupBox>
#include <QTabWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QGridLayout>
#include <QFileDialog>
#include <QSaveFile>
#include <QTextStream>

namespace neuralnet_tracker_ns {
enhanced::snapshot NeuralNetTracker::diagnostics() const
{
    auto result = telemetry_.read();
    // Validity reflects the sample actually available to the output pipeline.
    QMutexLocker lock(&mtx_);
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
        auto* widget = new QWidget(tabs);
        auto* form = new QFormLayout(widget);
        tabs->addTab(widget, title); return form;
    };
    auto* performance = page(tr("Performance"));
    auto* low_latency = enhanced_ui::check(performance, tr("Latest-frame capture worker (restart tracking to apply)"), settings_.low_latency);
    low_latency->setToolTip(tr("Uses one pending frame. Older pending frames are replaced; inference quality and model resolution remain unchanged."));
    enhanced_ui::number(performance, tr("Maximum host frame age"), settings_.max_frame_age, 5, 500, tr(" ms"));
    enhanced_ui::check(performance, tr("Automatically reconnect camera"), settings_.reconnect);
    auto* retries = new QSpinBox;
    retries->setRange(0, 10); performance->addRow(tr("Maximum consecutive retries"), retries);
    tie_setting(settings_.reconnect_attempts, retries);

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
        stream << "width,height,requested_fps,capture_delivery_fps,inference_pipeline_fps,inference_ms,host_frame_age_ms,host_processing_ms,p50_ms,p95_ms,p99_ms,captured,processed,queue_replacements,stale_frames,backend_sequence_gaps,reconnections,tracking_valid,adaptive_active,prediction_ms,prediction_degrees,prediction_cm,application_ms,application_p50_ms,application_p95_ms,application_p99_ms,output_samples\n";
        stream << d.width << ',' << d.height << ',' << d.requested_fps << ',' << number(d.capture_fps) << ',' << number(d.inference_fps) << ','
            << number(d.inference_ms) << ',' << number(d.frame_age_ms) << ',' << number(d.processing_ms) << ','
            << number(d.p50_ms) << ',' << number(d.p95_ms) << ',' << number(d.p99_ms) << ','
            << qulonglong(d.captured) << ',' << qulonglong(d.processed) << ',' << qulonglong(d.overwritten) << ',' << qulonglong(d.stale_frames) << ','
            << (d.backend_drops ? QString::number(qulonglong(*d.backend_drops)) : QString()) << ','
            << qulonglong(d.reconnects) << ',' << d.valid << ',' << d.adaptive_filter << ',' << d.prediction_ms << ',' << d.prediction_rotation << ',' << d.prediction_translation << ','
            << number(d.application_ms) << ',' << number(d.application_p50_ms) << ',' << number(d.application_p95_ms) << ',' << number(d.application_p99_ms) << ',' << qulonglong(d.output_samples) << '\n';
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
