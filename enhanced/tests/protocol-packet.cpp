#include "proto-ft/ftnoir_protocol_ft.h"
#include "logic/mappings.hpp"
#include "enhanced/head-controls.hpp"
#include "enhanced/calibration-profile.hpp"
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
        // Generated styles go through the same production mapping, gain and
        // FreeTrack/NPClient writer used by legacy curves.
        using namespace enhanced::calibration;
        profile stored;
        for(auto style:{response_style::natural,response_style::balanced,response_style::responsive}) {
            record calibrated;
            for(int i=0;i<6;++i){auto& a=calibrated.axis[i];a.positive={i<3?4.:32.,.95,3,226,true};a.negative={i<3?2.:20.,.9,3,226,true};
                a.target_positive=i<3?15.:65.;a.target_negative=i<3?12.:55.;a.output_gain=4;a.style=style;check(generate(a),"packet style generation");}
            check(apply(mappings,calibrated,stored,true),"packet style application");
            for(int direction:{-1,1}) {
                for(int i=0;i<6;++i){raw[i]=(i<3?direction>0?4.:-2.:direction>0?32.:-20.);mapped[i]=(direction<0?mappings(i).spline_alt:mappings(i).spline_main).get_value_no_save(raw[i]);limits[i]=mappings(i).opts.max_clamp_y();}
                enhanced::head_movement_controls generated;generated.configure(start,{true,false,100,55});generated.configure(start+std::chrono::milliseconds(120),{true,false,100,55});
                const auto final=generated.process(mapped,mapped,start+std::chrono::milliseconds(120),start,limits);producer.pose(final.data(),raw.data());
                for(int i=0;i<6;++i)check(std::abs(final[i]-(direction>0?calibrated.axis[i].target_positive:-calibrated.axis[i].target_negative))<.1,"packet endpoint gained twice");
                check(std::abs(heap->data.X-final[0]*10)<1e-4 && std::abs(heap->data.Y-final[1]*10)<1e-4 && std::abs(heap->data.Z-final[2]*10)<1e-4,"smart packet translations");
                check(std::abs(heap->data.Yaw+final[3]*radians)<1e-5 && std::abs(heap->data.Pitch+final[4]*radians)<1e-5 && std::abs(heap->data.Roll-final[5]*radians)<1e-5,"smart packet rotations");
                check(std::abs(heap->data.RawYaw+raw[3]*radians)<1e-5,"smart packet changed raw observation");
            }
        }
        std::cout << "Unchanged production FreeTrack/NPClient packet: six mapped axes, radians/mm, signs and raw fields PASS\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
