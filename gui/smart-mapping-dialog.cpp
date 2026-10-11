#include "smart-mapping-dialog.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

using namespace enhanced::calibration;
static const char* names[]={"X","Y","Z","Yaw","Pitch","Roll"};
class smart_comparison final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(smart_comparison)
public:
    std::array<spline,3> positive,negative;
    axis_result data;
    bool valid=false;
    double limit=180;
    QString units="degrees";
    explicit smart_comparison(QWidget* parent) : QWidget(parent) {setMinimumHeight(185);for(auto* a:{&positive,&negative})for(auto& s:*a)private_spline(s);}
    void set_axis(const axis_result& a,double output_limit,const QString& unit) {
        data=a;limit=output_limit;units=unit;valid=a.calibrated;
        for(int i=0;i<3 && valid;++i) {
            auto variant=a;variant.style=response_style(i+1);
            valid=generate(variant);
            if(valid) {positive[i].set_points(qt_points(variant.positive_points));negative[i].set_points(qt_points(variant.negative_points));}
        }
        update();
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.fillRect(rect(),palette().base());p.setRenderHint(QPainter::Antialiasing);
        const QRectF box(56,28,width()-73,height()-75);p.setPen(palette().text().color());p.drawRect(box);
        if(!valid)return;
        const double xmin=-data.negative.magnitude*1.05,xmax=data.positive.magnitude*1.05;
        const double ymax=std::max(data.target_positive,data.target_negative)*1.08;
        const auto screen=[&](double x,double y){return QPointF(box.left()+(x-xmin)/(xmax-xmin)*box.width(),box.center().y()-y/ymax*box.height()/2);};
        p.setPen(palette().mid().color());p.drawLine(screen(xmin,0),screen(xmax,0));p.drawLine(screen(0,-ymax),screen(0,ymax));
        const QColor colors[]={QColor(30,120,70),QColor(40,100,210),QColor(190,65,155)};
        for(int i=0;i<3;++i){QPainterPath path;for(int k=0;k<=1024;++k){const double x=xmin+(xmax-xmin)*k/1024;
            const double y=enhanced::adjusted_mapping_output((x<0?negative[i]:positive[i]).get_value_no_save(x),data.output_gain,limit);
            if(!k)path.moveTo(screen(x,y));else path.lineTo(screen(x,y));}
            p.setPen(QPen(colors[i],2));p.drawPath(path);
            p.drawText(55+i*190,19,QString::fromLatin1(style_name(response_style(i+1))));}
        p.setPen(palette().text().color());
        for(double x:{-data.negative.magnitude,0.,data.positive.magnitude})p.drawText(QRectF(screen(x,0).x()-35,box.bottom()+3,70,18),Qt::AlignCenter,QString::number(x,'f',1));
        for(double y:{-data.target_negative,0.,data.target_positive})p.drawText(QRectF(0,screen(0,y).y()-9,52,18),Qt::AlignRight,QString::number(y,'f',1));
        p.drawText(QRectF(0,height()-25,width(),24),Qt::AlignCenter,tr("Physical input (%1) → effective mapped target (%1); identical ranges, targets and gain").arg(units));
    }
};

smart_mapping_dialog::smart_mapping_dialog(Mappings& m,std::function<bool()> stop,std::function<QString()> path,QWidget* parent,std::optional<record> measurements)
    :QDialog(parent),mappings(m),stop_tracking(std::move(stop)),new_profile_path(std::move(path)) {
    controls.set_raii_dtor_state(false);
    draft=measurements.value_or(decode(*stored.data).value_or(record{}));
    setWindowTitle(tr("Smart Mapping and Racing Profiles"));setObjectName("smartMappingDialog");resize(950,880);
    auto* layout=new QVBoxLayout(this);
    auto* note=new QLabel(tr("Preview edits stay in this draft. Physical ranges come from your calibration. Templates suggest editable camera targets without assumptions about game FOV or monitor placement."));note->setWordWrap(true);layout->addWidget(note);
    auto* selectors=new QHBoxLayout;game=new QComboBox;game->setObjectName("smartTemplate");game->addItem(tr("Personal calibration / custom targets"));
    for(const auto& t:racing_templates)game->addItem(QString::fromLatin1(t.name));
    axis=new QComboBox;axis->setObjectName("smartAxis");for(int i=0;i<6;++i)axis->addItem(names[i]);axis->setCurrentIndex(3);
    selectors->addWidget(game,1);selectors->addWidget(axis);layout->addLayout(selectors);
    template_reason=new QLabel;template_reason->setWordWrap(true);layout->addWidget(template_reason);
    auto* axes=new QHBoxLayout;axes->addWidget(new QLabel(tr("Apply axes:")));
    for(int i=0;i<6;++i){selected[i]=new QCheckBox(names[i]);selected[i]->setObjectName(QString("smartSelect%1").arg(i));selected[i]->setEnabled(draft.axis[i].calibrated);selected[i]->setChecked(draft.axis[i].calibrated);axes->addWidget(selected[i]);connect(selected[i],&QCheckBox::toggled,this,[this]{refresh_preview();});
        for(auto* s:{&positive[i],&negative[i],&original_positive[i],&original_negative[i]})private_spline(*s);
        original_positive[i].set_points(m(i).spline_main.get_points());original_negative[i].set_points(*m(i).opts.altp?m(i).spline_alt.get_points():m(i).spline_main.get_points());
        auto& a=draft.axis[i];if(a.calibrated){const double gain=current_gain(controls,i);
            if(a.manual_points){const double ratio=a.output_gain/gain;for(auto* points:{&a.positive_points,&a.negative_points})for(auto& pt:*points)pt.output*=ratio;a.output_gain=gain;}
            else {a.output_gain=gain;generate(a);} positive[i].set_points(qt_points(a.positive_points));negative[i].set_points(qt_points(a.negative_points));}
        previous[i]=a;
        for(auto* s:{&positive[i],&negative[i]})connect(s->get_settings().get(),&spline_detail::base_settings::recomputed,this,[this,i]{point_edited(i);});
    }layout->addLayout(axes);
    auto* fields=new QHBoxLayout;auto* left=new QFormLayout;auto* right=new QFormLayout;fields->addLayout(left,1);fields->addLayout(right,1);layout->addLayout(fields);
    style=new QComboBox;style->setObjectName("smartStyle");for(auto s:{response_style::legacy,response_style::natural,response_style::balanced,response_style::responsive})style->addItem(QString::fromLatin1(style_name(s)),int(s));left->addRow(tr("Geometry style"),style);
    const auto spin=[&](const char* name,double min,double max,double step){auto* s=new QDoubleSpinBox;s->setObjectName(name);s->setDecimals(3);s->setRange(min,max);s->setSingleStep(step);return s;};
    target_positive=spin("smartTargetPositive",.1,600,1);target_negative=spin("smartTargetNegative",.1,600,1);
    sensitivity_positive=spin("smartSensitivityPositive",.5,2,.05);sensitivity_negative=spin("smartSensitivityNegative",.5,2,.05);
    shape=spin("smartShape",1.2,3,.1);deadzone=spin("smartDeadzone",0,100,.01);probe=spin("smartPhysicalProbe",-180,180,1);
    left->addRow(tr("Virtual target +"),target_positive);left->addRow(tr("Virtual target −"),target_negative);left->addRow(tr("Early sensitivity +"),sensitivity_positive);
    right->addRow(tr("Early sensitivity −"),sensitivity_negative);right->addRow(tr("Response shape"),shape);right->addRow(tr("Physical deadzone"),deadzone);right->addRow(tr("Physical input probe"),probe);
    style->setToolTip(tr("Natural: near linear with a soft center. Balanced: softer center with increasing response. Responsive: stronger early response. Geometry only; compare personal preferences below."));
    auto* tabs=new QTabWidget;layout->addWidget(tabs,1);
    auto* edit_page=new QWidget;auto* edits=new QVBoxLayout(edit_page);
    auto* legend=new QLabel(tr("Grey dashed: original mapping. Blue dashed + points: draft curve. Orange: draft × Amplification, capped at output limit. Shaded: deadzone. Green: comfortable physical limit. Red: output saturation. Left click/drag edits points; right click removes."));legend->setWordWrap(true);edits->addWidget(legend);
    positive_editor=new spline_widget;negative_editor=new spline_widget;positive_editor->setObjectName("smartPositiveEditor");negative_editor->setObjectName("smartNegativeEditor");
    for(auto* w:{positive_editor,negative_editor}){w->setMinimumSize(600,165);w->setColorBezier(QColor(40,100,210));w->set_snap(0,0);w->set_y_step(30);w->set_x_step(5);}
    edits->addWidget(new QLabel(tr("Positive physical branch")));edits->addWidget(positive_editor,1);edits->addWidget(new QLabel(tr("Negative physical branch (magnitudes)")));edits->addWidget(negative_editor,1);tabs->addTab(edit_page,tr("Edit generated points"));
    auto* compare_page=new QWidget;auto* compare_layout=new QVBoxLayout(compare_page);comparison=new smart_comparison(this);compare_layout->addWidget(comparison,1);
    auto* compare_note=new QLabel(tr("All three modes use the same measured ranges and explicit targets. Natural is conservative and nearly linear; Balanced lowers center sensitivity; Responsive increases early turning. None is objectively best for every user. Speed does not change these lines."));compare_note->setWordWrap(true);compare_layout->addWidget(compare_note);tabs->addTab(compare_page,tr("Compare all three styles"));
    auto* recommend_page=new QWidget;auto* recommend_layout=new QVBoxLayout(recommend_page);advice=new QLabel;advice->setObjectName("smartRecommendations");advice->setWordWrap(true);advice->setTextInteractionFlags(Qt::TextSelectableByMouse);recommend_layout->addWidget(advice);recommend_layout->addStretch();
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setWidget(recommend_page);tabs->addTab(scroll,tr("Measured recommendations"));
    readout=new QLabel;readout->setObjectName("smartReadout");readout->setWordWrap(true);layout->addWidget(readout);
    timing=new QLabel;timing->setObjectName("smartTiming");timing->setWordWrap(true);layout->addWidget(timing);
    status=new QLabel;status->setObjectName("smartStatus");status->setWordWrap(true);layout->addWidget(status);
    auto* reset=new QPushButton(tr("Reset this axis to previous draft"));reset->setObjectName("smartResetAxis");layout->addWidget(reset);
    confirmation=new QCheckBox(tr("I approve saving these selected axes. Applying stops tracking and saves the current profile; Save as writes a separate INI profile."));confirmation->setObjectName("smartConfirm");layout->addWidget(confirmation);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Cancel);apply_button=buttons->addButton(tr("Apply selected axes"),QDialogButtonBox::AcceptRole);apply_button->setObjectName("smartApply");save_button=buttons->addButton(tr("Save as new profile…"),QDialogButtonBox::ActionRole);save_button->setObjectName("smartSaveAs");layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(axis,&QComboBox::currentIndexChanged,this,[this]{load_axis();});
    connect(style,&QComboBox::currentIndexChanged,this,[this]{regenerate();});
    for(auto* s:{target_positive,target_negative,sensitivity_positive,sensitivity_negative,shape,deadzone})connect(s,&QDoubleSpinBox::valueChanged,this,[this]{regenerate();});
    connect(probe,&QDoubleSpinBox::valueChanged,this,[this]{refresh_preview();});
    connect(confirmation,&QCheckBox::toggled,this,[this]{refresh_preview();});
    connect(reset,&QPushButton::clicked,this,[this]{const int i=axis->currentIndex();const auto current=draft.axis[i];draft.axis[i]=previous[i];previous[i]=current;updating=true;positive[i].set_points(qt_points(draft.axis[i].positive_points));negative[i].set_points(qt_points(draft.axis[i].negative_points));updating=false;load_axis();});
    connect(game,&QComboBox::currentIndexChanged,this,[this](int index){if(updating)return;if(index==0){template_reason->setText(tr("Targets are personal and editable."));return;}
        const auto& t=racing_templates[index-1];draft.use_case=t.name;template_reason->setText(QString::fromLatin1(t.reason)+tr(" These targets are starting preferences, independent of game FOV."));
        updating=true;for(int i:{3,4})if(draft.axis[i].calibrated){previous[i]=draft.axis[i];auto& a=draft.axis[i];a.target_positive=a.target_negative=i==3?t.yaw:t.pitch;a.style=t.style;generate(a);positive[i].set_points(qt_points(a.positive_points));negative[i].set_points(qt_points(a.negative_points));}updating=false;load_axis();});
    connect(apply_button,&QPushButton::clicked,this,[this]{if(!confirmation->isChecked() || !valid_selection())return;if(!stop_tracking || !stop_tracking()){status->setText(tr("Tracking could not be stopped. Retry before applying."));return;}
        draft.date=QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();if(!apply(mappings,draft,stored,true,selected_axes())){status->setText(tr("Curve validation failed. Adjust the draft before applying."));return;}applied=true;accept();});
    connect(save_button,&QPushButton::clicked,this,[this]{if(!confirmation->isChecked() || !valid_selection() || !new_profile_path)return;const auto destination=new_profile_path();if(destination.isEmpty())return;
        draft.date=QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
        QString source;options::globals::with_settings_object([&](QSettings& ini){source=ini.fileName();});
        const auto error=save_new_mapping_profile(source,destination,mappings,draft,selected_axes());if(!error.isEmpty()){status->setText(error);return;}accept();});
    load_axis();
}
axis_selection smart_mapping_dialog::selected_axes() const {axis_selection mask{};for(int i=0;i<6;++i)mask[i]=selected[i]->isChecked() && selected[i]->isEnabled();return mask;}
void smart_mapping_dialog::load_axis(){updating=true;const int i=axis->currentIndex();const auto& a=draft.axis[i];
    style->setCurrentIndex(int(a.style));target_positive->setMaximum(i<3?600:180);target_negative->setMaximum(i<3?600:180);
    target_positive->setValue(a.target_positive);target_negative->setValue(a.target_negative);sensitivity_positive->setValue(a.sensitivity_positive);sensitivity_negative->setValue(a.sensitivity_negative);shape->setValue(a.shape);
    deadzone->setMaximum(std::min(a.positive.magnitude,a.negative.magnitude)*.39);deadzone->setValue(a.deadzone);probe->setRange(-std::max(1.,a.negative.magnitude)*1.1,std::max(1.,a.positive.magnitude)*1.1);
    for(QWidget* w:std::initializer_list<QWidget*>{style,target_positive,target_negative,sensitivity_positive,sensitivity_negative,shape,deadzone,positive_editor,negative_editor})w->setEnabled(a.calibrated);
    positive_editor->set_config(&positive[i]);negative_editor->set_config(&negative[i]);positive_editor->set_comparison(&original_positive[i]);negative_editor->set_comparison(&original_negative[i]);updating=false;refresh_preview();}
void smart_mapping_dialog::regenerate(){if(updating)return;const int i=axis->currentIndex();auto& a=draft.axis[i];if(!a.calibrated)return;previous[i]=a;
    a.style=response_style(style->currentData().toInt());a.target_positive=target_positive->value();a.target_negative=target_negative->value();a.sensitivity_positive=sensitivity_positive->value();a.sensitivity_negative=sensitivity_negative->value();a.shape=shape->value();a.deadzone=deadzone->value();
    a.output_gain=current_gain(controls,i);if(generate(a)){updating=true;positive[i].set_points(qt_points(a.positive_points));negative[i].set_points(qt_points(a.negative_points));updating=false;}refresh_preview();}
void smart_mapping_dialog::point_edited(int i){if(updating)return;previous[i]=draft.axis[i];auto& a=draft.axis[i];a.manual_points=true;
    for(int branch=0;branch<2;++branch){auto& pts=branch?a.negative_points:a.positive_points;pts.clear();for(const auto& p:(branch?negative[i]:positive[i]).get_points())pts.push_back({p.x(),p.y()});}
    if(!a.positive_points.empty())a.target_positive=a.positive_points.back().output*a.output_gain;if(!a.negative_points.empty())a.target_negative=a.negative_points.back().output*a.output_gain;
    if(i==axis->currentIndex())load_axis();}
bool smart_mapping_dialog::valid_selection(QString* reason) const {const auto fail=[&](QString text){if(reason)*reason=text;return false;};bool any=false;const auto mask=selected_axes();
    record checked=draft;for(int i=0;i<6;++i)checked.axis[i].calibrated &= mask[i];
    for(int i=0;i<6;++i)if(mask[i]){any=true;const auto& a=draft.axis[i];spline p,n;
        if(*mappings(i).opts.src!=i || !*settings.apply_mapping_curves)return fail(tr("Selected axes must use their own tracker input with mapping enabled."));
        if(i<3 && (*settings.reltrans_mode!=reltrans_disabled || *settings.neck_enable))return fail(tr("Disable relative translation and neck compensation before generating translation targets."));
        if(a.positive_points.empty() || a.negative_points.empty() || !verify_branch(p,a.positive_points) || !verify_branch(n,a.negative_points) || !decode(encode(checked)))return fail(tr("Edited points must start at zero, remain monotonic, and stay within measured physical ranges. Reset the axis or adjust its points."));
        if(std::max({a.target_positive,a.target_negative,a.positive_points.back().output,a.negative_points.back().output})>(i<3?600.:180.))return fail(tr("A selected target exceeds the native output limit at this Amplification. Reduce its target or adjust Amplification in Mapping."));}
    return any?true:fail(tr("Select a calibrated axis. Run Automatic calibration first to measure personal physical ranges."));}
void smart_mapping_dialog::refresh_preview(){if(updating)return;const int i=axis->currentIndex();const auto& a=draft.axis[i];const double gain=current_gain(controls,i);
    const double max_output=i<3?600.:180.;double limit=std::max(a.target_positive,a.target_negative);if(!a.positive_points.empty())limit=std::max(limit,a.positive_points.back().output);if(!a.negative_points.empty())limit=std::max(limit,a.negative_points.back().output);
    if(i<3){for(int v:{75,100,150,300,600})if(limit<=v){limit=v;break;}}else limit=180;
    for(int branch=0;branch<2;++branch){auto* w=branch?negative_editor:positive_editor;w->set_mapping_overlay(true,gain);w->set_calibration_visuals(branch?a.negative.magnitude:a.positive.magnitude,a.deadzone,limit,branch?-1:1);
        w->set_preview_bounds(std::max(1.,branch?a.negative.magnitude:a.positive.magnitude)*1.1,std::max({1.,limit,a.target_positive,a.target_negative})*1.08);w->set_x_step(i<3?1:5);w->set_y_step(i<3?15:30);}
    comparison->set_axis(a,std::min(limit,max_output),i<3?"cm":"degrees");const auto& opts=mappings(i).opts;
    const auto r=preview_target(probe->value(),positive[i],negative[i],true,*opts.invert_pre,*opts.invert_post,*opts.zero,gain,std::min(limit,max_output),*settings.apply_mapping_curves,*controls.head_enabled);
    readout->setText(tr("Physical %1 %2 → Curve output %3 → Amplification %4× = %5 → Final target %6 %7%8\nSteady target preview includes input/output signs, output cap and zero offset; live filtering and prediction affect the moving pose.")
        .arg(names[i]).arg(r.physical,0,'f',2).arg(r.curve,0,'f',2).arg(r.gain,0,'f',3).arg(r.amplified,0,'f',2).arg(r.final,0,'f',2).arg(i<3?"cm":"degrees").arg(r.saturated?tr(" (saturated)"):QString{}));
    timing->setText(tr("Speed %1%: response timing only. 55% retains the existing filter response; Amplification 42% is 1×. %2 No response style adds a temporal filter.").arg(*controls.speed).arg(*controls.head_enabled?tr("Head Movement Controls enabled."):tr("Head Movement Controls disabled: gain 1× and original timing.")));
    advice->setText(a.calibrated?recommendations(draft,i,a.target_positive,a.target_negative,gain,*controls.speed):tr("This axis has no physical calibration. Run Automatic calibration before requesting measured recommendations."));
    QString reason;const bool valid=valid_selection(&reason);status->setText(valid?tr("Draft ready. No profile is written until you approve and choose Apply or Save as."):reason);apply_button->setEnabled(valid && confirmation->isChecked());save_button->setEnabled(valid && confirmation->isChecked() && bool(new_profile_path));}
