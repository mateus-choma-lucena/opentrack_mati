#pragma once
#include "widgets.hpp"
#include "head-controls.hpp"
#include <QSlider>
#include <QPushButton>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QTimer>
#include <functional>
#include <QGridLayout>
#include <QSpinBox>
#include <QSignalBlocker>

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
        "Speed %11%; mapped output responsiveness %12x (%13)\n"
        "Estimated largest smoothing time constant: %14 ms (model estimate, not measured latency)\n"
        "Raw: %15\nProtocol output: %16\n"
        "Sample capture / output timestamps: %17 / %18 ms (monotonic host clock); valid output: %19")
        .arg(d.enabled ? QObject::tr("on") : QObject::tr("off"), d.transitioning ? QObject::tr(" (transitioning)") : QString())
        .arg(d.amplification).arg(d.gain,0,'f',3)
        .arg(d.gains[0],0,'f',3).arg(d.gains[1],0,'f',3).arg(d.gains[2],0,'f',3)
        .arg(d.gains[3],0,'f',3).arg(d.gains[4],0,'f',3).arg(d.gains[5],0,'f',3)
        .arg(d.speed).arg(d.responsiveness,0,'f',3)
        .arg(d.fallback_response ? QObject::tr("post-mapping response; parallel filter reference") : QObject::tr("original response"))
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
        [&setting,slider,label](int) {
            const int value=std::clamp(*setting,0,100);
            const QSignalBlocker blocker(slider);
            slider->setValue(value); label->setText(QString::number(value)+"%");
        },setting.SAFE_CONNTYPE);
    slider->setValue(std::clamp(*setting,0,100)); label->setText(QString::number(slider->value())+"%");
    form->addRow(title,row); return slider;
}
inline QCheckBox* head_check(QFormLayout* form, const QString& title, options::value<bool>& setting)
{
    auto* widget=new QCheckBox(title);
    widget->setChecked(*setting);
    QObject::connect(widget,&QCheckBox::toggled,&setting,[&setting](bool value) { setting=value; });
    options::value_::connect(&setting,options::value_::value_changed<bool>(),widget,
        [&setting,widget](bool) { const QSignalBlocker blocker(widget); widget->setChecked(*setting); },setting.SAFE_CONNTYPE);
    form->addRow(widget); return widget;
}
inline void head_movement(QFormLayout* form, enhanced_settings& s,
                         std::function<std::optional<enhanced::head_controls_snapshot>()> reader={},
                         std::function<int()> axis=[] { return 3; }, std::function<double(int)> limit=[](int) { return 180.; })
{
    auto* enable=head_check(form,QObject::tr("Enable Head Movement Controls"),s.head_enabled);
    enable->setObjectName("headControlsEnabled");
    enable->setToolTip(QObject::tr("Defaults preserve the existing output. Setting changes blend over 120 ms; turning this off restores the original pipeline."));
    auto* amplification=percentage(form,"headAmplification",QObject::tr("Amplification"),s.amplification,
        QObject::tr("Scales virtual camera movement after the response curves. 0% = 0.25x, 42% = 1x, 100% = 4x. Does not change smoothing or prediction."));
    percentage(form,"headSpeed",QObject::tr("Speed"),s.speed,
        QObject::tr("Controls convergence after the mapping curves. 55% preserves the original response; other values scale its observed responsiveness. No capture or prediction settings change."));
    auto* advanced=head_check(form,QObject::tr("Override global amplification per axis"),s.per_axis);
    advanced->setObjectName("headPerAxis");
    advanced->setToolTip(QObject::tr("Axis values replace the global value; gains are never multiplied together."));
    auto* axis_widget=new QWidget;
    auto* axes=new QGridLayout(axis_widget); axes->setContentsMargins(0,0,0,0);
    std::array<options::value<int>*,6> values{&s.amplification_x,&s.amplification_y,&s.amplification_z,&s.amplification_yaw,&s.amplification_pitch,&s.amplification_roll};
    const std::array<QString,6> names{QObject::tr("X"),QObject::tr("Y"),QObject::tr("Z"),QObject::tr("Yaw"),QObject::tr("Pitch"),QObject::tr("Roll")};
    for (int i=0;i<6;++i) {
        auto* spin=new QSpinBox; spin->setRange(0,100); spin->setSuffix("%"); spin->setObjectName("headAxis"+QString::number(i));
        spin->setToolTip(amplification->toolTip()); spin->setValue(**values[i]);
        auto* setting=values[i];
        QObject::connect(spin,&QSpinBox::valueChanged,setting,[setting](int value) { *setting=value; });
        options::value_::connect(setting,options::value_::value_changed<int>(),spin,
            [setting,spin](int) { const QSignalBlocker blocker(spin); spin->setValue(std::clamp(**setting,0,100)); },setting->SAFE_CONNTYPE);
        axes->addWidget(new QLabel(names[i]),i/3,(i%3)*2); axes->addWidget(spin,i/3,(i%3)*2+1);
    }
    form->addRow(axis_widget);
    QObject::connect(advanced,&QCheckBox::toggled,axis_widget,&QWidget::setVisible);
    QObject::connect(advanced,&QCheckBox::toggled,amplification,[amplification](bool checked) { amplification->setEnabled(!checked); });
    axis_widget->setVisible(*s.per_axis); amplification->setEnabled(!*s.per_axis);
    options::value_::connect(&s.per_axis,options::value_::value_changed<bool>(),axis_widget,
        [&s,axis_widget,amplification](bool) { axis_widget->setVisible(*s.per_axis); amplification->setEnabled(!*s.per_axis); },s.per_axis.SAFE_CONNTYPE);
    auto* reset=new QPushButton(QObject::tr("Reset to neutral")); reset->setObjectName("headControlsReset"); form->addRow(reset);
    QObject::connect(reset,&QPushButton::clicked,reset,[&s,values] {
        s.amplification=42; s.speed=55; s.per_axis=false;
        for (auto* value:values) *value=42;
    });
    auto* gain_label=new QLabel; gain_label->setObjectName("headEffectiveGain");
    auto* response_label=new QLabel; response_label->setObjectName("headEffectiveResponse");
    gain_label->setWordWrap(true); response_label->setWordWrap(true);
    gain_label->setTextInteractionFlags(Qt::TextSelectableByMouse); response_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(gain_label); form->addRow(response_label);
    auto refresh=[&s,values,names,reader=std::move(reader),axis=std::move(axis),limit=std::move(limit),gain_label,response_label] {
        const int k=std::clamp(axis(),0,5);
        const auto d=reader ? reader() : std::optional<enhanced::head_controls_snapshot>{};
        const double gain=*s.head_enabled ? enhanced::amplification_gain(*s.per_axis ? **values[k] : *s.amplification) : 1.;
        gain_label->setText(QObject::tr("Effective %1 target gain: %2x%3; mapped movement limit: ±%4 %5")
            .arg(names[k]).arg(gain,0,'f',3).arg(d&&d->transitioning ? QObject::tr(" (live %1x)").arg(d->gains[k],0,'f',3) : QString())
            .arg(limit(k),0,'f',1).arg(k<3 ? QObject::tr("cm") : QObject::tr("degrees")));
        if (!*s.head_enabled || *s.speed==55) response_label->setText(QObject::tr("Response: original selected filter (neutral Speed 55%; no added output lag)"));
        else {
            const double cutoff=d&&d->fallback_response&&d->response_cutoffs[k]>0 ? d->response_cutoffs[k] : 12.*enhanced::speed_response(*s.speed);
            response_label->setText(QObject::tr("Estimated %1 response: %2 ms / %3 Hz; %4")
                .arg(names[k]).arg(1000./(6.283185307179586*cutoff),0,'f',1).arg(cutoff,0,'f',2)
                .arg(d&&d->fallback_response ? QObject::tr("from live mapped filter reference") : QObject::tr("12 Hz baseline until tracking starts")));
        }
    };
    auto* timer=new QTimer(gain_label); QObject::connect(timer,&QTimer::timeout,gain_label,refresh); timer->start(100);
    auto* refresh_context=static_cast<QObject*>(gain_label);
    options::tie_setting(s.head_enabled,refresh_context,[refresh](bool) { refresh(); });
    options::tie_setting(s.amplification,refresh_context,[refresh](int) { refresh(); });
    options::tie_setting(s.speed,refresh_context,[refresh](int) { refresh(); });
    options::tie_setting(s.per_axis,refresh_context,[refresh](bool) { refresh(); });
    for (auto* value:values) options::tie_setting(*value,refresh_context,[refresh](int) { refresh(); });
    refresh();
}
} // namespace enhanced_ui
