#include "enhanced/options.hpp"
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
    context.mtx.unlock();
    try {
        enhanced_settings settings;
        if (*settings.adaptive_enabled || *settings.prediction_enabled) throw std::runtime_error("new behavior defaults enabled");
        settings.prediction_ms = 12.5; settings.prediction_enabled = true; settings.rotation_smoothing = 2.5;
        settings.b->save();
        context.qsettings->sync();
        QSettings disk(path, QSettings::IniFormat);
        if (disk.value("enhanced-tracking/prediction-ms").toDouble() != 12.5 || !disk.value("enhanced-tracking/prediction-enabled").toBool())
            throw std::runtime_error("settings did not reach INI file");
        settings.prediction_ms = 1.; settings.prediction_enabled = false;
        settings.b->reload();
        if (*settings.prediction_ms != 12.5 || !*settings.prediction_enabled || *settings.rotation_smoothing != 2.5)
            throw std::runtime_error("cancel/reload did not restore saved values");
        std::cout << "settings persistence: PASS\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
