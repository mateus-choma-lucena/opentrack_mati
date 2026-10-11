#include "calibration-dialog.hpp"
#include "enhanced/options.hpp"
#include "smart-mapping-dialog.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace enhanced::calibration;
static const char* axis_names[]={"X","Y","Z","Yaw","Pitch","Roll"};

class calibration_preview final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(calibration_preview)
public:
    spline positive,negative;
    axis_result axis;
    bool valid=false;
    int selected=3;
    explicit calibration_preview(QWidget* parent) : QWidget(parent) { setMinimumSize(620,220); private_spline(positive); private_spline(negative); }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.fillRect(rect(),palette().base()); p.setRenderHint(QPainter::Antialiasing);
        const QRectF area(55,25,width()-80,height()-65);
        p.setPen(palette().text().color()); p.drawRect(area);
        if (!valid) { p.drawText(area,Qt::AlignCenter,tr("Calibrate this axis to preview its curves.")); return; }
        const double input_limit=std::max(axis.positive.magnitude,axis.negative.magnitude)*1.08;
        const double output_limit=std::max(axis.target_positive,axis.target_negative)*1.08;
        const auto screen=[&](double x,double y) { return QPointF(area.center().x()+x/input_limit*area.width()/2,area.center().y()-y/output_limit*area.height()/2); };
        p.setPen(QPen(palette().mid().color(),1)); p.drawLine(screen(-input_limit,0),screen(input_limit,0)); p.drawLine(screen(0,-output_limit),screen(0,output_limit));
        QPainterPath path;
        for (int k=0;k<=1024;++k) {
            const double x=-input_limit+2*input_limit*k/1024;
            const double y=enhanced::adjusted_mapping_output((x<0?negative:positive).get_value_no_save(x),axis.output_gain,selected<3?600.:180.);
            if (!k) path.moveTo(screen(x,y)); else path.lineTo(screen(x,y));
        }
        p.setPen(QPen(QColor(230,140,30),2)); p.drawPath(path);
        p.setPen(palette().text().color());
        p.drawText(QRectF(0,height()-30,width(),25),Qt::AlignCenter,tr("Physical %1 (%2), negative ← 0 → positive").arg(axis_names[selected],selected<3?"cm":"degrees"));
        p.drawText(QRectF(55,1,width()-80,22),Qt::AlignCenter,tr("Virtual output: −%1 / +%2 %3").arg(axis.target_negative,0,'f',1).arg(axis.target_positive,0,'f',1).arg(selected<3?"cm":"degrees"));
    }
};

calibration_dialog::calibration_dialog(Mappings& m,calibration_input in,std::function<bool()> stop,std::function<bool()> center,QWidget* parent)
    : QDialog(parent),mappings(m),input(std::move(in)),stop_tracking(std::move(stop)),center_tracking(std::move(center)) {
    setWindowTitle(tr("Automatic Head Movement Calibration")); setObjectName("automaticCalibrationWizard"); resize(860,780);
    auto* layout=new QVBoxLayout(this); pages=new QStackedWidget(this); layout->addWidget(pages);
    auto* gather=new QWidget; auto* form=new QVBoxLayout(gather); pages->addWidget(gather);
    instructions=new QLabel; instructions->setWordWrap(true); form->addWidget(instructions);
    auto* note=new QLabel(tr("Use only a relaxed, comfortable range. Never force your neck. First move gently to the requested position, then press Record and hold it. No curves change during measurement.\nRequires NeuralNet 2.0 with pose validation enabled. Loss or poor reliability requires a retry. Recenter restarts the sequence."));
    note->setWordWrap(true); form->addWidget(note);
    roll=new QCheckBox(tr("Include roll (optional)")); translation=new QCheckBox(tr("Include X/Y/Z translations (optional)"));
    roll->setObjectName("calibrationRoll"); translation->setObjectName("calibrationTranslations");
    form->addWidget(roll); form->addWidget(translation);
    use_case=new QComboBox; use_case->setEditable(true); use_case->addItems({tr("General head tracking"),tr("Flight simulation"),tr("Driving simulation")}); form->addWidget(use_case);
    comfortable=new QCheckBox; comfortable->setObjectName("calibrationComfort"); form->addWidget(comfortable);
    status=new QLabel; status->setWordWrap(true); status->setObjectName("calibrationStatus"); form->addWidget(status);
    auto* row=new QHBoxLayout; record_button=new QPushButton(tr("Record / Retry")); next_button=new QPushButton(tr("Confirm and continue"));
    record_button->setObjectName("calibrationRecord"); next_button->setObjectName("calibrationNext"); row->addWidget(record_button); row->addWidget(next_button); form->addLayout(row); form->addStretch();
    auto* review_page=new QWidget; auto* review_layout=new QVBoxLayout(review_page); pages->addWidget(review_page);
    review_stats=new QLabel; review_stats->setWordWrap(true); review_layout->addWidget(review_stats);
    table=new QTableWidget(6,7); table->setObjectName("calibrationReviewTable");
    table->setHorizontalHeaderLabels({tr("Axis"),tr("Physical −"),tr("Physical +"),tr("Virtual −"),tr("Virtual +"),tr("Deadzone"),tr("Shape")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); table->verticalHeader()->hide(); table->setMinimumHeight(210); table->setMaximumHeight(220); review_layout->addWidget(table);
    preview_axis=new QComboBox; for (int i=0;i<6;++i) preview_axis->addItem(axis_names[i],i); preview_axis->setCurrentIndex(3); review_layout->addWidget(preview_axis);
    preview=new calibration_preview(this); review_layout->addWidget(preview);
    auto* smart=new QPushButton(tr("Compare styles, edit points, select axes or save as…"));smart->setObjectName("calibrationSmartMapping");review_layout->addWidget(smart);
    connect(smart,&QPushButton::clicked,this,[this]{tick();if(!reviewing)return;regenerate();if(!decode(encode(result)))return;
        smart_mapping_dialog dialog(mappings,[this]{tick();return reviewing && stop_tracking && stop_tracking();},[this]{tick();return reviewing && new_profile_path?new_profile_path():QString{};},this,result);
        if(dialog.exec()==QDialog::Accepted){applied=dialog.applied_to_current();applied_selection=dialog.selected_axes();accept();}
    });
    review_validity=new QLabel;review_validity->setObjectName("calibrationReviewValidity");review_validity->setWordWrap(true);review_layout->addWidget(review_validity);
    overwrite=new QCheckBox(tr("I approve replacing the calibrated mapping branches and their limits. Keep a rollback snapshot.")); overwrite->setObjectName("calibrationOverwrite"); review_layout->addWidget(overwrite);
    auto* save_note=new QLabel(tr("Virtual targets are game camera movement, not physical head rotation. Yaw defaults to 150° in each direction. Deadzones use measured noise; higher Shape softens small motion. Applying stops tracking so curves can be replaced safely, then saves this profile. Restart tracking when ready.")); save_note->setWordWrap(true); review_layout->addWidget(save_note);
    apply_button=new QPushButton(tr("Apply curves and stop tracking")); apply_button->setObjectName("calibrationApply"); review_layout->addWidget(apply_button);
    auto* cancel=new QPushButton(tr("Cancel")); layout->addWidget(cancel); connect(cancel,&QPushButton::clicked,this,&QDialog::reject);
    connect(record_button,&QPushButton::clicked,this,&calibration_dialog::begin);
    connect(next_button,&QPushButton::clicked,this,&calibration_dialog::advance);
    connect(comfortable,&QCheckBox::toggled,this,[this] { next_button->setEnabled(measurement.status()==state::ready && comfortable->isChecked()); });
    connect(preview_axis,&QComboBox::currentIndexChanged,this,[this] { regenerate(); });
    connect(overwrite,&QCheckBox::toggled,this,[this] { apply_button->setEnabled(preview_valid && overwrite->isChecked()); });
    connect(apply_button,&QPushButton::clicked,this,[this] {
        tick(); if (!reviewing) return;
        regenerate(); if (!preview_valid || !overwrite->isChecked()) return;
        std::vector<observation> ignored; if (input) input(false,ignored); timer.stop();
        if (!stop_tracking || !stop_tracking()) { QMessageBox::warning(this,tr("Calibration"),tr("Tracking could not be stopped. Curves have not been applied.")); return; }
        profile stored;
        if (!apply(mappings,result,stored,true)) { QMessageBox::warning(this,tr("Calibration"),tr("Curve validation failed. Curves have not been applied.")); return; }
        for(int i=0;i<6;++i)applied_selection[i]=result.axis[i].calibrated;
        applied=true;accept(); // Mapping owner explicitly saves only the reviewed axes.
    });
    connect(&timer,&QTimer::timeout,this,&calibration_dialog::tick); timer.start(50);
    show_step();
}
calibration_dialog::~calibration_dialog() { std::vector<observation> ignored; if (input) input(false,ignored); }

void calibration_dialog::show_step() {
    if (step==0) instructions->setText(tr("1. Neutral position: sit normally and look straight ahead. Record will center tracking here; remain relaxed for at least two seconds."));
    else if (step==1) instructions->setText(tr("2. Stationary noise: remain relaxed at neutral for at least four seconds. Breathe normally and avoid deliberate movement."));
    else instructions->setText(tr("%1. %2 Hold only a comfortable position, then Record for at least two seconds.").arg(step+1).arg(steps[std::size_t(step-2)].second));
    comfortable->setText(step<2?tr("I stayed relaxed at neutral without deliberate movement."):tr("This physical position felt comfortable; I did not strain.")); comfortable->setChecked(false);
    next_button->setEnabled(false); status->setText(tr("Ready to record. Tracking must be running."));
}
void calibration_dialog::begin() {
    if (step==0) {
        result={}; first_sign={}; baseline_epoch.reset(); steps={{3,tr("Turn left.")},{3,tr("Turn right.")},{4,tr("Look upward.")},{4,tr("Look downward.")}};
        if (roll->isChecked()) { steps.emplace_back(5,tr("Tilt left.")); steps.emplace_back(5,tr("Tilt right.")); }
        if (translation->isChecked()) {
            main_settings settings;
            if (*settings.reltrans_mode!=reltrans_disabled || *settings.neck_enable) { status->setText(tr("Translation calibration requires relative translation and neck displacement to be disabled in Options. Retry after changing them, or skip translations.")); return; }
            for (const auto& s:std::vector<std::pair<int,QString>>{{0,tr("Move left.")},{0,tr("Move right.")},{1,tr("Move upward.")},{1,tr("Move downward.")},{2,tr("Move forward.")},{2,tr("Move backward.")}}) steps.push_back(s);
        }
        result.use_case=use_case->currentText().toStdString();
    }
    main_settings settings;
    if (*settings.auto_center || !*settings.apply_mapping_curves || *settings.centering_mode==center_disabled) { status->setText(tr("Enable mapping and centering, and disable automatic centering in Options before calibration.")); return; }
    enhanced_settings controls;
    controls.set_raii_dtor_state(false);
    if (step>=2 && *mappings(steps[std::size_t(step-2)].first).opts.src!=steps[std::size_t(step-2)].first) { status->setText(tr("This axis must use its own tracker input in Options. Disabled or remapped axes cannot be calibrated.")); return; }
    std::vector<observation> ignored;
    if (!input || !input(true,ignored)) { status->setText(tr("Start tracking with NeuralNet 2.0 and validation enabled, then retry.")); return; }
    if (step==0) {
        if (!center_tracking || !center_tracking()) { status->setText(tr("Tracking could not be centered. Restart tracking and retry.")); return; }
        // Allow the existing centering/reset lifecycle to complete. Acquisition time,
        // not the UI poll time, excludes in-flight observations from before centering.
        accept_after=enhanced::clock::now()+std::chrono::milliseconds(500);
    } else accept_after={};
    measurement.begin(step<2?-1:steps[std::size_t(step-2)].first,result.jitter.median);
    comfortable->setChecked(false); next_button->setEnabled(false);
    roll->setEnabled(false); translation->setEnabled(false); use_case->setEnabled(false);
}
void calibration_dialog::tick() {
    if (measurement.status()==state::idle && !baseline_epoch) return;
    std::vector<observation> samples;
    if (!input || !input(true,samples)) {
        if (reviewing) return; // Completed measurements can be applied after an ordinary stop.
        step=0; baseline_epoch.reset(); measurement={}; roll->setEnabled(true); translation->setEnabled(true); use_case->setEnabled(true); show_step();
        status->setText(tr("Tracking stopped. Restart tracking and begin again from neutral.")); return;
    }
    for (const auto& sample:samples) {
        if (accept_after!=enhanced::timestamp{} && (enhanced::clock::now()<accept_after || sample.captured<accept_after)) continue;
        if (sample.valid) {
            if (baseline_epoch && sample.center_epoch!=*baseline_epoch) {
                step=0; reviewing=false; preview_valid=false; pages->setCurrentIndex(0); measurement={}; baseline_epoch.reset(); roll->setEnabled(true); translation->setEnabled(true); use_case->setEnabled(true); show_step();
                status->setText(tr("Recenter detected. Restart from neutral.")); return;
            }
            if (!baseline_epoch) baseline_epoch=sample.center_epoch;
        }
        if (!reviewing && measurement.status()!=state::idle) measurement.add(sample);
    }
    if (reviewing || measurement.status()==state::idle) return;
    const bool ready=measurement.status()==state::ready && (step!=1 || measurement.duration()>=4.);
    if (measurement.status()==state::retry) status->setText(QString::fromStdString(measurement.error()));
    else status->setText(tr("%1 valid samples, %2 seconds. %3").arg(measurement.count()).arg(measurement.duration(),0,'f',1).arg(ready?tr("Confirm comfort to continue."):tr("Keep holding gently.")));
    next_button->setEnabled(ready && comfortable->isChecked());
}
void calibration_dialog::advance() {
    if (!comfortable->isChecked() || measurement.status()!=state::ready) return;
    if (step<2) {
        const auto noise=measurement.stationary();
        if (noise.ambiguous_motion) { status->setText(tr("Movement or drift makes this noise measurement ambiguous. Return to neutral and retry; it will not be treated as sensor jitter.")); next_button->setEnabled(false); return; }
        if (step==1 && measurement.duration()<4.) return;
        result.jitter=noise;
    } else {
        const int axis=steps[std::size_t(step-2)].first;
        const auto measured=measurement.movement(true);
        if (!measured) { status->setText(tr("Endpoint was too small or not held steadily. Return to a comfortable position and retry.")); next_button->setEnabled(false); return; }
        auto& a=result.axis[axis]; auto& branch=measured->first>0?a.positive:a.negative;
        if (branch.comfort_confirmed) { status->setText(tr("This repeated the same signed direction. Move gently in the opposite requested direction and retry.")); next_button->setEnabled(false); return; }
        branch=measured->second;
        if (!first_sign[axis]) first_sign[axis]=measured->first;
        a.calibrated=a.positive.comfort_confirmed && a.negative.comfort_confirmed;
    }
    measurement={}; ++step;
    if (step>=int(steps.size())+2) review(); else show_step();
}
void calibration_dialog::review() {
    reviewing=true; // Keep monitoring the epoch so a review cannot survive recentering.
    result.date=QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
    updating=true;
    for (int i=0;i<6;++i) {
        auto& a=result.axis[i]; const bool enabled=a.calibrated; a.deadzone=std::min(result.jitter.deadzone[i],std::min(a.positive.magnitude,a.negative.magnitude)*.2);
        a.target_positive=a.target_negative=i==3?150.:i==4?90.:i==5?45.:15.;
        table->setItem(i,0,new QTableWidgetItem(QString("%1 (%2)").arg(axis_names[i],i<3?"cm":"deg")));
        const QString first[]={tr("Left"),tr("Up"),tr("Forward"),tr("Left"),tr("Up"),tr("Tilt left")};
        const QString second[]={tr("Right"),tr("Down"),tr("Backward"),tr("Right"),tr("Down"),tr("Tilt right")};
        table->setItem(i,1,new QTableWidgetItem(enabled?QString("%1 (%2)").arg(a.negative.magnitude,0,'f',2).arg(first_sign[i]<0?first[i]:second[i]):tr("Skipped")));
        table->setItem(i,2,new QTableWidgetItem(enabled?QString("%1 (%2)").arg(a.positive.magnitude,0,'f',2).arg(first_sign[i]>0?first[i]:second[i]):tr("Skipped")));
        for (int c=0;c<3;++c) table->item(i,c)->setFlags(Qt::ItemIsEnabled);
        for (int c=3;c<7;++c) {
            auto* spin=new QDoubleSpinBox; spin->setDecimals(3); spin->setEnabled(enabled);
            if (c<=4) { spin->setRange(.1,i<3?600.:180.); spin->setValue(c==3?a.target_negative:a.target_positive); }
            else if (c==5) { spin->setRange(0,enabled?std::min(a.positive.magnitude,a.negative.magnitude)*.39:0); spin->setValue(a.deadzone); spin->setSingleStep(i<3?.01:.1); }
            else { spin->setRange(1.2,3); spin->setValue(2); spin->setSingleStep(.1); }
            if (i==3 && c<=4) spin->setToolTip((c==3?first_sign[i]<0:first_sign[i]>0)?tr("Virtual camera target when turning physically left."):tr("Virtual camera target when turning physically right."));
            table->setCellWidget(i,c,spin); connect(spin,&QDoubleSpinBox::valueChanged,this,[this] { regenerate(); });
        }
    }
    const auto& j=result.jitter;
    QString text=tr("Stationary measurement: %1 valid samples, %2 Hz, reliability %3%. Capture interval P05 / median / P95: %4 / %5 / %6 ms.")
        .arg(j.valid_samples).arg(j.valid_hz,0,'f',1).arg(j.reliability*100,0,'f',1).arg(j.interval_p05_ms,0,'f',2).arg(j.interval_p50_ms,0,'f',2).arg(j.interval_p95_ms,0,'f',2);
    text+=tr("\nPer-axis median / MAD / P95 deviation / drift:");
    for (int i=0;i<6;++i) text+=QString("  %1 %2/%3/%4/%5").arg(axis_names[i]).arg(j.median[i],0,'f',2).arg(j.mad[i],0,'f',3).arg(j.p95[i],0,'f',3).arg(j.drift[i],0,'f',3);
    double rotation_noise=0,translation_noise=0; for (int i=0;i<6;++i) (i<3?translation_noise:rotation_noise)=std::max(i<3?translation_noise:rotation_noise,j.p95[i]);
    text+=tr("\nSmall motion: start with measured deadzones and Shape 2; lower Shape toward 1.2 if fine movement feels weak. Enhanced Adaptive Filter recommendation: jitter protection %1, rotation smoothing %2, translation smoothing %3. Apply these manually only if that filter is selected; these are conservative starting points, not a diagnosis of sensor noise.")
        .arg(std::clamp(std::max(rotation_noise/.1,translation_noise/.01875),0.,5.),0,'f',2).arg(std::clamp(1.+rotation_noise*.5,1.,3.),0,'f',2).arg(std::clamp(1.+translation_noise,1.,3.),0,'f',2);
    review_stats->setText(text); updating=false; pages->setCurrentIndex(1); regenerate();
}
void calibration_dialog::regenerate() {
    if (updating || !reviewing) return;
    preview_valid=true;
    enhanced_settings controls;controls.set_raii_dtor_state(false);
    for (int i=0;i<6;++i) if (result.axis[i].calibrated) {
        auto& a=result.axis[i];
        a.target_negative=static_cast<QDoubleSpinBox*>(table->cellWidget(i,3))->value(); a.target_positive=static_cast<QDoubleSpinBox*>(table->cellWidget(i,4))->value();
        a.deadzone=static_cast<QDoubleSpinBox*>(table->cellWidget(i,5))->value(); a.shape=static_cast<QDoubleSpinBox*>(table->cellWidget(i,6))->value();
        a.output_gain=current_gain(controls,i);
        spline p,n;
        if (!generate(a) || !verify_branch(p,a.positive_points) || !verify_branch(n,a.negative_points) || std::max(a.target_positive,a.target_negative)/a.output_gain>(i<3?600.:180.)) preview_valid=false;
    }
    const int i=preview_axis->currentIndex(); preview->selected=i; preview->axis=result.axis[i]; preview->valid=preview_valid && result.axis[i].calibrated;
    if (preview->valid) { preview->positive.set_points(qt_points(result.axis[i].positive_points)); preview->negative.set_points(qt_points(result.axis[i].negative_points)); }
    preview->update(); apply_button->setEnabled(preview_valid && overwrite->isChecked());
    review_validity->setText(preview_valid?tr("Targets include current Amplification once. Neutral Amplification is 42% (1×); Speed 55% preserves the existing filter response."):
        tr("A generated target cannot pass the native curve/output limits at the current Amplification. Lower its virtual target or open Smart Mapping to inspect and edit the draft. Amplification and Speed are preserved."));
}
