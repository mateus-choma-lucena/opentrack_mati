#pragma once
#include "widgets.hpp"
#include "head-controls.hpp"
#include <QSlider>
#include <QPushButton>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QTimer>
#include <functional>

namespace enhanced_ui {
inline QString pose_text(const std::array<double,6>& p)
{
    return QObject::tr("X %1, Y %2, Z %3 cm; yaw %4, pitch %5, roll %6 degrees")
        .arg(p[0],0,'f',2).arg(p[1],0,'f',2).arg(p[2],0,'f',2)
        .arg(p[3],0,'f',2).arg(p[4],0,'f',2).arg(p[5],0,'f',2);
}
inline QString stamp_text(enhanced::timestamp time)
{ return time==enhanced::timestamp{} ? QObject::tr("N/A") : QString::number(std::chrono::duration<double,std::milli>(time.time_since_epoch()).count(),'f',3); }
inline QString head_diagnostics_text(const enhanced::head_controls_snapshot& d)
{
    return QObject::tr("Head Movement Controls: %1%2; Amplification %3%; effective global gain %4x\n"
        "Effective gains (X / Y / Z / yaw / pitch / roll): %5 / %6 / %7 / %8 / %9 / %10\n"
        "Speed %11%; effective responsiveness %12x (%13)\n"
        "Estimated largest smoothing time constant: %14 ms (model estimate, not measured latency)\n"
        "Raw: %15\nProtocol output: %16\n"
        "Sample capture / output timestamps: %17 / %18 ms (monotonic host clock); valid output: %19")
        .arg(d.enabled ? QObject::tr("on") : QObject::tr("off"), d.transitioning ? QObject::tr(" (transitioning)") : QString())
        .arg(d.amplification).arg(d.gain,0,'f',3)
        .arg(d.gains[0],0,'f',3).arg(d.gains[1],0,'f',3).arg(d.gains[2],0,'f',3)
        .arg(d.gains[3],0,'f',3).arg(d.gains[4],0,'f',3).arg(d.gains[5],0,'f',3)
        .arg(d.speed).arg(d.responsiveness,0,'f',3)
        .arg(d.shared_response ? QObject::tr("Enhanced Adaptive Filter") : d.fallback_response ? QObject::tr("Speed response stage") : QObject::tr("selected filter unchanged"))
        .arg(d.estimated_lag_ms ? QString::number(*d.estimated_lag_ms,'f',2) : QObject::tr("N/A"))
        .arg(pose_text(d.raw),pose_text(d.processed),stamp_text(d.captured),stamp_text(d.submitted),d.valid ? QObject::tr("yes") : QObject::tr("no"));
}
inline QSlider* percentage(QFormLayout* form, const QString& name, const QString& title, options::value<int>& setting, const QString& tip)
{
    auto* row=new QWidget;
    auto* layout=new QHBoxLayout(row); layout->setContentsMargins(0,0,0,0);
    auto* slider=new QSlider(Qt::Horizontal); slider->setRange(0,100); slider->setObjectName(name);
    slider->setAccessibleName(title); slider->setToolTip(tip);
    auto* label=new QLabel; label->setMinimumWidth(45); label->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
    layout->addWidget(slider,1); layout->addWidget(label);
    QObject::connect(slider,&QSlider::valueChanged,&setting,[&setting,label](int value) { setting=value; label->setText(QString::number(value)+"%"); });
    options::value_::connect(&setting,options::value_::value_changed<int>(),slider,
        [slider](int value) { slider->setValue(std::clamp(value,0,100)); },setting.SAFE_CONNTYPE);
    slider->setValue(std::clamp(*setting,0,100)); label->setText(QString::number(slider->value())+"%");
    form->addRow(title,row); return slider;
}
inline void head_movement(QFormLayout* form, enhanced_settings& s,
                         std::function<std::optional<enhanced::head_controls_snapshot>()> reader={})
{
    auto* enable=check(form,QObject::tr("Enable Head Movement Controls"),s.head_enabled);
    enable->setObjectName("headControlsEnabled");
    enable->setToolTip(QObject::tr("Defaults preserve the existing output. Setting changes blend over 120 ms; turning this off restores the original pipeline."));
    auto* amplification=percentage(form,"headAmplification",QObject::tr("Amplification"),s.amplification,
        QObject::tr("Scales virtual camera movement after the response curves. 0% = 0.25x, 42% = 1x, 100% = 4x. Does not change smoothing or prediction."));
    percentage(form,"headSpeed",QObject::tr("Speed"),s.speed,
        QObject::tr("Higher values respond faster; lower values stabilize movement. 55% preserves the selected filter. Prediction horizon is unchanged."));
    auto* advanced=check(form,QObject::tr("Override global amplification per axis"),s.per_axis);
    advanced->setObjectName("headPerAxis");
    advanced->setToolTip(QObject::tr("Axis values replace the global value; gains are never multiplied together."));
    auto* axis_widget=new QWidget;
    auto* axes=new QFormLayout(axis_widget); axes->setContentsMargins(0,0,0,0);
    std::array<options::value<int>*,6> values{&s.amplification_x,&s.amplification_y,&s.amplification_z,&s.amplification_yaw,&s.amplification_pitch,&s.amplification_roll};
    const std::array<QString,6> names{QObject::tr("X"),QObject::tr("Y"),QObject::tr("Z"),QObject::tr("Yaw"),QObject::tr("Pitch"),QObject::tr("Roll")};
    for (std::size_t i=0;i<6;++i) percentage(axes,"headAxis"+QString::number(i),names[i],*values[i],amplification->toolTip());
    form->addRow(axis_widget);
    QObject::connect(advanced,&QCheckBox::toggled,axis_widget,&QWidget::setVisible);
    QObject::connect(advanced,&QCheckBox::toggled,amplification,[amplification](bool checked) { amplification->setEnabled(!checked); });
    axis_widget->setVisible(*s.per_axis); amplification->setEnabled(!*s.per_axis);
    auto* reset=new QPushButton(QObject::tr("Reset to neutral")); reset->setObjectName("headControlsReset"); form->addRow(reset);
    QObject::connect(reset,&QPushButton::clicked,reset,[&s,values] {
        s.amplification=42; s.speed=55; s.per_axis=false;
        for (auto* value:values) *value=42;
    });
    auto* note=new QLabel(QObject::tr("Neutral: Amplification 42% = 1x; Speed 55% keeps the current response. "
        "Speed adjusts Enhanced Adaptive Filter directly. With other filters, values away from 55% use the Speed response stage. "
        "Disable NeuralNet internal smoothing manually when comparing responsiveness."));
    note->setWordWrap(true); form->addRow(note);
    if (reader)
    {
        auto* group=new QGroupBox(QObject::tr("Live diagnostics")); group->setFlat(true); group->setCheckable(true); group->setChecked(false);
        group->setObjectName("headLiveDiagnostics");
        auto* layout=new QVBoxLayout(group); auto* label=new QLabel; label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse); label->setVisible(false); layout->addWidget(label); form->addRow(group);
        QObject::connect(group,&QGroupBox::toggled,label,&QWidget::setVisible);
        auto* timer=new QTimer(group);
        QObject::connect(timer,&QTimer::timeout,label,[reader=std::move(reader),label,group] {
            if (!group->isChecked() || !group->isVisible()) return;
            const auto d=reader(); label->setText(d ? head_diagnostics_text(*d) : QObject::tr("Tracking offline; measurements unavailable."));
        });
        timer->start(250);
    }
}
} // namespace enhanced_ui
