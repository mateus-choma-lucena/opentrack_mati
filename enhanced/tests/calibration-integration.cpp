#include "enhanced/calibration-profile.hpp"
#include "gui/calibration-dialog.hpp"
#include "gui/mapping-dialog.hpp"
#include "gui/smart-mapping-dialog.hpp"
#include "logic/pipeline.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFontDatabase>
#include <QLabel>
#include <QComboBox>
#include <QWizardPage>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QScreen>
#include <QFontMetrics>
#include <QHeaderView>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace enhanced;
using namespace enhanced::calibration;
static void require(bool ok,const char* text) { if (!ok) throw std::runtime_error(text); }
static void wait_ms(int ms) { QEventLoop loop; QTimer::singleShot(ms,&loop,&QEventLoop::quit); loop.exec(); }
static double cpu_ms() {
#ifdef _WIN32
    FILETIME created{},exited{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user);
    const auto value=[](FILETIME t){return (std::uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};return (value(kernel)+value(user))/10000.;
#else
    return 0;
#endif
}
static record example() {
    record r; r.date="2026-10-11T12:00:00Z"; r.use_case="Flight simulation";
    r.jitter.valid_samples=301; r.jitter.valid_seconds=4; r.jitter.valid_hz=75; r.jitter.interval_p05_ms=r.jitter.interval_p50_ms=r.jitter.interval_p95_ms=1000./75;
    r.jitter.reliability=.9;
    for (int i=0;i<6;++i) {
        auto& a=r.axis[i]; a.positive={i<3?4.:32.,.95,3.,226,true}; a.negative={i<3?2.:20.,.9,3.,226,true};
        a.target_positive=i<3?15.:150.; a.target_negative=i<3?12.:120.; a.deadzone=i<3?.05:.5;
        require(generate(a),"example generation failed");
    }
    return r;
}
struct source final : ITracker {
    pose physical{2,3,4,10,8,5};
    bool valid=true;
    std::mutex mutex;
    module_status start_tracker(QFrame*) override { return status_ok(); }
    void data(double* out) override { tracking_sample_info ignored; data_with_info(out,ignored); }
    void data_with_info(double* out,tracking_sample_info& sample) override {
        std::lock_guard lock(mutex); sample={clock::now(),{}}; sample.validated=true; sample.calibration_captured=sample.captured;
        if (valid) sample.calibration_physical=physical;
        sample.estimated_reliability=.95;
        for (int i=0;i<6;++i) out[i]=valid?physical[i]*2:std::numeric_limits<double>::quiet_NaN(); // Internal tracker filtering simulation.
    }
    void invalidate() { std::lock_guard lock(mutex); valid=false; }
};
struct test_filter final : IFilter {
    module_status initialize() override { return status_ok(); }
    void filter(const double* in,double* out) override { for (int i=0;i<6;++i) out[i]=in[i]*3; }
};
struct protocol final : IProtocol {
    module_status initialize() override { return status_ok(); }
    QString game_name() override { return {}; }
    void pose(const double*,const double*) override {}
};
int main(int argc,char** argv) {
    QApplication app(argc,argv); app.setStyle("Fusion");QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeui.ttf")); app.setFont(QFont("Segoe UI",9));
    QTemporaryDir temporary;
    auto& global=options::globals::detail::global_settings();
    global.qsettings.emplace(temporary.filePath("globals.ini"),QSettings::IniFormat); global.qsettings->setValue(OPENTRACK_PROFILE_FILENAME_KEY,"calibration.ini"); global.mtx.unlock();
    const auto path=temporary.filePath("calibration.ini");
    auto& context=options::globals::detail::cur_settings(); context.pathname="calibration.ini"; context.qsettings.emplace(path,QSettings::IniFormat);
    context.qsettings->setValue("neuralnet-tracker/camera-name","PS3 Eye existing"); context.qsettings->setValue("legacy/setting",1234); context.qsettings->sync(); context.mtx.unlock();
    try {
        const std::string test=argc>1?argv[1]:"profile";
        main_settings settings; settings.center_at_startup=false; settings.centering_mode=center_point;
        Mappings mappings(settings.all_axis_opts); profile stored;
        const auto r=example();
        if (test=="profile") {
            require((*stored.data).isEmpty() && (*stored.rollback).isEmpty() && !decode(*stored.data),"old profile enabled calibration");
            mappings.forall([](Map& m) { m.spline_main.set_points({{0,0},{10,20},{30,60}}); m.spline_alt.set_points({{0,0},{12,24}}); m.opts.altp=false; m.save(); m.opts.b_mapping_window->save(); });
            const auto original=mappings(3).spline_main.get_points(); const auto clamp=*mappings(3).opts.clamp_x_;
            require(!apply(mappings,r,stored,false) && mappings(3).spline_main.get_points()==original,"unconfirmed overwrite changed mapping");
            require(apply(mappings,r,stored,true),"application rejected real splines");
            for (int i=0;i<6;++i) {
                const auto& a=r.axis[i]; auto& m=mappings(i);
                require(*m.opts.altp && std::abs(m.spline_main.get_value_no_save(a.positive.magnitude)-a.target_positive)<.05 && std::abs(m.spline_alt.get_value_no_save(-a.negative.magnitude)+a.target_negative)<.05,"actual mapping branch sign/endpoint incorrect");
                m.save(); m.opts.b_mapping_window->save();
            }
            stored.b->save(); context.qsettings->sync();
            const auto applied_points=mappings(3).spline_main.get_points();
            mappings(3).spline_main.clear(); mappings(3).opts.altp=false;
            mappings(3).load(); mappings(3).opts.b_mapping_window->reload();
            require(mappings(3).spline_main.get_points()==applied_points && *mappings(3).opts.altp,"saved mapping points/branch flag did not reload");
            const auto loaded=decode(*stored.data); require(loaded && encode(*loaded)==encode(r),"calibration record did not round-trip");
            stored.data=QString{}; stored.rollback=QString{}; stored.b->reload();
            QSettings disk(path,QSettings::IniFormat); require(disk.value("legacy/setting").toInt()==1234 && disk.value("neuralnet-tracker/camera-name").toString()=="PS3 Eye existing","profile compatibility broken");
            require(!restore(mappings,stored,false),"unconfirmed rollback accepted");
            require(restore(mappings,stored,true) && mappings(3).spline_main.get_points()==original && *mappings(3).opts.clamp_x_==clamp && !*mappings(3).opts.altp && (*stored.data).isEmpty(),"rollback lost original curves/settings");
            const auto valid_json=encode(r); auto obj=QJsonDocument::fromJson(valid_json.toUtf8()).object(); obj["schema"]=99;
            require(!decode(QString::fromUtf8(QJsonDocument(obj).toJson())),"unknown schema accepted");
            stored.rollback="{broken}"; require(!restore(mappings,stored,true) && mappings(3).spline_main.get_points()==original,"corrupt snapshot partially restored");
        } else if(test=="profile-write-failure") {
#ifdef _WIN32
            const auto original_keys=context.qsettings->allKeys();const auto original_camera=context.qsettings->value("neuralnet-tracker/camera-name");
            const auto lock=CreateFileW(reinterpret_cast<const wchar_t*>(path.utf16()),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);require(lock!=INVALID_HANDLE_VALUE,"cannot lock isolated profile");
            axis_selection selected{};selected[3]=true;require(apply(mappings,r,stored,true,selected),"write failure draft could not apply");
            enhanced_settings controls;controls.set_raii_dtor_state(false);
            require(!save_selected_mapping(mappings,stored,selected,controls),"unwritable profile reported success");
            CloseHandle(lock);
            require(context.qsettings->allKeys()==original_keys && context.qsettings->value("neuralnet-tracker/camera-name")==original_camera,"failed save left queued changes");
            require(restore(mappings,stored,true),"failed save mappings did not restore");
            require(apply(mappings,r,stored,true,selected) && save_selected_mapping(mappings,stored,selected,controls),"save could not retry after I/O recovery");
            QSettings disk(path,QSettings::IniFormat);require(decode(disk.value("automatic-calibration/record").toString()).has_value() && disk.value("legacy/setting").toInt()==1234,"retry lost profile compatibility");
#else
            return 77; // Windows atomic-replace sharing failure.
#endif
        } else if (test=="splines") {
            for (double extent:{.3,1.,3.,10.,40.,120.}) for (double shape:{1.2,1.5,2.,2.5,3.}) for (double deadzone:{0.,.005,.1}) {
                axis_result a; a.positive={extent,.9,3,300,true}; a.negative={extent*.8,.9,3,300,true}; a.target_positive=150; a.target_negative=90; a.shape=shape; a.deadzone=deadzone;
                if (deadzone>=extent*.8*.4) continue;
                require(generate(a),"spline generation failed"); spline p,n;
                if (!verify_branch(p,a.positive_points) || !verify_branch(n,a.negative_points)) {
                    std::cerr<<"extent="<<extent<<" shape="<<shape<<" deadzone="<<deadzone<<'\n';
                    double last=0;
                    for (int k=0;k<=8192;++k) {
                        const double x=extent*k/8192, y=p.get_value_no_save(x), z=p.get_value_no_save(-x);
                        if (y+1e-7<last || y<0 || y>150.000001 || z>1e-7 || z<-150.05) { std::cerr<<"bucket="<<k<<" x="<<x<<" y="<<y<<" last="<<last<<" negative="<<z<<'\n'; break; }
                        last=y;
                    }
                    throw std::runtime_error("actual spline is nonmonotonic or misses endpoint");
                }
                require(std::abs(p.get_value_no_save(1e-8))<1e-3 && std::abs(n.get_value_no_save(-1e-8))<1e-3,"actual spline discontinuous at zero");
                if (deadzone>.01) require(std::abs(p.get_value_no_save(deadzone*.8))<1e-6,"actual spline lost deadzone");
            }
        } else if (test=="pipeline") {
            enhanced_settings enhanced; enhanced.head_enabled=true; enhanced.amplification=90; enhanced.prediction_enabled=true;
            runtime_libraries libs; auto tracker=std::make_shared<source>(); libs.pTracker=tracker; libs.pFilter=std::make_shared<test_filter>(); libs.pProtocol=std::make_shared<protocol>(); TrackLogger logger;
            pipeline pipe(mappings,libs,logger); pipe.calibration_enable(true); pipe.start(); wait_ms(130);
            const auto first=pipe.calibration_drain(); bool found=false; std::uint64_t epoch=0;
            for (const auto& s:first) if (s.valid) { require(s.physical==tracker->physical,"filter/prediction/amplification contaminated calibration"); epoch=s.center_epoch; found=true; }
            require(found,"pipeline never published physical observation");
            pipe.set_center(true); wait_ms(80); const auto centered=pipe.calibration_drain();
            require(!centered.empty() && centered.back().center_epoch!=epoch,"pipeline recenter not visible");
            tracker->invalidate(); wait_ms(80); const auto missing=pipe.calibration_drain(); bool invalid=false; for (const auto& s:missing) invalid|=!s.valid;
            require(invalid,"NaN pipeline error path hid tracking loss"); pipe.calibration_enable(false);
        } else if (test.starts_with("wizard")) {
            std::vector<observation> pending; std::uint64_t epoch=1; bool running=true,stopped=false,active=false;
            timestamp capture=clock::now()+std::chrono::seconds(2);
            auto input=[&](bool enabled,std::vector<observation>& out) {active=enabled;if(!enabled){pending.clear();return running;}out=std::exchange(pending,{});return running;};
            calibration_dialog wizard(mappings,input,[&]{running=false;stopped=true;return true;},[&]{++epoch;return true;},nullptr);
            const auto feed=[&](axes pose,int count=251){for(int k=0;k<count;++k){capture+=std::chrono::milliseconds(20);pending.push_back({pose,capture,capture+std::chrono::milliseconds(10),epoch,true,.95,.9});}wait_ms(70);};
            const auto render=[&](const char* name){wait_ms(20);const auto bounds=wizard.screen()->availableGeometry();require(wizard.width()<=bounds.width() && wizard.height()<=bounds.height(),"wizard exceeds available screen");
                for(auto* label:wizard.currentPage()->findChildren<QLabel*>())if(label->isVisible() && !label->text().isEmpty()){
                    const int flags=(label->wordWrap()?Qt::TextWordWrap:Qt::TextSingleLine)|(label->buddy()?Qt::TextShowMnemonic:0);
                    const auto needed=label->fontMetrics().boundingRect(QRect(0,0,label->contentsRect().width(),10000),flags,label->text());
                    if(needed.height()>label->contentsRect().height()+2 || needed.width()>label->contentsRect().width()+2){std::cerr<<"Clipped label: "<<label->text().toStdString()<<" available="<<label->contentsRect().width()<<'x'<<label->contentsRect().height()<<" needed="<<needed.width()<<'x'<<needed.height()<<'\n';wizard.grab().save("calibration-layout-failure.png");throw std::runtime_error("page label clipped");}
                }
                const auto scale=QString::fromLocal8Bit(qgetenv("QT_SCALE_FACTOR"));wizard.grab().save(QString("%1%2.png").arg(name,scale.isEmpty()?QString{}:"-dpi-"+scale));};
            wizard.show();wait_ms(20);require(wizard.currentId()==calibration_dialog::welcome,"wizard did not start at Welcome");
            require(!wizard.button(QWizard::NextButton)->isEnabled(),"welcome skipped camera readiness");feed({},1);require(wizard.button(QWizard::NextButton)->isEnabled(),"validated readiness unavailable");
            render("calibration-welcome");wizard.button(QWizard::NextButton)->click();
            enhanced_settings controls;controls.set_raii_dtor_state(false);controls.head_enabled=true;controls.amplification=80;
            const auto record_step=[&](axes pose,bool loss=false){auto* p=wizard.currentPage();auto* record=p->findChild<QPushButton*>("calibrationRecord");auto* comfort=p->findChild<QCheckBox*>("calibrationComfort");
                require(record && comfort,"recording page controls missing");feed(pose,1);record->click();if(wizard.currentId()==calibration_dialog::center_head)wait_ms(550);
                if(loss){pending.push_back({});wait_ms(70);require(!wizard.button(QWizard::NextButton)->isEnabled(),"loss did not suspend recording");feed(pose,35);}
                feed(pose);require(!wizard.button(QWizard::NextButton)->isEnabled(),"comfort confirmation skipped");comfort->setChecked(true);require(wizard.button(QWizard::NextButton)->isEnabled(),"held measurement rejected");
                render("calibration-recording");wizard.button(QWizard::NextButton)->click();require((*stored.data).isEmpty(),"wizard changed profile during recording");};
            if(test=="wizard-benchmark"){
                record_step({});QTimer producer;producer.setTimerType(Qt::PreciseTimer);producer.setInterval(13);
                unsigned produced=0;QObject::connect(&producer,&QTimer::timeout,&wizard,[&]{const auto now=clock::now();pending.push_back({{},now,now,epoch,true,.95,.9});++produced;});producer.start();
                for(bool recording:{false,true}){if(recording)wizard.currentPage()->findChild<QPushButton*>("calibrationRecord")->click();const auto start=clock::now();const double before=cpu_ms();const auto count=produced;wait_ms(4500);const double elapsed=milliseconds(start,clock::now()),used=cpu_ms()-before;
                    std::cout<<(recording?"recording":"live_preview")<<" synthetic_observations="<<produced-count<<" wall_ms="<<elapsed<<" process_cpu_ms="<<used<<" one_core_cpu_percent="<<100*used/elapsed<<'\n';}
                require(wizard.currentPage()->findChild<QLabel*>("calibrationStatus")->text().contains("Complete"),"paced UI recording incomplete");producer.stop();wizard.reject();require(!active && (*stored.data).isEmpty(),"benchmark changed profile");return 0;
            }
            for(int id=calibration_dialog::center_head;id<=calibration_dialog::down;++id){require(wizard.currentId()==id,"core page order incorrect");axes p{};if(id==calibration_dialog::left)p[3]=-20;if(id==calibration_dialog::right)p[3]=30;if(id==calibration_dialog::up)p[4]=15;if(id==calibration_dialog::down)p[4]=-25;
                if(test=="wizard-cancel" && id==calibration_dialog::left){feed(p,1);wizard.currentPage()->findChild<QPushButton*>("calibrationRecord")->click();feed(p,20);wizard.reject();require(!active && !stopped && (*stored.data).isEmpty() && wizard.result()==QDialog::Rejected,"Cancel changed mappings or left acquisition active");std::cout<<"calibration integration "<<test<<": PASS\n";return 0;}
                if(test=="wizard-back" && id==calibration_dialog::right){wizard.button(QWizard::BackButton)->click();require(wizard.currentId()==calibration_dialog::left && !wizard.draft_record().axis[3].negative.comfort_confirmed,"Back did not clear dependent ranges");axes redo{};redo[3]=-20;record_step(redo);}
                record_step(p,test=="wizard-loss" && id==calibration_dialog::left);}
            require(wizard.currentId()==calibration_dialog::optional,"optional page missing");
            const bool optional=test=="wizard-optional";
            if(optional || test=="wizard-skip"){wizard.findChild<QCheckBox*>("calibrationRoll")->setChecked(true);wizard.findChild<QCheckBox*>("calibrationTranslations")->setChecked(true);}
            wizard.button(QWizard::NextButton)->click();
            if(test=="wizard-skip"){while(wizard.currentId()<calibration_dialog::game_profile){wizard.currentPage()->findChild<QPushButton*>("calibrationSkip")->click();}require(!wizard.draft_record().axis[5].calibrated,"Skip applied optional range");}
            if(optional)for(int id=calibration_dialog::roll_left;id<=calibration_dialog::z_backward;++id){require(wizard.currentId()==id,"optional page order incorrect");axes p{};if(id<=calibration_dialog::roll_right)p[5]=id==calibration_dialog::roll_left?-10:15;else p[(id-calibration_dialog::x_left)/2]=(id-calibration_dialog::x_left)%2?-3.:2.;record_step(p);}
            require(wizard.currentId()==calibration_dialog::game_profile,"game profile page missing");wizard.findChild<QComboBox*>("calibrationGame")->setCurrentIndex(1);wizard.button(QWizard::NextButton)->click();
            require(wizard.currentId()==calibration_dialog::compare && wizard.findChild<QWidget*>("calibrationComparison"),"comparison page missing");render("calibration-comparison");wizard.button(QWizard::NextButton)->click();
            require(wizard.currentId()==calibration_dialog::review,"review page missing");auto* approval=wizard.findChild<QCheckBox*>("calibrationOverwrite");auto* table=wizard.findChild<QTableWidget*>("calibrationReviewTable");require(!wizard.button(QWizard::CommitButton)->isEnabled(),"approval skipped");
            if(test=="wizard-recenter"){++epoch;feed({},1);require(wizard.currentId()==calibration_dialog::welcome && !stopped && (*stored.data).isEmpty(),"recenter survived review");wizard.reject();std::cout<<"calibration integration "<<test<<": PASS\n";return 0;}
            if(test=="wizard-recenter-invalid"){observation missing;missing.center_epoch=++epoch;pending.push_back(missing);wait_ms(70);require(wizard.currentId()==calibration_dialog::welcome && !stopped && (*stored.data).isEmpty(),"new epoch survived unreliable tracking");wizard.reject();std::cout<<"calibration integration "<<test<<": PASS\n";return 0;}
            auto* target=static_cast<QDoubleSpinBox*>(table->cellWidget(3,3));require(target->value()==65,"LMU editable target incorrect");target->setValue(55);
            if(test=="wizard"){controls.amplification=0;target->setValue(56);approval->setChecked(true);require(!wizard.button(QWizard::CommitButton)->isEnabled() && wizard.findChild<QLabel*>("calibrationReviewValidity")->text().contains("Amplification"),"unreachable target was not explained");controls.amplification=80;target->setValue(55);}
            if(test=="wizard-save-failure"){const auto before=mappings(3).spline_main.get_points();wizard.save_applied=[](const axis_selection&){return false;};approval->setChecked(true);wizard.button(QWizard::CommitButton)->click();require(wizard.currentId()==calibration_dialog::review && mappings(3).spline_main.get_points()==before && (*stored.data).isEmpty(),"save failure lost rollback");wizard.reject();std::cout<<"calibration integration "<<test<<": PASS\n";return 0;}
            if(test=="wizard-smart-save-failure"){const auto before=mappings(3).spline_main.get_points();wizard.save_applied=[](const axis_selection&){return false;};QTimer::singleShot(0,&wizard,[&]{auto* dialog=wizard.findChild<smart_mapping_dialog*>();require(dialog,"nested Smart Mapping missing");dialog->findChild<QCheckBox*>("smartConfirm")->setChecked(true);auto* apply=dialog->findChild<QPushButton*>("smartApply");require(apply->isEnabled(),"nested apply unavailable");apply->click();});wizard.findChild<QPushButton*>("calibrationSmartMapping")->click();require(wizard.currentId()==calibration_dialog::review && !wizard.applied_to_current() && mappings(3).spline_main.get_points()==before && (*stored.data).isEmpty() && (*stored.rollback).isEmpty(),"nested save failure lost rollback");wizard.reject();std::cout<<"calibration integration "<<test<<": PASS\n";return 0;}
            render("calibration-review");for(int column=0;column<table->columnCount();++column)require(table->fontMetrics().horizontalAdvance(table->horizontalHeaderItem(column)->text())+8<=table->columnWidth(column),"review table header clipped");approval->setChecked(true);require(wizard.button(QWizard::CommitButton)->isEnabled(),"review validation rejected");wizard.button(QWizard::CommitButton)->click();
            require(stopped && wizard.currentId()==calibration_dialog::completion && wizard.persisted_to_current(),"completion did not save and stop tracking");render("calibration-complete");wizard.button(QWizard::FinishButton)->click();require(!active && wizard.result()==QDialog::Accepted,"Finish did not clean up acquisition");
            const auto data=decode(*stored.data);require(data.has_value(),"saved wizard record did not decode");for(int i=0;i<6;++i)require(data->axis[i].calibrated==(optional || i==3 || i==4),"optional/skipped metadata incorrect");
            require(std::abs(mappings(3).spline_alt.get_value_no_save(-20)*enhanced::amplification_gain(80)+55)<.05,"virtual output/gain incorrect");require(restore(mappings,stored,true),"wizard rollback missing");
        } else throw std::runtime_error("unknown integration case");
        std::cout<<"calibration integration "<<test<<": PASS\n"; return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
