#include "api/plugin-api.hpp"
#include "video/camera.hpp"
#include "enhanced/options.hpp"
#include <QApplication>
#include <QLibrary>
#include <QTemporaryDir>
#include <QDir>
#include <QTimer>
#include <QEventLoop>
#include <QPixmap>
#include <QFontDatabase>
#include <QFont>
#include <QTabWidget>
#include <QProcess>
#include <QFile>
#include <QSlider>
#include <QCheckBox>
#include <QGroupBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QPushButton>
#include <QFileDialog>
#include <iostream>
#include <thread>
#include <atomic>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
std::atomic<unsigned> starts = 0, delivered = 0;
struct fake_camera final : video::impl::camera {
    std::array<unsigned char, 640*480> image{};
    video::frame frame;
    bool open = false;
    bool start(info& args) override { ++starts; args.width=640; args.height=480; args.fps=75; open=true; return true; }
    void stop() override { open=false; }
    bool is_open() override { return open; }
    std::tuple<const video::frame&,bool> get_frame() override {
        std::this_thread::sleep_for(std::chrono::microseconds(13333));
        ++delivered;
        if (starts == 1 && delivered > 30) { open=false; return {frame,false}; }
        frame.data=image.data(); frame.width=640; frame.height=480; frame.channels=1;
        return {frame,true};
    }
    bool show_dialog() override { return false; }
};
struct fake_backend final : video::impl::camera_ {
    std::vector<QString> camera_names() const override { return {"Enhanced synthetic camera"}; }
    std::unique_ptr<video::impl::camera> make_camera(const QString&) override { return std::make_unique<fake_camera>(); }
    bool show_dialog(const QString&) override { return false; }
    bool can_show_dialog(const QString&) override { return false; }
};
void wait_ms(int duration) { QEventLoop loop; QTimer::singleShot(duration,&loop,&QEventLoop::quit); loop.exec(); }
}

int main(int argc, char** argv)
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    std::cerr << "Initializing Qt\n";
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);
    QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/segoeui.ttf"));
    app.setFont(QFont(QStringLiteral("Segoe UI"), 9));
    std::cerr << "Initializing isolated settings\n";
    if (argc<2) return 1;
    const QString stage = QString::fromLocal8Bit(argv[1]);
    const QString mode = argc<3 ? "latest" : QString::fromLocal8Bit(argv[2]);
    if (mode == "missing-runtime")
    {
        QTemporaryDir sandbox;
        const auto executable = sandbox.filePath("enhanced-runtime-check.exe");
        if (!QFile::copy(QCoreApplication::applicationFilePath(), executable)) return 1;
        QProcess child;
        child.setProcessChannelMode(QProcess::ForwardedChannels);
        child.start(executable, {stage, "expect-runtime-failure"});
        if (!child.waitForStarted(3000) || !child.waitForFinished(10000)) { child.kill(); child.waitForFinished(); return 1; }
        return child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0 ? 0 : 1;
    }
    const bool nn2 = mode=="nn2-latest" || mode=="nn2-serial";
    const bool low_latency = mode == "latest" || mode=="nn2-latest";
    QTemporaryDir temporary;
    auto& global = options::globals::detail::global_settings();
    global.qsettings.emplace(temporary.filePath("globals.ini"),QSettings::IniFormat);
    global.qsettings->setValue(OPENTRACK_PROFILE_FILENAME_KEY,"synthetic.ini"); global.mtx.unlock();
    auto& context = options::globals::detail::cur_settings();
    context.pathname="synthetic.ini"; context.qsettings.emplace(temporary.filePath("synthetic.ini"),QSettings::IniFormat); context.mtx.unlock();
    const auto neural_settings = options::make_bundle("neuralnet-tracker");
    neural_settings->store_kv("camera-name",QString("Enhanced synthetic camera"));
    neural_settings->store_kv("enhanced-low-latency",low_latency);
    neural_settings->store_kv("enhanced-reconnect",true);
    neural_settings->store_kv("enhanced-max-frame-age-ms",200.);
    neural_settings->store_kv("enhanced-diagnostics",true);
    neural_settings->store_kv("force-fps",3); // Existing NeuralNet enum: fps_75
    neural_settings->store_kv("force-resolution",2); // Existing 640x480 mode
    neural_settings->store_kv("nn2-enabled",nn2);
    neural_settings->store_kv("posenet-file",QDir(stage).absoluteFilePath("modules/models/head-pose-0.5-small.onnx"));
    video::impl::register_camera(std::make_unique<fake_backend>());
    std::cerr << "Loading installed NeuralNet plugin\n";
    QLibrary library(QDir(stage).absoluteFilePath("modules/opentrack-tracker-neuralnet"));
    if (!library.load()) { std::cerr << library.errorString().toStdString(); return 1; }
    const auto construct = reinterpret_cast<ITracker*(*)()>(library.resolve("GetConstructor"));
    const auto dialog_factory = reinterpret_cast<ITrackerDialog*(*)()>(library.resolve("GetDialog"));
    if (!construct || !dialog_factory) return 1;
    std::unique_ptr<ITracker> tracker(construct());
    std::cerr << "Starting synthetic camera\n";
    QFrame frame; frame.resize(320,240);
    const auto status=tracker->start_tracker(&frame);
    if (mode == "expect-runtime-failure")
    {
        if (status.is_ok()) { std::cerr << "missing runtime unexpectedly initialized"; return 1; }
        std::cout << "Missing modules/onnxruntime.dll: clean initialization error, no abort: PASS\n";
        return 0;
    }
    if (!status.is_ok()) { std::cerr << status.error.toStdString(); return 1; }
    wait_ms(1200);
    const auto diagnostic=tracker->performance_snapshot();
    if (!diagnostic || !diagnostic->captured || !diagnostic->processed || !diagnostic->inference_ms || diagnostic->reconnects!=1 || starts!=2 ||
        diagnostic->width!=640 || diagnostic->height!=480 || diagnostic->requested_fps!=75)
    { std::cerr << "capture/inference/reconnect integration failed"; return 1; }
    std::cout << "nn2=" << nn2 << ' ' << (low_latency ? "latest" : "serial") << " captured=" << diagnostic->captured << " processed=" << diagnostic->processed
        << " replacements=" << diagnostic->overwritten << " inference_ms=" << *diagnostic->inference_ms
        << " p50_host_ms=" << diagnostic->p50_ms.value_or(-1) << " p95_host_ms=" << diagnostic->p95_ms.value_or(-1)
        << " p99_host_ms=" << diagnostic->p99_ms.value_or(-1) << " capture_fps=" << diagnostic->capture_fps.value_or(-1)
        << " inference_fps=" << diagnostic->inference_fps.value_or(-1) << '\n';
    double pose[6]{}; tracking_sample_info sample;
    tracker->data_with_info(pose,sample);
    if (!std::all_of(std::begin(pose),std::end(pose),[](double x) { return std::isnan(x); }))
    { std::cerr << "blank synthetic frame should report tracking loss"; return 1; }
    if (nn2 && (!sample.neuralnet2 || sample.prediction_allowed || sample.confidence || !sample.estimated_reliability ||
        diagnostic->neuralnet.state!=enhanced::tracking_state::lost || diagnostic->neuralnet.holding))
    { std::cerr << "NeuralNet 2.0 loss/reliability publication failed"; return 1; }
    if (nn2) {
        tracker->center();
        for (int i=0;i<10;++i) {
            neural_settings->store_kv("nn2-enabled",i%2==0); wait_ms(15);
            tracker->data_with_info(pose,sample);
            if (!std::all_of(std::begin(pose),std::end(pose),[](double x) { return std::isnan(x); })) return 1;
        }
        neural_settings->store_kv("nn2-enabled",true); wait_ms(50);
    }
    std::unique_ptr<ITrackerDialog> dialog(dialog_factory());
    dialog->register_tracker(tracker.get()); dialog->resize(780,800); dialog->show(); wait_ms(300);
    dialog->grab().save(QDir(stage).absoluteFilePath("enhanced-settings-preview.png"));
    if (auto* tabs = dialog->findChild<QTabWidget*>("enhancedTabs"))
    {
        if (tabs->count()!=6 || !dialog->findChild<QCheckBox*>("neuralnet2Enabled") || dialog->findChild<QWidget*>("headAmplification"))
        { std::cerr << "Mapping controls duplicated in tracker dialog"; return 1; }
        for (int index=0; index<tabs->count(); ++index)
        {
            tabs->setCurrentIndex(index); wait_ms(20);
            if (!dialog->grab().save(QDir(stage).absoluteFilePath(QString("enhanced-settings-preview-%1.png").arg(index)))) return 1;
        }
        tabs->setCurrentIndex(1);
        auto* advanced=dialog->findChild<QGroupBox*>("neuralnet2Advanced");
        if (!advanced) return 1;
        advanced->setChecked(true); wait_ms(20);
        auto* scroll=qobject_cast<QScrollArea*>(tabs->widget(1));
        if (!scroll) return 1;
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        wait_ms(20);
        if (!dialog->grab().save(QDir(stage).absoluteFilePath("enhanced-settings-preview-nn2-advanced.png"))) return 1;
        tabs->setCurrentIndex(tabs->count()-1);
        const auto filename=temporary.filePath("diagnostics.csv");
        QTimer::singleShot(50,[filename] {
            for (auto* widget:QApplication::topLevelWidgets()) if (auto* file=qobject_cast<QFileDialog*>(widget)) {
                file->selectFile(filename); QMetaObject::invokeMethod(file,"accept",Qt::DirectConnection);
            }
        });
        auto* export_csv=dialog->findChild<QPushButton*>("exportDiagnosticsCsv");
        if (!export_csv) return 1;
        export_csv->click();
        QFile csv(filename); if (!csv.open(QIODevice::ReadOnly)) return 1;
        const auto lines=csv.readAll().trimmed().split('\n');
        if (lines.size()!=2) return 1;
        const auto header=lines[0].split(','), values=lines[1].split(',');
        const auto enabled_column=header.indexOf("nn2_enabled");
        if (enabled_column<0 || header.size()!=values.size() || values[enabled_column]!=(nn2 ? "1" : "0") ||
            !header.contains("nn2_estimated_reliability") || !header.contains("nn2_raw_timestamp_ms") || !header.contains("nn2_rejected_total"))
        { std::cerr << "diagnostics CSV fields/value alignment incorrect"; return 1; }
        scroll=qobject_cast<QScrollArea*>(tabs->widget(tabs->count()-1));
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum()); wait_ms(20);
        if (!dialog->grab().save(QDir(stage).absoluteFilePath("enhanced-settings-preview-nn2-diagnostics.png"))) return 1;
    }
    else { std::cerr << "Enhanced settings tabs missing"; return 1; }
    dialog->unregister_tracker(); dialog.reset(); tracker.reset();
    std::cout << "Installed NeuralNet + synthetic capture/reconnect + diagnostics + Qt UI: PASS\n";
    return 0;
}
