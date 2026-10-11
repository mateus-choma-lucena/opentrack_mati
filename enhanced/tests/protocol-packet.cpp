#include "proto-ft/ftnoir_protocol_ft.h"
#include "logic/mappings.hpp"
#include "enhanced/head-controls.hpp"
#include <QApplication>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
static void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc,char** argv)
{
    QApplication app(argc,argv); QTemporaryDir temporary;
    auto& global=options::globals::detail::global_settings(); global.qsettings.emplace(temporary.filePath("global.ini"),QSettings::IniFormat);
    global.qsettings->setValue(OPENTRACK_PROFILE_FILENAME_KEY,"packet.ini"); global.mtx.unlock();
    auto& context=options::globals::detail::cur_settings(); context.pathname="packet.ini";
    context.qsettings.emplace(temporary.filePath("packet.ini"),QSettings::IniFormat); context.mtx.unlock();
    try {
        main_settings main; Mappings mappings(main.all_axis_opts);
        mappings.forall([](Map& map) { map.spline_main.clear(); map.spline_main.add_point(0,0); map.spline_main.add_point(30,30); });
        mappings(3).spline_main.clear(); mappings(3).spline_main.add_point(0,0); mappings(3).spline_main.add_point(10,20); mappings(3).spline_main.add_point(30,90);
        enhanced::pose raw{2,-3,4,10,-8,5},mapped{},limits{};
        for (int i=0;i<6;++i) { mapped[i]=mappings(i).spline_main.get_value_no_save(raw[i]); limits[i]=mappings(i).opts.max_clamp_y(); }
        enhanced::head_movement_controls controls; const auto start=enhanced::clock::now();
        controls.configure(start,{true,false,100,55}); controls.configure(start+std::chrono::milliseconds(120),{true,false,100,55});
        const auto output=controls.process(mapped,mapped,start+std::chrono::milliseconds(120),start,limits);
        check(std::abs(output[3]-80)<.01,"nonlinear mapped target incorrect");
        shm_wrapper reader(FREETRACK_HEAP,FREETRACK_MUTEX,sizeof(FTHeap)); check(reader.success(),"isolated protocol memory missing");
        auto* heap=static_cast<FTHeap*>(reader.ptr()); heap->GameID=0;
        freetrack producer; // No initialize(): no registry writes or dummy TrackIR process.
        producer.pose(output.data(),raw.data());
        constexpr double radians=3.14159265358979323846/180.;
        check(std::abs(heap->data.X-output[0]*10)<1e-4&&std::abs(heap->data.Y-output[1]*10)<1e-4&&std::abs(heap->data.Z-output[2]*10)<1e-4,"FreeTrack/NPClient translation differs from mapped target");
        check(std::abs(heap->data.Yaw+output[3]*radians)<1e-5&&std::abs(heap->data.Pitch+output[4]*radians)<1e-5&&std::abs(heap->data.Roll-output[5]*radians)<1e-5,"FreeTrack/NPClient rotation sign/gain differs from adjusted curve");
        check(std::abs(heap->data.RawYaw+raw[3]*radians)<1e-5&&std::abs(heap->data.RawX-raw[0]*10)<1e-5,"new gain changed raw protocol fields");
        std::cout << "Unchanged production FreeTrack/NPClient packet: six mapped axes, radians/mm, signs and raw fields PASS\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
