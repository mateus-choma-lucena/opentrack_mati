#include "logic/pipeline.hpp"
#include "gui/mapping-dialog.hpp"
#include "enhanced/head-controls-ui.hpp"
#include "enhanced/neuralnet2.hpp"
#include <QApplication>
#include <QTemporaryDir>
#include <QLibrary>
#include <QFontDatabase>
#include <QEventLoop>
#include <QScrollArea>
#include <QDir>
#include <QCheckBox>
#include <iostream>
#include <stdexcept>

using namespace enhanced;
static void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
static void wait_ms(int ms) { QEventLoop loop; QTimer::singleShot(ms,&loop,&QEventLoop::quit); loop.exec(); }
struct source final : ITracker {
    std::mutex mutex;
    pose value{2,3,4,10,8,5};
    timestamp captured=clock::now();
    double horizon=0;
    bool neuralnet2=false;
    neuralnet_pipeline validation;
    timestamp processed{};
    neuralnet_result latest;
    std::function<void()> after_filter;
    module_status start_tracker(QFrame*) override { return status_ok(); }
    void data(double* out) override { tracking_sample_info sample; data_with_info(out,sample); }
    void data_with_info(double* out,tracking_sample_info& sample) override {
        std::lock_guard lock(mutex);
        const auto now=clock::now(); if (milliseconds(captured,now)>=10) captured=now;
        std::copy(value.begin(),value.end(),out); sample={captured,{}};
        if (neuralnet2) {
            neuralnet_parameters config; config.enabled=true;
            if (processed!=captured) { latest=validation.update(value,captured,now,config); processed=captured; }
            else latest=validation.poll(now);
            sample.captured=latest.captured; sample.neuralnet2=sample.validated=sample.motion_aware=sample.recovery_managed=true;
            sample.holding=latest.holding; sample.recovering=latest.diagnostics.recovery_active;
            sample.motion=latest.diagnostics.motion; sample.prediction_allowed=latest.prediction_allowed;
            sample.estimated_reliability=latest.diagnostics.reliability;
            if (latest.valid) std::copy(latest.value.begin(),latest.value.end(),out);
            else std::fill_n(out,6,std::numeric_limits<double>::quiet_NaN());
        }
    }
    bool center() override { std::lock_guard lock(mutex); if (neuralnet2) { validation.reset(); processed={}; } return false; }
    void set(pose p) { std::lock_guard lock(mutex); value=p; }
    void enhanced_feedback(bool,double applied,double,double) override {
        std::function<void()> action;
        { std::lock_guard lock(mutex); horizon=applied; action=std::exchange(after_filter,{}); }
        if (action) action();
    }
    void after_filter_once(std::function<void()> action) { std::lock_guard lock(mutex); after_filter=std::move(action); }
};
struct sink final : IProtocol {
    std::mutex mutex;
    enhanced::pose value{},raw{};
    unsigned calls=0;
    timestamp last{};
    double max_rate=0;
    module_status initialize() override { return status_ok(); }
    QString game_name() override { return {}; }
    void pose(const double* p,const double* r) override {
        std::lock_guard lock(mutex); const auto now=clock::now();
        if (last!=timestamp{}&&now>last) for (std::size_t i=0;i<6;++i) max_rate=std::max(max_rate,std::abs(p[i]-value[i])/milliseconds(last,now));
        last=now; std::copy_n(p,6,value.begin()); std::copy_n(r,6,raw.begin()); ++calls;
    }
    enhanced::pose read() { std::lock_guard lock(mutex); return value; }
    void reset_rate() { std::lock_guard lock(mutex); max_rate=0; }
    double rate() { std::lock_guard lock(mutex); return max_rate; }
};
struct legacy_filter final : IFilter {
    std::atomic<unsigned> calls=0;
    module_status initialize() override { return status_ok(); }
    void filter(const double* input,double* output) override { ++calls; std::copy_n(input,6,output); }
};
int main(int argc,char** argv)
{
    QApplication app(argc,argv);
    QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeui.ttf")); app.setFont(QFont("Segoe UI",9));
    if (argc<3) return 1;
    QTemporaryDir temporary;
    auto& global=options::globals::detail::global_settings();
    global.qsettings.emplace(temporary.filePath("globals.ini"),QSettings::IniFormat);
    global.qsettings->setValue(OPENTRACK_PROFILE_FILENAME_KEY,"head.ini"); global.mtx.unlock();
    auto& context=options::globals::detail::cur_settings();
    context.pathname="head.ini"; context.qsettings.emplace(temporary.filePath("head.ini"),QSettings::IniFormat); context.mtx.unlock();
    try {
        const QString stage=QString::fromLocal8Bit(argv[1]); const QString mode=QString::fromLocal8Bit(argv[2]);
        const bool adaptive=mode.endsWith("adaptive"); const bool nn2=mode.startsWith("nn2-");
        enhanced_settings settings; settings.adaptive_enabled=true; settings.jitter=0.;
        main_settings main; main.center_at_startup=false; main.centering_mode=center_point; main.a_yaw.zero=3.;
        Mappings mappings(main.all_axis_opts);
        mappings.forall([](Map& m) { m.spline_main.clear(); m.spline_main.add_point(0,0); m.spline_main.add_point(100,100); });
        mappings(3).spline_main.clear(); mappings(3).spline_main.add_point(0,0); mappings(3).spline_main.add_point(10,30); mappings(3).spline_main.add_point(30,120);
        const double mapped=mappings(3).spline_main.get_value_no_save(10);
        require(std::abs(mapped-30)<.1,"nonlinear curve setup failed");
        runtime_libraries libraries;
        auto tracker=std::make_shared<source>(); auto protocol=std::make_shared<sink>(); auto legacy=std::make_shared<legacy_filter>();
        tracker->neuralnet2=nn2;
        libraries.pTracker=tracker; libraries.pProtocol=protocol;
        QLibrary filter_library(QDir(stage).absoluteFilePath("modules/opentrack-filter-enhanced-adaptive"));
        if (adaptive) {
            require(filter_library.load(),"installed adaptive plugin did not load");
            const auto construct=reinterpret_cast<IFilter*(*)()>(filter_library.resolve("GetConstructor"));
            require(construct!=nullptr,"adaptive constructor missing"); libraries.pFilter.reset(construct());
            require(libraries.pFilter->initialize().is_ok()&&libraries.pFilter->guarded_mapping_target(),"adaptive mapping interface unavailable");
        } else libraries.pFilter=legacy;
        TrackLogger logger;
        {
            pipeline pipeline_(mappings,libraries,logger); pipeline_.start(); wait_ms(80);
            const auto baseline=protocol->read();
            require(std::abs(baseline[3]-(mapped+3))<.05,"disabled controls broke curve/offset behavior");
            settings.head_enabled=true; wait_ms(150);
            require(protocol->read()==baseline,"first neutral activation changed existing output");
            protocol->reset_rate(); settings.amplification=100; wait_ms(150);
            require(protocol->rate()<1.6,"amplification setting introduced abrupt output step");
            const auto amplified=protocol->read();
            for (std::size_t i=0;i<6;++i) require(std::abs(amplified[i]-((baseline[i]-(i==3?3.:0.))*4+(i==3?3.:0.)))<.1,"gain applied before curves, twice, or to zero offset");
            const auto d=pipeline_.head_movement_snapshot();
            require(d.enabled&&d.gain==4&&d.raw[3]==10&&d.processed==amplified&&d.submitted>=d.captured,"pipeline diagnostic values/timestamps incorrect");
            settings.per_axis=true; settings.amplification_yaw=0; settings.amplification_x=100; wait_ms(150);
            require(std::abs(protocol->read()[3]-(mapped*.25+3))<.05&&std::abs(protocol->read()[0]-baseline[0]*4)<.05,"per-axis output mapping wrong");
            protocol->reset_rate(); settings.head_enabled=false; wait_ms(150);
            require(protocol->rate()<1.6,"disabling controls introduced abrupt output step");
            require(protocol->read()==baseline,"disable did not restore original pipeline");
            settings.per_axis=false; settings.amplification=42; settings.speed=0; settings.head_enabled=true; wait_ms(150);
            const auto response=pipeline_.head_movement_snapshot();
            require(response.responsiveness==.125&&response.fallback_response&&response.estimated_lag_ms,"effective mapped responsiveness diagnostics missing");
            require(*settings.responsiveness==1.&&*settings.rotation_smoothing==1.,"Speed modified persisted adaptive settings");
            if (!adaptive) require(legacy->calls>100,"fallback stopped keeping original filter warm");
            tracker->set({2,3,4,20,8,5}); wait_ms(35);
            require(protocol->read()[3]<mappings(3).spline_main.get_value_no_save(20)+3-.1,"slow response did not smooth real pipeline output");
            wait_ms(300);
            // Issue the request after this tick's centering stage. It must survive
            // until the next tick rather than being cleared unconditionally.
            tracker->after_filter_once([&pipeline_] { pipeline_.set_center(true); }); wait_ms(60);
            require(std::abs(protocol->read()[3]-3)<.05&&std::abs(protocol->read()[0])<.05,"recenter retained old response state");
            settings.prediction_enabled=true; settings.prediction_ms=8.; settings.speed=100;
            for (int i=1;i<=20;++i) { tracker->set({2,3,4,20+i*.5,8,5}); wait_ms(10); }
            { std::lock_guard lock(tracker->mutex); require(tracker->horizon==8.,"Speed changed prediction horizon"); }
            pipeline_.set_zero(true); wait_ms(20); require(protocol->read()[3]==3.,"controls amplified fixed zero output"); pipeline_.set_zero(false);
            const auto original_points=mappings(3).spline_main.get_points();
            mapping_dialog dialog(mappings);
            dialog.head_diagnostics=[&pipeline_] { return std::optional{pipeline_.head_movement_snapshot()}; };
            dialog.show();
            auto* tabs=dialog.findChild<QTabWidget*>("tabWidget"); auto* curve=dialog.findChild<spline_widget*>("rxconfig");
            require(tabs&&tabs->count()==6&&curve,"existing Mapping editor damaged"); tabs->setCurrentIndex(0); wait_ms(40);
            auto* gain=dialog.findChild<QSlider*>("headAmplification"); auto* speed=dialog.findChild<QSlider*>("headSpeed");
            auto* enabled=dialog.findChild<QCheckBox*>("headControlsEnabled");
            require(gain&&speed&&enabled,"mapping controls missing");
            speed->setValue(55); gain->setValue(100); wait_ms(230);
            require(std::abs(curve->adjusted_output_for_input(10)-mapped*4)<.1,"curve overlay disagrees with mapped output gain");
            require(curve->adjusted_output_for_input(30)==180,"overlay does not saturate at max output");
            require(mappings(3).spline_main.get_points()==original_points,"amplification changed curve control points");
            const auto snapshot_name=adaptive ? "head-controls-mapping-adaptive.png" : "head-controls-mapping-fallback.png";
            require(dialog.grab().save(QDir(stage).absoluteFilePath(snapshot_name)),"mapping controls screenshot failed");
            auto* per_axis=dialog.findChild<QCheckBox*>("headPerAxis");
            require(per_axis,"advanced UI missing");
            per_axis->setChecked(true); wait_ms(230);
            require(!dialog.findChild<QSlider*>("headAmplification")->isEnabled(),"advanced axis overrides left global slider active");
            require(dialog.grab().save(QDir(stage).absoluteFilePath(QString("head-controls-mapping-%1-advanced.png").arg(adaptive?"adaptive":"fallback"))),"advanced preview failed");
            per_axis->setChecked(false);
            auto* reset=dialog.findChild<QPushButton*>("headControlsReset");
            require(reset&&gain&&speed&&enabled,"UI controls missing");
            gain->setValue(12); speed->setValue(30); reset->click();
            require(*settings.amplification==42&&*settings.speed==55&&!*settings.per_axis&&enabled->isChecked(),"UI reset does not restore neutral settings");
            dialog.accept(); gain->setValue(0); speed->setValue(100); mappings(3).spline_main.move_point(1,{10,20}); dialog.reject(); wait_ms(10);
            require(gain->value()==42&&speed->value()==55&&mappings(3).spline_main.get_points()==original_points,"mapping save/cancel destroyed points or settings");
            enabled->setChecked(false); require(!*settings.head_enabled,"UI master switch not bound");

            main.a_yaw.invert_pre=true; main.a_yaw.invert_post=true; main.a_yaw.altp=true;
            mappings(3).spline_alt.clear(); mappings(3).spline_alt.add_point(0,0); mappings(3).spline_alt.add_point(10,15); mappings(3).spline_alt.add_point(30,30);
            settings.head_enabled=true; settings.amplification=100; settings.prediction_enabled=false;
            tracker->set({0,0,0,0,0,0}); wait_ms(40); pipeline_.set_center(true); wait_ms(40);
            require(std::abs(protocol->read()[3]-3)<.05,"center request was lost before inversion check");
            tracker->set({2,3,4,10,8,5}); wait_ms(250);
            const double alternate=mappings(3).spline_alt.get_value_no_save(-10);
            std::cout << "Inversion: output=" << protocol->read()[3] << " alternate=" << alternate
                << " raw=" << pipeline_.head_movement_snapshot().raw[3]
                << " live_gain=" << pipeline_.head_movement_snapshot().gains[3]
                << " enabled=" << *settings.head_enabled << " speed=" << *settings.speed
                << " pre=" << *main.a_yaw.invert_pre << " post=" << *main.a_yaw.invert_post
                << " asymmetric=" << *main.a_yaw.altp << '\n';
            require(std::abs(protocol->read()[3]-(-(alternate*4-3)))<.5,"asymmetric curve or pre/post inversion sign incorrect");
            main.a_yaw.invert_pre=false; main.a_yaw.invert_post=false; main.a_yaw.altp=false;
            tracker->set({50,50,50,40,40,40}); wait_ms(300);
            require(protocol->read()[0]<=75&&protocol->read()[3]<=183&&protocol->read()[4]<=90,"actual protocol output exceeded amplified movement limits");
            if (nn2) {
                settings.prediction_enabled=true;
                tracker->set({std::numeric_limits<double>::quiet_NaN(),0,0,0,0,0}); wait_ms(20);
                { std::lock_guard lock(tracker->mutex); require(tracker->horizon==0,"real pipeline predicted held or invalid NN2 pose"); }
                wait_ms(160);
                { std::lock_guard lock(tracker->mutex); require(!tracker->latest.valid && tracker->latest.diagnostics.state==tracking_state::lost,"real pipeline retained lost validity"); }
                // The prior target is (50,50,50) cm: at 100 cm/s its 86.6 cm
                // return needs at least 866 ms plus confirmation, independent of FPS.
                tracker->set({0,0,0,-30,0,0}); wait_ms(1250);
                { std::lock_guard lock(tracker->mutex); require(tracker->latest.valid && tracker->latest.diagnostics.state==tracking_state::tracking,"actual pipeline reacquisition failed"); }
                pipeline_.set_center(true); wait_ms(60);
                require(std::abs(protocol->read()[3]-3)<.05,"NN2 recenter did not reset recovery/filter/prediction/mapping");
            }
        }
        std::cout << (adaptive?"adaptive":"fallback") << " actual pipeline + mapped response + nonlinear/asymmetric curves + saturation + inversion + Mapping UI: PASS\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
