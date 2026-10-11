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
#include <iostream>
#include <thread>
#include <atomic>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
std::atomic<unsigned> starts = 0, delivered = 0;
struct fake_camera final : video::impl::camera {
    std::array<unsigned char, 320*240> image{};
    video::frame frame;
    bool open = false;
    bool start(info& args) override { ++starts; args.width=320; args.height=240; args.fps=100; open=true; return true; }
    void stop() override { open=false; }
    bool is_open() override { return open; }
    std::tuple<const video::frame&,bool> get_frame() override {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ++delivered;
        if (starts == 1 && delivered > 30) { open=false; return {frame,false}; }
        frame.data=image.data(); frame.width=320; frame.height=240; frame.channels=1;
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
    const bool low_latency = mode == "latest";
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
    if (!diagnostic || !diagnostic->captured || !diagnostic->processed || !diagnostic->inference_ms || diagnostic->reconnects!=1 || starts!=2)
    { std::cerr << "capture/inference/reconnect integration failed"; return 1; }
    std::cout << (low_latency ? "latest" : "serial") << " captured=" << diagnostic->captured << " processed=" << diagnostic->processed
        << " replacements=" << diagnostic->overwritten << " inference_ms=" << *diagnostic->inference_ms
        << " p95_host_ms=" << diagnostic->p95_ms.value_or(-1) << '\n';
    double pose[6]{}; tracking_sample_info sample;
    tracker->data_with_info(pose,sample);
    if (!std::all_of(std::begin(pose),std::end(pose),[](double x) { return std::isnan(x); }))
    { std::cerr << "blank synthetic frame should report tracking loss"; return 1; }
    std::unique_ptr<ITrackerDialog> dialog(dialog_factory());
    dialog->register_tracker(tracker.get()); dialog->resize(780,800); dialog->show(); wait_ms(300);
    dialog->grab().save(QDir(stage).absoluteFilePath("enhanced-settings-preview.png"));
    if (auto* tabs = dialog->findChild<QTabWidget*>("enhancedTabs"))
        for (int index=0; index<tabs->count(); ++index)
        {
            tabs->setCurrentIndex(index); wait_ms(20);
            if (!dialog->grab().save(QDir(stage).absoluteFilePath(QString("enhanced-settings-preview-%1.png").arg(index)))) return 1;
        }
    dialog->unregister_tracker(); dialog.reset(); tracker.reset();
    std::cout << "Installed NeuralNet + synthetic capture/reconnect + diagnostics + Qt UI: PASS\n";
    return 0;
}
