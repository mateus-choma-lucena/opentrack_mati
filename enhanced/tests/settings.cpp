#include "enhanced/options.hpp"
#include "enhanced/neuralnet2-options.hpp"
#include "enhanced/experimental-options.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("OpenTrack Enhanced Tests");
    app.setApplicationName("settings-persistence");
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    auto& global = options::globals::detail::global_settings();
    global.qsettings.emplace(directory.filePath("globals.ini"), QSettings::IniFormat);
    global.qsettings->setValue(OPENTRACK_PROFILE_FILENAME_KEY, "test.ini");
    global.mtx.unlock();
    const auto path = directory.filePath("test.ini");
    auto& context = options::globals::detail::cur_settings();
    context.pathname = "test.ini";
    context.qsettings.emplace(path, QSettings::IniFormat);
    context.qsettings->setValue("neuralnet-tracker/camera-name", "Existing PS3 Eye profile");
    context.qsettings->setValue("legacy-plugin/private-setting", 1234);
    context.mtx.unlock();
    try {
        enhanced_settings settings;
        experimental_settings experiment;
        if(*experiment.flow_mode || *experiment.analyzer || *experiment.recording || *settings.dynamic_prediction) throw std::runtime_error("experiments enabled in old profiles");
        experiment.flow_mode=1;experiment.flow_quality=2;experiment.fusion_strength=.6;experiment.analyzer=true;experiment.recording_seconds=20.;experiment.b->save();
        settings.dynamic_prediction=true;settings.dynamic_max_ms=11.;
        neuralnet2_settings nn2;
        if (*nn2.enabled || !*nn2.validation || !*nn2.recovery || *nn2.hold_ms!=50.)
            throw std::runtime_error("NeuralNet 2.0 defaults unsafe for old profiles");
        nn2.enabled=true; nn2.hold_ms=35.; nn2.angular_velocity=900.; nn2.b->save();
        if (*settings.adaptive_enabled || *settings.prediction_enabled || *settings.head_enabled || *settings.amplification!=42 || *settings.speed!=55)
            throw std::runtime_error("new behavior defaults not neutral/disabled");
        settings.prediction_ms = 12.5; settings.prediction_enabled = true; settings.rotation_smoothing = 2.5;
        settings.head_enabled=true; settings.amplification=80; settings.speed=20; settings.per_axis=true;
        settings.amplification_x=0; settings.amplification_y=10; settings.amplification_z=20;
        settings.amplification_yaw=100; settings.amplification_pitch=90; settings.amplification_roll=80;
        settings.b->save();
        context.qsettings->sync();
        QSettings disk(path, QSettings::IniFormat);
        if (!disk.value("neuralnet-tracker/nn2-enabled").toBool() || disk.value("neuralnet-tracker/nn2-hold-ms").toDouble()!=35.)
            throw std::runtime_error("NeuralNet 2.0 additive keys not saved");
        nn2.enabled=false; nn2.hold_ms=0.; nn2.b->reload();
        if (!*nn2.enabled || *nn2.hold_ms!=35. || *nn2.angular_velocity!=900.)
            throw std::runtime_error("NeuralNet 2.0 cancel/reload failed");
        if (disk.value("neuralnet-tracker/camera-name").toString()!="Existing PS3 Eye profile" || disk.value("legacy-plugin/private-setting").toInt()!=1234)
            throw std::runtime_error("saving new settings changed existing profile keys");
        if (disk.value("enhanced-tracking/prediction-ms").toDouble() != 12.5 || !disk.value("enhanced-tracking/prediction-enabled").toBool())
            throw std::runtime_error("settings did not reach INI file");
        if (!disk.value("enhanced-tracking/head-controls-enabled").toBool() || disk.value("enhanced-tracking/head-speed").toInt()!=20
            || disk.value("enhanced-tracking/head-amplification-yaw").toInt()!=100)
            throw std::runtime_error("Head Movement Controls did not persist");
        settings.prediction_ms = 1.; settings.prediction_enabled = false;
        settings.head_enabled=false; settings.amplification=42; settings.speed=55; settings.per_axis=false; settings.amplification_yaw=42;
        settings.b->reload();
        experiment.flow_mode=0;experiment.analyzer=false;experiment.b->reload();
        if(*experiment.flow_mode!=1 || *experiment.flow_quality!=2 || *experiment.fusion_strength!=.6 || !*experiment.analyzer || *experiment.recording_seconds!=20. || !*settings.dynamic_prediction || *settings.dynamic_max_ms!=11.)
            throw std::runtime_error("experimental settings persistence failed");
        if (*settings.prediction_ms != 12.5 || !*settings.prediction_enabled || *settings.rotation_smoothing != 2.5)
            throw std::runtime_error("cancel/reload did not restore saved values");
        if (!*settings.head_enabled || *settings.amplification!=80 || *settings.speed!=20 || !*settings.per_axis
            || *settings.amplification_x!=0 || *settings.amplification_y!=10 || *settings.amplification_z!=20
            || *settings.amplification_yaw!=100 || *settings.amplification_pitch!=90 || *settings.amplification_roll!=80)
            throw std::runtime_error("Head Movement Controls cancel/reload lost saved values");
        std::cout << "settings persistence: PASS\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
