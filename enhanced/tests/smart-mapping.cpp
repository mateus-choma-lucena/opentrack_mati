#include "gui/smart-mapping-dialog.hpp"
#include "gui/mapping-dialog.hpp"
#include "logic/pipeline.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFontDatabase>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

using namespace enhanced;
using namespace enhanced::calibration;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void wait_ms(int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();}
static record measured(){record r;r.date="2026-10-11T12:00:00Z";r.jitter.valid_hz=75;r.jitter.reliability=.92;r.jitter.valid_seconds=4;r.jitter.valid_samples=301;
    for(int i=0;i<6;++i){auto& a=r.axis[i];a.positive={i<3?4.:32.,.92,3,226,true};a.negative={i<3?2.:20.,.9,3,226,true};
        a.target_positive=i<3?15.:65.;a.target_negative=i<3?12.:55.;a.deadzone=i<3?.03:.3;r.jitter.mad[i]=i<3?.005:.05;r.jitter.p95[i]=i<3?.01:.1;r.jitter.deadzone[i]=i<3?.018:.18;check(generate(a),"fixture");}return r;}
static QByteArray bytes(const QString& path){QFile f(path);check(f.open(QIODevice::ReadOnly),"read fixture");return f.readAll();}
struct source final:ITracker{pose value{2,-1,1,10,-8,5};module_status start_tracker(QFrame*)override{return status_ok();}void data(double* out)override{std::copy(value.begin(),value.end(),out);}
    void data_with_info(double* out,tracking_sample_info& s)override{data(out);s={clock::now(),{}};}};
struct sink final:IProtocol{std::mutex mutex;enhanced::pose value{};module_status initialize()override{return status_ok();}QString game_name()override{return{};}
    void pose(const double* out,const double*)override{std::lock_guard lock(mutex);std::copy_n(out,6,value.begin());}enhanced::pose read(){std::lock_guard lock(mutex);return value;}};
int main(int argc,char** argv){QApplication app(argc,argv);QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");app.setFont(QFont("Segoe UI",9));QTemporaryDir directory;
    auto& global=options::globals::detail::global_settings();global.qsettings.emplace(directory.filePath("globals.ini"),QSettings::IniFormat);global.qsettings->setValue(OPENTRACK_PROFILE_FILENAME_KEY,"smart.ini");global.mtx.unlock();
    const auto path=directory.filePath("smart.ini");auto& context=options::globals::detail::cur_settings();context.pathname="smart.ini";context.qsettings.emplace(path,QSettings::IniFormat);
    context.qsettings->setValue("neuralnet-tracker/camera-name","PS3 Eye 75 FPS");context.qsettings->setValue("enhanced-tracking/prediction-ms",11);context.qsettings->setValue("unknown-plugin/preserved","unchanged");context.qsettings->sync();context.mtx.unlock();
    try{const std::string mode=argc>1?argv[1]:"natural";main_settings settings;settings.center_at_startup=false;settings.centering_mode=center_disabled;settings.reltrans_mode=reltrans_disabled;settings.neck_enable=false;
        Mappings maps(settings.all_axis_opts);profile stored;enhanced_settings controls;controls.set_raii_dtor_state(false);auto r=measured();
        maps.forall([](Map& m){m.spline_main.set_points({{0,0},{10,20},{30,60}});m.spline_alt.set_points({{0,0},{10,10},{30,30}});m.opts.altp=false;m.save();m.opts.b_mapping_window->save();});
        context.qsettings->sync();
        const auto original=maps(3).spline_main.get_points();const auto disk_before=bytes(path);
        if(mode=="natural" || mode=="balanced" || mode=="responsive"){
            const auto style=mode=="natural"?response_style::natural:mode=="balanced"?response_style::balanced:response_style::responsive;
            for(int i=0;i<6;++i)for(double shaping:{1.2,2.,3.})for(double gain:{.5,1.,4.})for(double sensitivity:{.5,1.,2.}){
                auto a=r.axis[i];a.style=style;a.shape=shaping;a.output_gain=gain;a.sensitivity_positive=sensitivity;a.sensitivity_negative=2.5-sensitivity;
                check(generate(a),"response generation");const auto once=encode([&]{auto v=r;v.axis[i]=a;return v;}());check(generate(a),"regenerate");auto v=r;v.axis[i]=a;check(encode(v)==once,"determinism");
                spline p,n;check(verify_branch(p,a.positive_points)&&verify_branch(n,a.negative_points),"native style monotonicity");
                check(std::abs(p.get_value_no_save(a.positive.magnitude)*gain-a.target_positive)<.08,"positive target/gain");check(std::abs(n.get_value_no_save(-a.negative.magnitude)*gain+a.target_negative)<.08,"negative target/gain");
                check(std::abs(p.get_value_no_save(a.deadzone*.7))<1e-6 && std::abs(n.get_value_no_save(-a.deadzone*.7))<1e-6,"deadzone");
            }
            std::array<double,3> at_center{};for(int i=0;i<3;++i){auto a=r.axis[3];a.style=response_style(i+1);generate(a);spline s;private_spline(s);s.set_points(qt_points(a.positive_points));at_center[i]=s.get_value_no_save(a.positive.magnitude*.2);}
            check(at_center[1]<at_center[0] && at_center[0]<at_center[2],"styles have wrong early response order");
        }else if(mode=="overlay"){
            controls.head_enabled=true;controls.amplification=80;smart_mapping_dialog dialog(maps,[]{return true;},{},nullptr,r);dialog.show();wait_ms(30);
            auto* w=dialog.findChild<spline_widget*>("smartPositiveEditor");auto* n=dialog.findChild<spline_widget*>("smartNegativeEditor");
            check(w && n && w->mapping_overlay_enabled(),"native editor missing");const double gain=amplification_gain(80);
            check(std::abs(w->comparison_output_for_input(10)-maps(3).spline_main.get_value_no_save(10))<1e-8,"original overlay");
            check(std::abs(w->adjusted_output_for_input(10)-dialog.draft_curve(3).get_value_no_save(10)*gain)<1e-8,"effective overlay double gain");
            check(std::abs(n->adjusted_output_for_input(10)+dialog.draft_curve(3,true).get_value_no_save(-10)*gain)<1e-8,"negative evaluator visualization");
            dialog.findChild<QDoubleSpinBox*>("smartPhysicalProbe")->setValue(10);check(dialog.findChild<QLabel*>("smartReadout")->text().contains("Final target"),"readout");
            dialog.grab().save("smart-mapping-editor.png");dialog.findChild<QTabWidget*>()->setCurrentIndex(1);wait_ms(10);dialog.grab().save("smart-mapping-comparison.png");
            dialog.findChild<QTabWidget*>()->setCurrentIndex(2);wait_ms(10);dialog.grab().save("smart-mapping-recommendations.png");
            if(bytes(path)!=disk_before)std::cerr<<"before bytes="<<disk_before.size()<<" after bytes="<<bytes(path).size()<<'\n';
            check(bytes(path)==disk_before,"preview changed INI bytes");check(maps(3).spline_main.get_points()==original,"preview changed live points");dialog.reject();
            spline_widget legacy;legacy.set_config(&maps(3).spline_main);legacy.set_mapping_overlay(true,2);check(legacy.adjusted_output_for_input(-10)<0,"legacy signed overlay regression");
        }else if(mode=="gain-speed-saturation"){
            auto a=r.axis[3];a.output_gain=4;generate(a);spline p,n;private_spline(p);private_spline(n);p.set_points(qt_points(a.positive_points));n.set_points(qt_points(a.negative_points));
            const auto target=preview_target(10,p,n,true,true,true,3,4,180);
            check(std::abs(target.final+target.bounded-3)<1e-8 && target.curve<0,"sign/zero preview");
            const auto capped=preview_target(32,p,n,true,false,false,0,20,60);check(capped.saturated && capped.bounded==60,"saturation readout");
            check(preview_target(250,p,n,true,false,false,0,1,180,false,false).final==250,"legacy disabled mapping/controls clipped");
            std::array<double,3> initial{};int k=0;for(int speed:{0,55,100}){head_movement_controls c;const auto start=clock::now();head_control_parameters params{true,false,100,speed};c.configure(start,params);c.configure(start+std::chrono::milliseconds(120),params);
                enhanced::pose zero{},limits{180,180,180,180,180,180};c.process(zero,zero,start+std::chrono::milliseconds(120),start,limits);enhanced::pose mapped{};mapped[3]=p.get_value_no_save(10);
                initial[k++]=c.process(mapped,mapped,start+std::chrono::milliseconds(130),start+std::chrono::milliseconds(130),limits)[3];
                for(int t=140;t<=1000;t+=10)c.process(mapped,mapped,start+std::chrono::milliseconds(t),start+std::chrono::milliseconds(t),limits);
                const auto d=c.snapshot({}, {}, {}, {}, true);check(std::abs(d.mapped_target[3]-mapped[3]*4)<1e-8,"Speed changed static target");}
            check(initial[0]<initial[2] && initial[2]<initial[1],"Speed timing ordering");
        }else if(mode=="selected-undo"){
            axis_selection yaw{};yaw[3]=true;const auto pitch=maps(4).spline_main.get_points();auto bad=r;bad.axis[4].positive_points.clear();
            check(apply(maps,bad,stored,true,yaw),"unchecked invalid axis blocked selected apply");check(maps(4).spline_main.get_points()==pitch,"unchecked axis changed");const auto first=maps(3).spline_main.get_points();
            r.axis[3].style=response_style::responsive;generate(r.axis[3]);check(apply(maps,r,stored,true,yaw),"second apply");check(restore(maps,stored,true) && maps(3).spline_main.get_points()==first,"undo previous");
            check(restore_original(maps,stored,true) && maps(3).spline_main.get_points()==original,"restore original");check(restore(maps,stored,true) && maps(3).spline_main.get_points()==first,"undo original restore");
            check(bytes(path)==disk_before,"helper silently saved");stored.b->save();maps(3).save();maps(3).opts.b_mapping_window->save();QSettings disk(path,QSettings::IniFormat);
            check(disk.value("unknown-plugin/preserved").toString()=="unchanged" && disk.value("enhanced-tracking/prediction-ms").toInt()==11,"unrelated settings changed");
            // An unchecked manual draft and unrelated unsaved prediction change
            // must survive in memory without being included in selected saving.
            maps(4).spline_main.set_points({{0,0},{20,90}});maps(4).opts.altp=true;controls.prediction_ms=25;
            check(apply(maps,r,stored,true,yaw) && save_selected_mapping(maps,stored,yaw,controls),"selected persistence");
            QSettings selected_disk(path,QSettings::IniFormat);
            check(selected_disk.value(maps(4).name+"/points").value<QList<QPointF>>()==pitch && !selected_disk.value("opentrack-mappings/pitch-alt-axis-sign").toBool(),"selected save persisted unchecked draft");
            check(selected_disk.value("enhanced-tracking/prediction-ms").toInt()==11 && *controls.prediction_ms==25 && maps(4).spline_main.get_points().back().y()==90,"selected save lost unrelated pending edits");
        }else if(mode=="templates-edit"){
            smart_mapping_dialog dialog(maps,[]{return true;},{},nullptr,r);auto* game=dialog.findChild<QComboBox*>("smartTemplate");check(game->count()==6,"templates missing");
            for(int i=0;i<5;++i){game->setCurrentIndex(i+1);const auto& a=dialog.draft_record().axis[3];check(a.positive.magnitude==32 && a.negative.magnitude==20,"template replaced physical ranges");check(a.target_positive==racing_templates[i].yaw && a.style==racing_templates[i].style,"template target/style");}
            const auto before=dialog.draft_curve(3).get_points();auto points=before;points[12].setY((points[11].y()+points[13].y())/2);dialog.draft_curve(3).move_point(12,points[12]);
            check(dialog.draft_record().axis[3].manual_points && decode(encode(dialog.draft_record())).has_value(),"point edit not stored");
            dialog.findChild<QPushButton*>("smartResetAxis")->click();check(dialog.draft_curve(3).get_points()==before,"reset previous draft failed");
            for(int i=0;i<6;++i)dialog.findChild<QCheckBox*>(QString("smartSelect%1").arg(i))->setChecked(i==3);
            dialog.findChild<QCheckBox*>("smartConfirm")->setChecked(true);dialog.findChild<QPushButton*>("smartApply")->click();check(dialog.applied_to_current() && maps(3).spline_main.get_points()==before,"UI selected apply");check(maps(4).spline_main.get_points()==original,"template changed unchecked axis");
            mapping_dialog native(maps);native.calibration_stop_tracking=[]{return true;};native.show();wait_ms(10);
            const auto previous_yaw=maps(3).spline_main.get_points();const auto previous_clamp=*maps(3).opts.clamp_x_;
            maps(3).spline_main.set_points({{0,0},{20,100}});maps(3).opts.clamp_x_=axis_opts::r60;maps(4).spline_main.set_points({{0,0},{20,80}});
            native.findChild<QPushButton*>("mappingResetAxis")->click();check(maps(3).spline_main.get_points()==previous_yaw && *maps(3).opts.clamp_x_==previous_clamp && maps(4).spline_main.get_points().back().y()==80,"native editor axis reset affected another axis");
            native.reject();check(bytes(path)==disk_before,"native reset silently persisted");
        }else if(mode=="profiles-legacy"){
            const auto imported=directory.filePath("import.ini"),exported=directory.filePath("export.ini"),new_profile=directory.filePath("racing.ini");
            check(copy_profile_file(path,exported).isEmpty() && copy_profile_file(exported,imported).isEmpty(),"INI import/export");check(bytes(imported)==disk_before,"legacy INI changed in transport");
            check(!copy_profile_file(path,exported).isEmpty() && !copy_profile_file(path,path,true).isEmpty(),"unsafe overwrite accepted");
            QSettings legacy(imported,QSettings::IniFormat);check(legacy.value(maps(3).name+"/points").value<QList<QPointF>>()==original,"legacy points import");
            auto obj=QJsonDocument::fromJson(encode(r).toUtf8()).object();obj["schema"]=1;auto axes=obj["axes"].toArray();for(int i=0;i<6;++i){auto a=axes[i].toObject();for(auto key:{"style","sensitivity-positive","sensitivity-negative","output-gain","manual-points"})a.remove(key);axes[i]=a;}obj["axes"]=axes;
            check(decode(QString::fromUtf8(QJsonDocument(obj).toJson())).has_value(),"schema 1 record rejected");axis_selection yaw{};yaw[3]=true;
            const auto save_error=save_new_mapping_profile(path,new_profile,maps,r,yaw);if(!save_error.isEmpty())std::cerr<<save_error.toStdString()<<'\n';check(save_error.isEmpty(),"save new profile failed");check(bytes(path)==disk_before && maps(3).spline_main.get_points()==original,"save as changed active profile");
            QSettings next(new_profile,QSettings::IniFormat);check(next.value("unknown-plugin/preserved").toString()=="unchanged" && next.value("enhanced-tracking/prediction-ms").toInt()==11,"save as unrelated keys");
            check(next.value(maps(3).name+"/points").value<QList<QPointF>>()==qt_points(r.axis[3].positive_points),"save as native curve group");check(next.value(maps(4).name+"/points").value<QList<QPointF>>()==original,"save as unchecked points");
            check(!save_new_mapping_profile(path,new_profile,maps,r,yaw).isEmpty(),"save as overwrote existing profile");
            stored.data=next.value("automatic-calibration/record").toString();stored.rollback=next.value("automatic-calibration/rollback").toString();stored.original=next.value("automatic-calibration/original").toString();
            maps(3).spline_main.set_points(next.value(maps(3).name+"/points").value<QList<QPointF>>());check(restore_original(maps,stored,true) && maps(3).spline_main.get_points()==original,"save as original restore");
        }else if(mode=="save-as-ui"){
            controls.head_enabled=true;controls.amplification=80;controls.speed=72;controls.prediction_ms=25;
            const auto new_profile=directory.filePath("ui-racing.ini");bool stopped=false;
            smart_mapping_dialog dialog(maps,[&]{stopped=true;return true;},[&]{return new_profile;},nullptr,r);
            for(int i=0;i<6;++i)dialog.findChild<QCheckBox*>(QString("smartSelect%1").arg(i))->setChecked(i==3);
            dialog.findChild<QComboBox*>("smartTemplate")->setCurrentIndex(1);dialog.findChild<QCheckBox*>("smartConfirm")->setChecked(true);
            dialog.findChild<QPushButton*>("smartSaveAs")->click();check(dialog.result()==QDialog::Accepted && !dialog.applied_to_current() && !stopped,"save as UI mutated active tracking");
            check(bytes(path)==disk_before && maps(3).spline_main.get_points()==original,"save as UI changed active profile");
            QSettings saved(new_profile,QSettings::IniFormat);check(saved.value("enhanced-tracking/head-amplification").toInt()==80 && saved.value("enhanced-tracking/head-speed").toInt()==72,"pending gain/Speed not saved with curves");
            check(saved.value("enhanced-tracking/prediction-ms").toInt()==11 && *controls.prediction_ms==25,"save as persisted unrelated pending control");
            const auto data=decode(saved.value("automatic-calibration/record").toString());check(data && data->axis[3].calibrated && !data->axis[4].calibrated,"save as selected metadata");
            spline s;private_spline(s);s.set_points(saved.value(maps(3).name+"/points").value<QList<QPointF>>());check(std::abs(s.get_value_no_save(32)*amplification_gain(80)-65)<.08,"saved profile effective gain mismatch");
        }else if(mode=="pipeline"){
            controls.head_enabled=true;controls.amplification=80;controls.speed=55;controls.per_axis=true;
            controls.amplification_x=60;controls.amplification_y=70;controls.amplification_z=80;controls.amplification_yaw=90;controls.amplification_pitch=100;controls.amplification_roll=42;
            for(int i=0;i<6;++i){r.axis[i].style=response_style(i%3+1);r.axis[i].output_gain=current_gain(controls,i);generate(r.axis[i]);}
            check(apply(maps,r,stored,true),"pipeline apply");for(int i=0;i<6;++i){maps(i).opts.invert_pre=i%2==0;maps(i).opts.invert_post=i%3==0;maps(i).opts.zero=i<3?.5:3.;}
            runtime_libraries libs;auto tracker=std::make_shared<source>();auto output=std::make_shared<sink>();libs.pTracker=tracker;libs.pProtocol=output;TrackLogger logger;
            pipeline pipe(maps,libs,logger);pipe.start();wait_ms(350);const auto actual=output->read();
            for(int i=0;i<6;++i){const auto& m=maps(i);const auto expected=preview_target(tracker->value[i],m.spline_main,m.spline_alt,*m.opts.altp,*m.opts.invert_pre,*m.opts.invert_post,*m.opts.zero,current_gain(controls,i),m.opts.max_clamp_y());
                if(std::abs(actual[i]-expected.final)>.08){std::cerr<<"axis="<<i<<" actual="<<actual[i]<<" expected="<<expected.final<<'\n';throw std::runtime_error("real pipeline disagrees with preview");}}
        }else if(mode=="invalid-drafts"){
            smart_mapping_dialog dialog(maps,[]{return true;},{},nullptr,r);dialog.findChild<QCheckBox*>("smartConfirm")->setChecked(true);
            auto points=dialog.draft_curve(3).get_points();points[12].setY(points[10].y()*.5);dialog.draft_curve(3).set_points(points);
            check(!dialog.findChild<QPushButton*>("smartApply")->isEnabled(),"nonmonotonic manual draft accepted");dialog.findChild<QPushButton*>("smartResetAxis")->click();
            points=dialog.draft_curve(3).get_points();points.back().setX(40);dialog.draft_curve(3).set_points(points);check(!dialog.findChild<QPushButton*>("smartApply")->isEnabled(),"point exceeded measured physical range");
            dialog.findChild<QPushButton*>("smartResetAxis")->click();check(dialog.findChild<QPushButton*>("smartApply")->isEnabled(),"reset did not recover valid draft");
            const auto original_snapshot=*stored.original;stored.original="{broken}";check(!restore_original(maps,stored,true) && maps(3).spline_main.get_points()==original,"invalid original partially restored");stored.original=original_snapshot;
            auto obj=QJsonDocument::fromJson(encode(r).toUtf8()).object();auto noise=obj["stationary"].toObject();auto p95=noise["p95"].toArray();p95[3]=-1;noise["p95"]=p95;obj["stationary"]=noise;check(!decode(QString::fromUtf8(QJsonDocument(obj).toJson())),"invalid noise recommendations accepted");
            controls.head_enabled=true;controls.amplification=0;smart_mapping_dialog low_gain(maps,[]{return true;},{},nullptr,r);low_gain.findChild<QCheckBox*>("smartConfirm")->setChecked(true);
            check(!low_gain.findChild<QPushButton*>("smartApply")->isEnabled(),"unreachable target passed output cap");
            check(maps(3).spline_main.get_points()==original && bytes(path)==disk_before,"invalid draft changed profile");
        }else throw std::runtime_error("unknown smart case");
        std::cout<<"smart mapping "<<mode<<": PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
