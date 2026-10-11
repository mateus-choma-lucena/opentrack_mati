#include "enhanced/calibration-profile.hpp"
#include "gui/calibration-dialog.hpp"
#include "gui/mapping-dialog.hpp"
#include "logic/pipeline.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFontDatabase>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

using namespace enhanced;
using namespace enhanced::calibration;
static void require(bool ok,const char* text) { if (!ok) throw std::runtime_error(text); }
static void wait_ms(int ms) { QEventLoop loop; QTimer::singleShot(ms,&loop,&QEventLoop::quit); loop.exec(); }
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
    QApplication app(argc,argv); QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeui.ttf")); app.setFont(QFont("Segoe UI",9));
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
        } else if (test=="wizard" || test=="wizard-optional" || test=="wizard-recenter" || test=="wizard-loss") {
            std::vector<observation> pending; std::uint64_t epoch=1; bool running=true,stopped=false;
            timestamp capture=clock::now()+std::chrono::seconds(2);
            auto input=[&](bool enabled,std::vector<observation>& out) { if (!enabled) { pending.clear(); return running; } out=std::exchange(pending,{}); return running; };
            calibration_dialog wizard(mappings,input,[&] { running=false; stopped=true; return true; },[&] { ++epoch; return true; },nullptr);
            auto* record_button=wizard.findChild<QPushButton*>("calibrationRecord"); auto* next=wizard.findChild<QPushButton*>("calibrationNext");
            auto* comfort=wizard.findChild<QCheckBox*>("calibrationComfort"); auto* approval=wizard.findChild<QCheckBox*>("calibrationOverwrite"); auto* apply_button=wizard.findChild<QPushButton*>("calibrationApply");
            require(record_button && next && comfort && approval && apply_button,"wizard controls missing");
            wizard.show(); wait_ms(60); wizard.grab().save("calibration-neutral.png");
            const bool optional=test=="wizard-optional";
            if (optional) { wizard.findChild<QCheckBox*>("calibrationRoll")->setChecked(true); wizard.findChild<QCheckBox*>("calibrationTranslations")->setChecked(true); }
            enhanced_settings controls; controls.head_enabled=true; controls.amplification=80;
            require(*controls.amplification==80,"wizard changed Amplification without confirmation");
            for (int step=0;step<(optional?14:6);++step) {
                record_button->click(); if (step==0) wait_ms(550);
                axes p{}; if (step==2) p[3]=-20; if (step==3) p[3]=30; if (step==4) p[4]=15; if (step==5) p[4]=-25;
                if (step==6) p[5]=-10; if (step==7) p[5]=15;
                if (step>=8) p[(step-8)/2]=step%2?-3.:2.;
                if (test=="wizard-loss" && step==2) {
                    pending.push_back({}); wait_ms(70); comfort->setChecked(true); require(!next->isEnabled(),"wizard continued through loss");
                    record_button->click();
                }
                for (int k=0;k<251;++k) { capture+=std::chrono::milliseconds(20); pending.push_back({p,capture,capture+std::chrono::milliseconds(10),epoch,true,.95,.9}); }
                wait_ms(70); require(!next->isEnabled(),"wizard skipped comfort confirmation"); comfort->setChecked(true); require(next->isEnabled(),"wizard failed valid held measurement"); next->click();
                require((*stored.data).isEmpty(),"wizard changed profile during measurement");
            }
            auto* table=wizard.findChild<QTableWidget*>("calibrationReviewTable"); require(table && !apply_button->isEnabled(),"wizard skipped overwrite confirmation");
            if (test=="wizard-recenter") {
                ++epoch; capture+=std::chrono::milliseconds(20); pending.push_back({{},capture,capture,epoch,true,.95,.9});
                approval->setChecked(true); apply_button->click();
                require(!stopped && (*stored.data).isEmpty() && !next->isEnabled(),"review survived recenter");
                wizard.reject(); std::cout<<"calibration integration "<<test<<": PASS\n"; return 0;
            }
            auto* target=static_cast<QDoubleSpinBox*>(table->cellWidget(3,3)); require(target->value()==150,"default virtual left target incorrect"); target->setValue(135);
            if(test=="wizard") {
                controls.amplification=0;target->setValue(136);approval->setChecked(true);
                require(!apply_button->isEnabled() && wizard.findChild<QLabel*>("calibrationReviewValidity")->text().contains("Amplification"),"low gain did not explain unreachable targets");
                controls.amplification=80;target->setValue(135);
            }
            wizard.grab().save("calibration-review.png"); approval->setChecked(true); require(apply_button->isEnabled(),"preview validation rejected wizard result"); apply_button->click();
            require(stopped,"wizard did not stop tracking");
            require(static_cast<QDialog&>(wizard).result()==QDialog::Accepted,"wizard did not accept");
            const auto data=decode(*stored.data); require(data.has_value(),"wizard applied record did not decode");
            for (int i=0;i<6;++i) require(data->axis[i].calibrated==(optional || i==3 || i==4),"optional/skipped axis calibration incorrect");
            require(std::abs(mappings(3).spline_alt.get_value_no_save(-20)*enhanced::amplification_gain(80)+135)<.05 && *mappings(3).opts.altp,"editable virtual target/gain compensation failed");
            require(restore(mappings,stored,true),"wizard did not snapshot original mappings");
        } else throw std::runtime_error("unknown integration case");
        std::cout<<"calibration integration "<<test<<": PASS\n"; return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
