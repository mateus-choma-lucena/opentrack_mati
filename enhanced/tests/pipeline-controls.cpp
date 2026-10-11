#include "logic/pipeline.hpp"
#include "gui/options-dialog.hpp"
#include "enhanced/head-controls-ui.hpp"
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
    module_status start_tracker(QFrame*) override { return status_ok(); }
    void data(double* out) override { tracking_sample_info sample; data_with_info(out,sample); }
    void data_with_info(double* out,tracking_sample_info& sample) override {
        std::lock_guard lock(mutex);
        const auto now=clock::now(); if (milliseconds(captured,now)>=10) captured=now;
        std::copy(value.begin(),value.end(),out); sample={captured,{}};
    }
    void set(pose p) { std::lock_guard lock(mutex); value=p; }
    void enhanced_feedback(bool,double applied,double,double) override { std::lock_guard lock(mutex); horizon=applied; }
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
        const QString stage=QString::fromLocal8Bit(argv[1]); const bool adaptive=QString::fromLocal8Bit(argv[2])=="adaptive";
        enhanced_settings settings; settings.adaptive_enabled=true; settings.jitter=0.;
        main_settings main; main.center_at_startup=false; main.centering_mode=center_point; main.a_yaw.zero=3.;
        Mappings mappings(main.all_axis_opts);
        mappings.forall([](Map& m) { m.spline_main.clear(); m.spline_main.add_point(0,0); m.spline_main.add_point(100,100); });
        mappings(3).spline_main.clear(); mappings(3).spline_main.add_point(0,0); mappings(3).spline_main.add_point(10,30); mappings(3).spline_main.add_point(30,120);
        const double mapped=mappings(3).spline_main.get_value_no_save(10);
        require(std::abs(mapped-30)<.1,"nonlinear curve setup failed");
        runtime_libraries libraries;
        auto tracker=std::make_shared<source>(); auto protocol=std::make_shared<sink>(); auto legacy=std::make_shared<legacy_filter>();
        libraries.pTracker=tracker; libraries.pProtocol=protocol;
        QLibrary filter_library(QDir(stage).absoluteFilePath("modules/opentrack-filter-enhanced-adaptive"));
        if (adaptive) {
            require(filter_library.load(),"installed adaptive plugin did not load");
            const auto construct=reinterpret_cast<IFilter*(*)()>(filter_library.resolve("GetConstructor"));
            require(construct!=nullptr,"adaptive constructor missing"); libraries.pFilter.reset(construct());
            require(libraries.pFilter->initialize().is_ok()&&libraries.pFilter->supports_head_response(),"adaptive shared interface unavailable");
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
            require(response.responsiveness==.125&&(adaptive?response.shared_response:response.fallback_response)&&response.estimated_lag_ms,"effective responsiveness diagnostics missing");
            require(*settings.responsiveness==1.&&*settings.rotation_smoothing==1.,"Speed modified persisted adaptive settings");
            if (!adaptive) require(legacy->calls>100,"fallback stopped keeping original filter warm");
            tracker->set({2,3,4,20,8,5}); wait_ms(35);
            require(protocol->read()[3]<mappings(3).spline_main.get_value_no_save(20)+3-.1,"slow response did not smooth real pipeline output");
            wait_ms(300); pipeline_.set_center(true); wait_ms(60);
            require(std::abs(protocol->read()[3]-3)<.05&&std::abs(protocol->read()[0])<.05,"recenter retained old response state");
            settings.prediction_enabled=true; settings.prediction_ms=8.; settings.speed=100;
            for (int i=1;i<=20;++i) { tracker->set({2,3,4,20+i*.5,8,5}); wait_ms(10); }
            { std::lock_guard lock(tracker->mutex); require(tracker->horizon==8.,"Speed changed prediction horizon"); }
            pipeline_.set_zero(true); wait_ms(20); require(protocol->read()[3]==3.,"controls amplified fixed zero output"); pipeline_.set_zero(false);
            std::unique_ptr<ITrackerDialog> tracker_dialog; std::unique_ptr<IProtocolDialog> protocol_dialog; std::unique_ptr<IFilterDialog> filter_dialog;
            options_dialog dialog(tracker_dialog,protocol_dialog,filter_dialog,[](bool){});
            dialog.head_diagnostics=[&pipeline_] { return std::optional{pipeline_.head_movement_snapshot()}; };
            dialog.resize(900,700); dialog.show();
            auto* tabs=dialog.findChild<QTabWidget*>("tabWidget"); auto* page=dialog.findChild<QScrollArea*>("headMovementControlsPage");
            require(tabs&&page,"global controls page missing"); tabs->setCurrentWidget(page); wait_ms(40);
            const auto snapshot_name=adaptive ? "head-controls-global-adaptive.png" : "head-controls-global-fallback.png";
            require(dialog.grab().save(QDir(stage).absoluteFilePath(snapshot_name)),"global controls screenshot failed");
            auto* per_axis=dialog.findChild<QCheckBox*>("headPerAxis"); auto* live=dialog.findChild<QGroupBox*>("headLiveDiagnostics");
            require(per_axis&&live,"advanced/diagnostic UI missing");
            per_axis->setChecked(true); wait_ms(20);
            require(!dialog.findChild<QSlider*>("headAmplification")->isEnabled(),"advanced axis overrides left global slider active");
            require(dialog.grab().save(QDir(stage).absoluteFilePath(QString("head-controls-global-%1-advanced.png").arg(adaptive?"adaptive":"fallback"))),"advanced preview failed");
            per_axis->setChecked(false); live->setChecked(true); wait_ms(275);
            require(dialog.grab().save(QDir(stage).absoluteFilePath(QString("head-controls-global-%1-diagnostics.png").arg(adaptive?"adaptive":"fallback"))),"diagnostic preview failed");
            auto* reset=dialog.findChild<QPushButton*>("headControlsReset"); auto* gain=dialog.findChild<QSlider*>("headAmplification");
            auto* speed=dialog.findChild<QSlider*>("headSpeed"); auto* enabled=dialog.findChild<QCheckBox*>("headControlsEnabled");
            require(reset&&gain&&speed&&enabled,"UI controls missing");
            gain->setValue(12); speed->setValue(30); reset->click();
            require(*settings.amplification==42&&*settings.speed==55&&!*settings.per_axis&&enabled->isChecked(),"UI reset does not restore neutral settings");
            dialog.save(); gain->setValue(0); speed->setValue(100); dialog.reload(); wait_ms(10);
            require(gain->value()==42&&speed->value()==55,"global UI save/cancel binding failed");
            enabled->setChecked(false); require(!*settings.head_enabled,"UI master switch not bound");
        }
        std::cout << (adaptive?"adaptive":"fallback") << " real pipeline + installed filter + curves + six axes + recenter + prediction + UI: PASS\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
