#include "calibration-dialog.hpp"
#include "mapping-comparison.hpp"
#include "smart-mapping-dialog.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>
using namespace enhanced::calibration;
static const char* axis_names[]={"X","Y","Z","Yaw","Pitch","Roll"};
class calibration_page final : public QWizardPage {
public:
    std::function<bool()> ready=[] {return true;};
    bool isComplete() const override {return ready();}
    void refresh() {emit completeChanged();}
};
static QLabel* description(QVBoxLayout* layout,const QString& text) {
    auto* label=new QLabel(text);label->setWordWrap(true);label->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(label);return label;
}
calibration_dialog::calibration_dialog(Mappings& m,calibration_input in,std::function<bool()> stop,std::function<bool()> recenter,QWidget* parent)
    :QWizard(parent),mappings(m),input(std::move(in)),stop_tracking(std::move(stop)),center_tracking(std::move(recenter)) {
    setObjectName("automaticCalibrationWizard");setWindowTitle(tr("OpenTrack Enhanced — Head Movement Calibration"));setWizardStyle(QWizard::ModernStyle);
    setOption(NoBackButtonOnStartPage);setOption(NoBackButtonOnLastPage);
    setButtonText(NextButton,tr("&Next"));setButtonText(BackButton,tr("&Back"));setButtonText(FinishButton,tr("&Finish"));setButtonText(CancelButton,tr("&Cancel"));
    button(NextButton)->setObjectName("calibrationNext");button(BackButton)->setObjectName("calibrationBack");button(FinishButton)->setObjectName("calibrationFinish");button(CommitButton)->setObjectName("calibrationApply");
    button(BackButton)->setToolTip(tr("Returning to a recording step requires a fresh recording and clears dependent measurements."));
    const auto geometry=screen()->availableGeometry();resize(std::min(860,int(geometry.width()*.92)),std::min(740,int(geometry.height()*.90)));setMinimumSize(540,360);
    // Scroll page contents at high DPI; native navigation remains visible.
    const auto add=[&](int id,const QString& title,const QString& subtitle) {
        auto* p=new calibration_page;p->setObjectName(QString("calibrationPage%1").arg(id));p->setTitle(title);p->setSubTitle(subtitle);
        auto* outer=new QVBoxLayout(p);auto* scroll=new QScrollArea;scroll->setFrameShape(QFrame::NoFrame);scroll->setWidgetResizable(true);
        auto* content=new QWidget;auto* layout=new QVBoxLayout(content);layout->setContentsMargins(8,8,8,8);layout->setSpacing(12);scroll->setWidget(content);outer->addWidget(scroll);setPage(id,p);return layout;
    };
    auto* layout=add(welcome,tr("Welcome and camera readiness"),tr("Comfortable movements create a personal mapping. Your profile stays unchanged until Apply."));
    description(layout,tr("Start NeuralNet tracking with NeuralNet 2.0 and pose validation enabled. Keep your face visible and the camera steady. For PS3 Eye, select a supported 640 × 480 at 75 FPS mode in camera settings."));
    description(layout,tr("Never force your neck. Move gently to each position before pressing Record, then hold briefly. Skip optional movements that feel uncomfortable."));
    readiness=description(layout,{});readiness->setObjectName("calibrationReadiness");layout->addStretch();static_cast<calibration_page*>(page(welcome))->ready=[this]{return ready_camera;};
    const std::array<QString,14> titles{tr("Center your head"),tr("Stationary noise test"),tr("Look comfortably left"),tr("Look comfortably right"),tr("Look up"),tr("Look down"),tr("Tilt comfortably left"),tr("Tilt comfortably right"),tr("Move comfortably left"),tr("Move comfortably right"),tr("Move comfortably up"),tr("Move comfortably down"),tr("Move comfortably forward"),tr("Move comfortably backward")};
    const std::array<int,14> ids{center_head,noise,left,right,up,down,roll_left,roll_right,x_left,x_right,y_up,y_down,z_forward,z_backward};
    for(std::size_t k=0;k<ids.size();++k) {
        const int id=ids[k];auto* l=add(id,titles[k],id==center_head?tr("Sit normally and look straight ahead. Record uses OpenTrack's existing Center operation."):tr("Move first, then Record. Hold only a relaxed and comfortable position."));
        description(l,id==noise?tr("Stay relaxed at neutral for at least four seconds. Breathe normally. Deliberate movement or drift can require a retry."):id==center_head?tr("Allow centering to settle, then hold neutral for two seconds."):tr("Hold the comfortable endpoint for at least two seconds. Comfort matters more than a large range."));
        recording_controls c;c.status=description(l,tr("Ready. Press Record when positioned."));c.status->setObjectName("calibrationStatus");
        c.progress=new QProgressBar;c.progress->setRange(0,100);c.progress->setAccessibleName(tr("Recording progress"));l->addWidget(c.progress);
        c.comfort=new QCheckBox(id<=noise?tr("I stayed &comfortable and relaxed."):tr("This position felt &comfortable; I did not strain."));c.comfort->setObjectName("calibrationComfort");l->addWidget(c.comfort);
        c.record=new QPushButton(tr("&Record / Retry"));c.record->setObjectName("calibrationRecord");c.record->setAutoDefault(false);c.record->setAccessibleDescription(tr("Record the current comfortable position. Alt+R."));l->addWidget(c.record);
        connect(c.record,&QPushButton::clicked,this,[this]{begin_recording();});connect(c.comfort,&QCheckBox::toggled,this,[this]{if(currentPage())static_cast<calibration_page*>(currentPage())->refresh();});
        if(id>=roll_left){auto* skip=new QPushButton(tr("&Skip this optional axis"));skip->setObjectName("calibrationSkip");skip->setAutoDefault(false);l->addWidget(skip);connect(skip,&QPushButton::clicked,this,&calibration_dialog::skip_optional);}
        l->addStretch();recordings[id]=c;static_cast<calibration_page*>(page(id))->ready=[this,id]{return currentId()==id && !recording && !waiting_retry && measurement.status()==state::ready && recordings.at(id).comfort->isChecked();};
    }
    layout=add(optional,tr("Optional roll and translation"),tr("Yaw and pitch are complete. Add other axes only if you use them in your game."));
    roll=new QCheckBox(tr("Include &roll (two gentle tilts)"));roll->setObjectName("calibrationRoll");layout->addWidget(roll);
    translation=new QCheckBox(tr("Include &X/Y/Z translation (six small movements)"));translation->setObjectName("calibrationTranslations");layout->addWidget(translation);
    optional_status=description(layout,tr("Translations require relative translation and neck compensation to be disabled. Each selected axis must use its own tracker source. Settings are never changed automatically."));
    auto* skip=new QPushButton(tr("&Skip optional axes"));skip->setObjectName("calibrationSkipOptional");skip->setAutoDefault(false);layout->addWidget(skip);connect(skip,&QPushButton::clicked,this,[this]{roll->setChecked(false);translation->setChecked(false);next();});layout->addStretch();
    layout=add(game_profile,tr("Choose a game profile"),tr("Editable virtual camera targets; your measured physical ranges remain personal."));
    game=new QComboBox;game->setObjectName("calibrationGame");game->addItem(tr("Personal targets (yaw 150°, pitch 90°)"));for(const auto& t:racing_templates)game->addItem(t.name);layout->addWidget(game);
    description(layout,tr("LMU GT3/Hypercar and Assetto Corsa GT/Road/Drift templates are preferences without assumptions about game FOV or monitor placement. Edit targets on Review."));layout->addStretch();connect(game,&QComboBox::currentIndexChanged,this,[this]{choose_template();});
    layout=add(compare,tr("Compare generated curves"),tr("The same measured ranges and requested targets, with three response geometries."));
    auto* row=new QHBoxLayout;preview_axis=new QComboBox;preview_axis->setObjectName("calibrationPreviewAxis");style=new QComboBox;style->setObjectName("calibrationStyle");for(auto s:{response_style::natural,response_style::balanced,response_style::responsive})style->addItem(style_name(s),int(s));
    auto* axis_label=new QLabel(tr("&Axis"));axis_label->setBuddy(preview_axis);auto* style_label=new QLabel(tr("&Style"));style_label->setBuddy(style);row->addWidget(axis_label);row->addWidget(preview_axis,1);row->addWidget(style_label);row->addWidget(style,1);layout->addLayout(row);
    comparison=new smart_comparison(this);comparison->setObjectName("calibrationComparison");layout->addWidget(comparison,1);
    description(layout,tr("Natural is near linear with a soft center. Balanced lowers center sensitivity. Responsive turns earlier. None is best for everyone. These modes change geometry, never add a temporal filter."));
    connect(preview_axis,&QComboBox::currentIndexChanged,this,[this]{if(updating)return;updating=true;style->setCurrentIndex(std::max(0,int(data.axis[preview_axis->currentData().toInt()].style)-1));updating=false;regenerate();});
    connect(style,&QComboBox::currentIndexChanged,this,[this]{if(updating)return;data.axis[preview_axis->currentData().toInt()].style=response_style(style->currentData().toInt());regenerate();});
    layout=add(review,tr("Review and apply"),tr("Check targets and axes. Apply stops tracking, saves selected mappings, and retains rollback snapshots."));page(review)->setCommitPage(true);setButtonText(CommitButton,tr("&Apply and continue"));
    review_stats=description(layout,{});table=new QTableWidget(6,7);table->setObjectName("calibrationReviewTable");table->setHorizontalHeaderLabels({tr("Apply axis"),tr("Physical −"),tr("Physical +"),tr("Virtual −"),tr("Virtual +"),tr("Deadzone"),tr("Shape")});table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);table->verticalHeader()->hide();table->setMinimumSize(600,235);layout->addWidget(table);
    review_validity=description(layout,{});review_validity->setObjectName("calibrationReviewValidity");
    auto* smart=new QPushButton(tr("&Smart Mapping: edit points, sensitivity or save as…"));smart->setObjectName("calibrationSmartMapping");smart->setAutoDefault(false);layout->addWidget(smart);
    connect(smart,&QPushButton::clicked,this,[this]{tick();if(currentId()!=review)return;regenerate();if(!decode(encode(data)))return;
        smart_mapping_dialog dialog(mappings,[this]{tick();return currentId()==review && stop_tracking && stop_tracking();},[this]{tick();return currentId()==review && new_profile_path?new_profile_path():QString{};},this,data);
        profile stored;const auto old_undo=*stored.rollback,old_original=*stored.original;
        if(dialog.exec()!=QDialog::Accepted)return;selection=dialog.selected_axes();applied=dialog.applied_to_current();
        if(applied){enhanced_settings c;c.set_raii_dtor_state(false);saved=save_applied?save_applied(selection):save_selected_mapping(mappings,stored,selection,c);if(!saved){restore(mappings,stored,true);stored.rollback=old_undo;stored.original=old_original;applied=false;review_validity->setText(tr("The profile could not be saved. Mappings were restored. Retry Apply."));return;}}
        skip_to=completion;next();
    });
    overwrite=new QCheckBox(tr("I &approve saving the checked axes and limits, with previous and original snapshots."));overwrite->setObjectName("calibrationOverwrite");layout->addWidget(overwrite);connect(overwrite,&QCheckBox::toggled,this,[this]{if(currentId()==review)static_cast<calibration_page*>(currentPage())->refresh();});
    static_cast<calibration_page*>(page(review))->ready=[this]{return saved || skip_to==completion || (preview_valid && overwrite->isChecked());};
    layout=add(completion,tr("Calibration complete"),tr("Your profile is ready for a careful game test."));finished=description(layout,{});finished->setObjectName("calibrationFinished");
    description(layout,tr("Restart tracking. Center while seated normally. Test small glances, comfortable limits and tracking loss before driving. Mapping → Undo calibration / previous mapping restores the prior curves; a backed-up INI is another fallback."));layout->addStretch();
    live_banner=new QWidget(this);auto* banner_layout=new QVBoxLayout(live_banner);banner_layout->setContentsMargins(8,0,8,0);auto* progress_row=new QHBoxLayout;stage=new QLabel;stage->setObjectName("calibrationStage");overall=new QProgressBar;overall->setRange(0,12);overall->setFormat(tr("Step %v of 12"));overall->setAccessibleName(tr("Wizard progress"));progress_row->addWidget(stage);progress_row->addWidget(overall,1);
    live_pose=new QLabel(tr("Physical pose: waiting for validated tracking"));live_pose->setWordWrap(true);live_pose->setObjectName("calibrationLivePose");live_pose->setAccessibleDescription(tr("Validated physical angles in degrees and translations in centimeters, before filter, prediction and mapping."));
    banner_layout->addLayout(progress_row);banner_layout->addWidget(live_pose);connect(this,&QWizard::currentIdChanged,this,&calibration_dialog::enter_page);connect(&timer,&QTimer::timeout,this,&calibration_dialog::tick);timer.start(50);setStartId(welcome);
}
calibration_dialog::~calibration_dialog(){std::vector<observation> ignored;if(input)input(false,ignored);}
int calibration_dialog::recording_axis(int id){if(id<=noise)return -1;if(id<=right)return 3;if(id<=down)return 4;if(id<=roll_right)return 5;if(id<=x_right)return 0;if(id<=y_down)return 1;return 2;}
int calibration_dialog::stage_number(int id){if(id<=down)return id+1;if(id<=z_backward)return 8;return id-game_profile+9;}
int calibration_dialog::nextId() const {if(skip_to>=0)return skip_to;switch(currentId()){case optional:return roll->isChecked()?roll_left:translation->isChecked()?x_left:game_profile;case roll_right:return translation->isChecked()?x_left:game_profile;case z_backward:return game_profile;case completion:return -1;default:return currentId()+1;}}
void calibration_dialog::initializePage(int id){QWizard::initializePage(id);}
void calibration_dialog::enter_page(int id){if(id<0)return;live_banner->setParent(page(id));static_cast<QVBoxLayout*>(page(id)->layout())->insertWidget(0,live_banner);live_banner->show();skip_to=-1;recording=waiting_retry=false;retry_since={};measurement={};accept_after={};stage->setText(tr("Calibration progress"));overall->setValue(stage_number(id));button(CancelButton)->setEnabled(!applied && id!=completion);
    if(id==welcome){data={};checkpoints.clear();baseline_epoch.reset();ready_camera=false;readiness->setText(reset_message.isEmpty()?tr("Checking validated tracking…"):reset_message);}
    if(id==center_head)reset_message.clear();
    if(recordings.contains(id)){if(auto it=checkpoints.find(id);it!=checkpoints.end()){data=it->second;for(auto end=checkpoints.lower_bound(id);end!=checkpoints.end();)end=checkpoints.erase(end);}checkpoints[id]=data;auto& c=recordings.at(id);c.comfort->setChecked(false);c.progress->setValue(0);c.status->setText(tr("Move gently into position, then Record. Back requires a fresh recording."));c.record->setFocus();}
    if(id==game_profile)choose_template();
    if(id==compare){updating=true;preview_axis->clear();for(int i=0;i<6;++i)if(data.axis[i].calibrated)preview_axis->addItem(axis_names[i],i);style->setCurrentIndex(std::max(0,int(data.axis[preview_axis->currentData().toInt()].style)-1));updating=false;regenerate();}
    if(id==review){build_review();regenerate();}
    if(id==completion){timer.stop();std::vector<observation> ignored;if(input)input(false,ignored);finished->setText(applied?tr("The selected mappings are saved to the current profile. Tracking is stopped. Finish closes this wizard."):tr("A new INI profile was saved separately. Finish, stop tracking, then select it from OpenTrack's profile list."));}
    }
void calibration_dialog::begin_recording(bool retry){const int id=currentId();if(!recordings.contains(id))return;auto& c=recordings.at(id);main_settings s;
    if(*s.auto_center || !*s.apply_mapping_curves || *s.centering_mode==center_disabled){c.status->setText(tr("Enable mapping and centering; disable automatic centering in Options. Cancel, adjust settings and reopen calibration."));return;}
    const int axis=recording_axis(id);if(axis>=0 && *mappings(axis).opts.src!=axis){c.status->setText(tr("This axis must use its own tracker input in Options."));return;}
    if(axis>=0 && axis<3 && (*s.reltrans_mode!=reltrans_disabled || *s.neck_enable)){c.status->setText(tr("Disable relative translation and neck compensation, or Skip this optional axis."));return;}
    if(!ready_camera && !retry){c.status->setText(tr("Validated tracking is unavailable. Wait for recovery, then Retry."));return;}
    if(id==center_head && !retry){baseline_epoch.reset();if(!center_tracking || !center_tracking()){c.status->setText(tr("Tracking could not be centered. Restart tracking and Retry."));return;}accept_after=enhanced::clock::now()+std::chrono::milliseconds(500);}else accept_after={};
    measurement.begin(axis,data.jitter.median);recording=true;waiting_retry=false;retry_since={};c.comfort->setChecked(false);c.progress->setValue(0);c.status->setText(tr("Recording. Hold gently; no curves are changing."));static_cast<calibration_page*>(currentPage())->refresh();
}
void calibration_dialog::tick(){if(currentId()<0 || currentId()==completion)return;std::vector<observation> samples;const bool running=input && input(true,samples);
    for(const auto& sample:samples){const bool quality=sample.valid && !enhanced::stale(sample.captured,sample.received,250.) && enhanced::finite(sample.physical) && (!sample.confidence || (std::isfinite(*sample.confidence) && *sample.confidence>=.65 && *sample.confidence<=1)) && (!sample.reliability || (std::isfinite(*sample.reliability) && *sample.reliability>=.65 && *sample.reliability<=1));
        if(quality){last_good=enhanced::clock::now();ready_camera=true;live_pose->setText(tr("Physical yaw %1°  pitch %2°  roll %3°\nX %4 cm  Y %5 cm  Z %6 cm").arg(sample.physical[3],0,'f',1).arg(sample.physical[4],0,'f',1).arg(sample.physical[5],0,'f',1).arg(sample.physical[0],0,'f',1).arg(sample.physical[1],0,'f',1).arg(sample.physical[2],0,'f',1));}else ready_camera=false;
        if(accept_after!=enhanced::timestamp{} && (enhanced::clock::now()<accept_after || sample.captured<accept_after))continue;
        if(baseline_epoch && sample.center_epoch!=0 && sample.center_epoch!=*baseline_epoch){reset_message=tr("Recenter or a new tracking session detected. Earlier measurements were cleared; begin from Center again.");restart();return;}
        if(quality && currentId()!=welcome && !baseline_epoch)baseline_epoch=sample.center_epoch;
        if(waiting_retry){if(!quality){retry_since={};continue;}if(retry_since==enhanced::timestamp{})retry_since=sample.captured;if(enhanced::milliseconds(retry_since,sample.captured)>=500.){begin_recording(true);measurement.add(sample);}continue;}
        if(recording)measurement.add(sample);
    }
    if(!running || (last_good!=enhanced::timestamp{} && enhanced::milliseconds(last_good,enhanced::clock::now())>250.))ready_camera=false;
    if(!running){live_pose->setText(tr("Physical pose unavailable — tracking stopped."));if(recording)measurement.add({});}
    else if(!ready_camera)live_pose->setText(tr("Tracking lost or unreliable — waiting for validated physical pose."));
    if(currentId()==welcome){const QString status=ready_camera?tr("Ready: validated physical tracking is available. Next will guide you through centering."):running?tr("Waiting for a reliable face pose. Check NeuralNet 2.0, validation, face visibility and lighting."):tr("Tracking is stopped. Cancel, start NeuralNet 2.0 with validation enabled, then reopen this wizard.");readiness->setText(reset_message.isEmpty()?status:reset_message+"\n"+status);static_cast<calibration_page*>(currentPage())->refresh();}
    if(recordings.contains(currentId()))update_recording();
}
void calibration_dialog::update_recording(){auto& c=recordings.at(currentId());if(recording && measurement.status()==state::retry){recording=false;waiting_retry=true;retry_since={};c.comfort->setChecked(false);c.status->setText(tr("%1 Automatic retry follows 0.5 seconds of reliable tracking. Hold the requested position gently, or press Retry.").arg(QString::fromStdString(measurement.error())));}
    if(recording){const double required=currentId()==noise?4.:2.;const bool ready=measurement.status()==state::ready && measurement.duration()>=required;c.progress->setValue(int(100*std::min({1.,measurement.duration()/required,measurement.count()/60.})));c.status->setText(tr("%1 samples • %2 / %3 seconds. %4").arg(measurement.count()).arg(measurement.duration(),0,'f',1).arg(required,0,'f',0).arg(ready?tr("Complete. Confirm comfort, then Next."):tr("Keep holding gently.")));if(ready)recording=false;}static_cast<calibration_page*>(currentPage())->refresh();}
bool calibration_dialog::validateCurrentPage(){const int id=currentId();if(skip_to>=0)return true;if(recordings.contains(id)){auto& c=recordings.at(id);if(recording || waiting_retry || measurement.status()!=state::ready || !c.comfort->isChecked())return false;
        if(id<=noise){const auto j=measurement.stationary();if(j.ambiguous_motion){c.status->setText(tr("Movement or drift makes this ambiguous. Return to neutral and Retry."));return false;}data.jitter=j;}
        else {const int i=recording_axis(id);const auto r=measurement.movement(true);if(!r){c.status->setText(tr("Endpoint too small or unsteady. Retry a comfortable hold."));return false;}auto& a=data.axis[i];auto& branch=r->first>0?a.positive:a.negative;if(branch.comfort_confirmed){c.status->setText(tr("This repeated the same signed direction. Move in the opposite requested direction and Retry."));return false;}branch=r->second;a.calibrated=a.positive.comfort_confirmed && a.negative.comfort_confirmed;}}
    if(id==optional && translation->isChecked()){main_settings s;if(*s.reltrans_mode!=reltrans_disabled || *s.neck_enable){optional_status->setText(tr("Relative translation or neck compensation is enabled. Skip translations, or Cancel and change Options before measuring them."));return false;}}
    if(id==review)return apply_result();return true;}
void calibration_dialog::skip_optional(){const int id=currentId();if(id<roll_left || id>z_backward)return;data.axis[recording_axis(id)]={};recording=waiting_retry=false;skip_to=id<=roll_right?(translation->isChecked()?x_left:game_profile):id<=x_right?y_up:id<=y_down?z_forward:game_profile;next();}
void calibration_dialog::choose_template(){const int index=game->currentIndex();data.use_case=index?racing_templates[index-1].name:"Personal targets";for(int i=0;i<6;++i)if(data.axis[i].calibrated){auto& a=data.axis[i];a.deadzone=std::min(data.jitter.deadzone[i],std::min(a.positive.magnitude,a.negative.magnitude)*.2);a.shape=2;a.style=index?racing_templates[index-1].style:response_style::balanced;a.target_positive=a.target_negative=i==3?(index?racing_templates[index-1].yaw:150.):i==4?(index?racing_templates[index-1].pitch:90.):i==5?45.:15.;}regenerate();}
void calibration_dialog::build_review(){updating=true;for(int i=0;i<6;++i){auto& a=data.axis[i];selected[i]=new QCheckBox(QString("%1 (%2)").arg(axis_names[i],i<3?"cm":"deg"));selected[i]->setObjectName(QString("calibrationSelect%1").arg(i));selected[i]->setChecked(a.calibrated);selected[i]->setEnabled(a.calibrated);table->setCellWidget(i,0,selected[i]);connect(selected[i],&QCheckBox::toggled,this,[this]{regenerate();});
        for(int c:{1,2}){table->setItem(i,c,new QTableWidgetItem(a.calibrated?QString::number(c==1?a.negative.magnitude:a.positive.magnitude,'f',2):tr("Skipped")));table->item(i,c)->setFlags(Qt::ItemIsEnabled);}
        for(int c=3;c<7;++c){auto* spin=new QDoubleSpinBox;spin->setDecimals(3);spin->setEnabled(a.calibrated);spin->setAccessibleName(QString("%1 %2").arg(axis_names[i],table->horizontalHeaderItem(c)->text()));if(c<=4){spin->setRange(.1,i<3?600.:180.);spin->setValue(c==3?a.target_negative:a.target_positive);}else if(c==5){spin->setRange(0,a.calibrated?std::min(a.positive.magnitude,a.negative.magnitude)*.39:0);spin->setValue(a.deadzone);spin->setSingleStep(i<3?.01:.1);}else{spin->setRange(1.2,3);spin->setValue(a.shape);spin->setSingleStep(.1);}table->setCellWidget(i,c,spin);connect(spin,&QDoubleSpinBox::valueChanged,this,[this]{regenerate();});}}
    updating=false;overwrite->setChecked(false);review_stats->setText(tr("Stationary: %1 samples, %2 valid Hz, reliability %3%. Capture intervals P05 / P50 / P95: %4 / %5 / %6 ms. Targets include Amplification once; Speed controls timing separately.").arg(data.jitter.valid_samples).arg(data.jitter.valid_hz,0,'f',1).arg(data.jitter.reliability*100,0,'f',1).arg(data.jitter.interval_p05_ms,0,'f',2).arg(data.jitter.interval_p50_ms,0,'f',2).arg(data.jitter.interval_p95_ms,0,'f',2));}
void calibration_dialog::regenerate(){if(updating)return;enhanced_settings c;c.set_raii_dtor_state(false);preview_valid=true;bool any=false;for(int i=0;i<6;++i)if(data.axis[i].calibrated){auto& a=data.axis[i];if(currentId()==review && table->cellWidget(i,3)){a.target_negative=static_cast<QDoubleSpinBox*>(table->cellWidget(i,3))->value();a.target_positive=static_cast<QDoubleSpinBox*>(table->cellWidget(i,4))->value();a.deadzone=static_cast<QDoubleSpinBox*>(table->cellWidget(i,5))->value();a.shape=static_cast<QDoubleSpinBox*>(table->cellWidget(i,6))->value();}a.output_gain=current_gain(c,i);spline p,n;const bool valid=generate(a) && verify_branch(p,a.positive_points) && verify_branch(n,a.negative_points) && std::max(a.target_positive,a.target_negative)/a.output_gain<=(i<3?600.:180.);if(currentId()!=review || (selected[i] && selected[i]->isChecked())){any=true;preview_valid &= valid;}}
    preview_valid &= any;if(currentId()==compare){const int i=preview_axis->currentData().toInt();comparison->set_axis(data.axis[i],i<3?600.:180.,i<3?"cm":"degrees");}
    if(currentId()==review){review_validity->setText(preview_valid?tr("Native spline checks passed. Amplification 42% is 1×; Speed 55% keeps existing filter response. Confirm targets and comfort."):tr("A selected target cannot pass the native output limit at this Amplification, or no axis is selected. Lower targets, deselect an axis or use Smart Mapping."));static_cast<calibration_page*>(currentPage())->refresh();}}
bool calibration_dialog::apply_result(){if(saved)return true;regenerate();if(!preview_valid || !overwrite->isChecked())return false;tick();if(currentId()!=review)return false;if(!stop_tracking || !stop_tracking()){review_validity->setText(tr("Tracking could not be stopped. Mappings were not applied."));return false;}
    profile stored;const auto old_undo=*stored.rollback,old_original=*stored.original;for(int i=0;i<6;++i)selection[i]=selected[i]->isChecked() && data.axis[i].calibrated;data.date=QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();if(!applied && !apply(mappings,data,stored,true,selection)){review_validity->setText(tr("Curve validation failed. Mappings were not changed."));return false;}
    enhanced_settings c;c.set_raii_dtor_state(false);saved=save_applied?save_applied(selection):save_selected_mapping(mappings,stored,selection,c);if(!saved){restore(mappings,stored,true);stored.rollback=old_undo;stored.original=old_original;applied=false;review_validity->setText(tr("The profile could not be saved. Mappings were restored. Retry Apply."));return false;}applied=true;return true;}
void calibration_dialog::reject(){if(saved || currentId()==completion)accept();else QWizard::reject();}
void calibration_dialog::done(int code){timer.stop();std::vector<observation> ignored;if(input)input(false,ignored);QWizard::done(code);}
