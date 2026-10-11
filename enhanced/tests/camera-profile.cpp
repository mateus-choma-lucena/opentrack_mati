#include "video/camera.hpp"
#include "options/options.hpp"
#include "enhanced/calibration.hpp"
#include <QApplication>
#include <QDir>
#include <QLibrary>
#include <QTemporaryDir>
#include <iostream>
#include <windows.h>
using namespace enhanced;
static double cpu_ms() {
    FILETIME created{},exited{},kernel{},user{};
    GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user);
    const auto value=[](FILETIME t){return (std::uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
    return (value(kernel)+value(user))/10000.;
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    QTemporaryDir temporary;
    auto& global=options::globals::detail::global_settings();
    global.qsettings.emplace(temporary.filePath("globals.ini"),QSettings::IniFormat);global.mtx.unlock();
    auto& settings=options::globals::detail::cur_settings();
    settings.qsettings.emplace(temporary.filePath("camera.ini"),QSettings::IniFormat);settings.mtx.unlock();
    QLibrary backend(QDir(QCoreApplication::applicationDirPath()).filePath("modules/opentrack-video-ps3eye.dll"));
    backend.setLoadHints(QLibrary::PreventUnloadHint);
    if(!backend.load()){std::cerr<<backend.errorString().toStdString()<<'\n';return 1;}
    const auto names=video::camera_names();
    for(const auto& name:names)std::cout<<"Camera: "<<name.toStdString()<<'\n';
    auto camera=video::make_camera_("PS3 Eye open driver");
    if(!camera){std::cerr<<"Physical PS3 Eye unavailable to the production backend\n";return 2;}
    std::cout<<"Physical PS3 Eye capture only; production backend. No frames saved; no inference or display latency claim.\n";
    for(int cycle=0;cycle<2;++cycle) {
        video::camera_impl::info requested;requested.width=640;requested.height=480;requested.fps=75;requested.num_channels=1;requested.quiet=true;
        const auto startup=clock::now();
        if(!camera->start(requested)){camera->stop();std::cerr<<"640x480@75 start failed (cycle "<<cycle<<")\n";return 3;}
        std::cout<<"cycle="<<cycle<<" negotiated="<<requested.width<<'x'<<requested.height<<'@'<<requested.fps<<" startup_ms="<<milliseconds(startup,clock::now())<<'\n';
        if(requested.width!=640 || requested.height!=480 || requested.fps!=75){camera->stop();return 4;}
        std::vector<double> intervals,calls;timestamp first{},previous{},last{};std::uint64_t dropped=0;
        const auto cpu_start=cpu_ms();
        for(int i=0;i<600;++i) {
            const auto before=clock::now();const auto [frame,valid]=camera->get_frame();const auto after=clock::now();
            if(!valid || !frame.data || frame.width!=640 || frame.height!=480){camera->stop();std::cerr<<"Frame missing at "<<i<<'\n';return 5;}
            if(!i)first=after;else intervals.push_back(milliseconds(previous,after));previous=last=after;
            calls.push_back(milliseconds(before,after));dropped=frame.dropped_frames.value_or(0);
        }
        const double elapsed=milliseconds(first,last),cpu=cpu_ms()-cpu_start;
        std::cout<<"frames=600 delivered_fps="<<599000./elapsed<<" dropped="<<dropped<<" cpu_ms="<<cpu<<" one_core_cpu_percent="<<100*cpu/elapsed<<'\n';
        for(const auto& [name,times]:{std::pair{"delivery_interval_ms",intervals},std::pair{"blocking_get_frame_ms",calls}})
            std::cout<<name<<" P50="<<calibration::percentile(times,.5)<<" P95="<<calibration::percentile(times,.95)<<" P99="<<calibration::percentile(times,.99)<<'\n';
        const auto stop=clock::now();camera->stop();std::cout<<"stop_ms="<<milliseconds(stop,clock::now())<<" is_open="<<camera->is_open()<<'\n';
        if(camera->is_open())return 6;
    }
    return 0;
}
