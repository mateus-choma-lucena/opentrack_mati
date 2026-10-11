#include "enhanced/neuralnet2.hpp"
#include "enhanced/head-controls.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

using namespace enhanced;
static timestamp at(double ms) { return timestamp{}+std::chrono::duration_cast<clock::duration>(std::chrono::duration<double,std::milli>(ms+1000)); }
static void require(bool okay,const char* message) { if (!okay) throw std::runtime_error(message); }
static pose yaw(double v) { pose p{}; p[3]=v; return p; }
static neuralnet_parameters parameters() { neuralnet_parameters p; p.enabled=true; return p; }
static void prime(neuralnet_pipeline& pipeline, neuralnet_parameters p)
{ for (int i=0;i<=15;++i) require(pipeline.update(yaw(0),at(i*13.),at(i*13.+1),p).accepted,"prime rejected"); }
static std::size_t memory()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX info{};
    if (GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&info),sizeof(info))) return info.WorkingSetSize;
#endif
    return 0;
}

int main(int argc,char** argv)
{
    try {
        const std::string test=argc>1 ? argv[1] : "";
        neuralnet_pipeline pipeline; auto p=parameters();
        if (test=="steady") {
            prime(pipeline,p);
            const auto r=pipeline.update(yaw(.02),at(210),at(212),p);
            require(r.accepted && r.value==yaw(.02) && r.diagnostics.motion==motion_state::stationary,"validator added smoothing delay");
            require(r.diagnostics.reliability>.9 && r.diagnostics.reliability<=1,"reliability not normalized");
            require(r.captured==at(210) && r.diagnostics.raw_time==at(210) && r.diagnostics.validated_time==at(210),"timestamps changed");
        } else if (test=="jitter") {
            prime(pipeline,p); adaptive_filter filter; adaptive_parameters fp;
            fp.enabled=fp.motion_aware=fp.validated=fp.recovery_managed=true;
            filter.update(yaw(0),at(195),fp); double squared=0;
            for (int i=1;i<=200;++i) {
                const auto r=pipeline.update(yaw(i%2 ? .1 : -.1),at(195+i*13.),at(196+i*13.),p);
                require(r.accepted && r.diagnostics.motion==motion_state::stationary,"stationary model noise classified as intentional reversal");
                fp.motion=r.diagnostics.motion;
                const auto output=filter.update(r.value,r.captured,fp); squared+=output[3]*output[3];
            }
            require(std::sqrt(squared/200)<.04,"motion-aware adaptive interaction did not protect stationary jitter");
        } else if (test=="fast" || test=="intervals") {
            pipeline.update(yaw(0),at(0),at(1),p); double time=0,angle=0;
            for (int i=0;i<25;++i) {
                const double dt=test=="intervals" ? (i%3==0 ? 7. : i%3==1 ? 22. : 13.) : 13.333;
                time+=dt; angle=std::remainder(angle+600*dt*.001,360.);
                const auto r=pipeline.update(yaw(angle),at(time),at(time+1),p);
                require(r.accepted && std::abs(delta(r.value[3],angle,3))<1e-9,"intentional rapid motion delayed or clamped");
            }
        } else if (test=="stops" || test=="reversal") {
            pipeline.update(yaw(0),at(0),at(1),p);
            auto r=pipeline.update(yaw(6),at(13),at(14),p);
            require(r.diagnostics.motion==motion_state::rapid,"rapid state missing");
            r=pipeline.update(yaw(test=="stops" ? 6 : 0),at(26),at(27),p);
            require(r.accepted && r.diagnostics.motion==(test=="stops" ? motion_state::stopping : motion_state::reversing),"stop/reversal missing");
        } else if (test=="wrap" || test=="quaternion") {
            pipeline.update(yaw(179),at(0),at(1),p);
            auto r=pipeline.update(yaw(-179),at(13),at(14),p);
            require(r.accepted && r.diagnostics.spikes==0,"angular wrap mistaken for spike");
            for (const pose target : {pose{0,0,0,45,30,-20},pose{0,0,0,-120,-60,40},pose{0,0,0,179,89,80}}) {
                pose output{}; neuralnet_rotation::blend(output,target,1);
                require(neuralnet_rotation::distance(output,target)<1e-5,"quaternion coordinate convention changed");
            }
            pose out=yaw(179); neuralnet_rotation::blend(out,yaw(-179),.5);
            require(std::abs(std::abs(out[3])-180)<1e-8,"recovery interpolation takes long arc");
        } else if (test=="spike" || test=="translation") {
            prime(pipeline,p); pose corrupt=yaw(120); if (test=="translation") { corrupt={}; corrupt[0]=25; }
            const auto rejected=pipeline.update(corrupt,at(208),at(209),p);
            require(!rejected.accepted && !rejected.diagnostics.raw_valid && rejected.holding,"spike treated as fresh valid pose");
            require(rejected.captured==at(195) && rejected.diagnostics.validated_time==at(195),"rejected sample replaced valid timestamp");
            const auto next=pipeline.update(yaw(0),at(221),at(222),p);
            require(next.accepted && next.diagnostics.spikes==1,"isolated spike poisoned next measurement");
        } else if (test=="persistent") {
            prime(pipeline,p);
            require(!pipeline.update(yaw(90),at(208),at(209),p).accepted,"first jump should be suspect");
            const auto r=pipeline.update(yaw(92),at(221),at(222),p);
            require(r.accepted && r.value[3]==92,"persistent intentional movement permanently clamped");
        } else if (test=="invalid") {
            prime(pipeline,p);
            for (std::size_t axis=0;axis<6;++axis) {
                pose corrupt{}; corrupt[axis]=axis%2 ? std::numeric_limits<double>::infinity() : std::numeric_limits<double>::quiet_NaN();
                const auto r=pipeline.update(corrupt,at(208+double(axis)*13),at(209+double(axis)*13),p);
                require(!r.accepted && !r.diagnostics.raw_valid,"nonfinite pose accepted");
            }
            require(pipeline.poll(at(400)).diagnostics.rejected==6,"invalid counter inaccurate");
            pose bad{}; bad[2]=1001; require(!pipeline.update(bad,at(410),at(411),p).accepted,"impossible translation accepted");
        } else if (test=="loss" || test=="long-loss") {
            prime(pipeline,p);
            auto r=pipeline.update({},at(208),at(209),p);
            require(r.holding && !r.prediction_allowed && r.diagnostics.state==tracking_state::suspect,"short hold/state missing");
            require(r.captured==at(195) && !r.accepted,"hold manufactured a fresh timestamp");
            r=pipeline.poll(at(246)); require(!r.valid && !r.holding,"hold did not expire without another frame");
            r=pipeline.poll(at(test=="long-loss" ? 10000 : 310));
            require(!r.valid && r.diagnostics.state==tracking_state::lost && r.diagnostics.loss_duration_ms>100,"lost retained a stale valid pose");
        } else if (test=="recovery") {
            prime(pipeline,p); pipeline.update({},at(208),at(209),p); pipeline.poll(at(500));
            auto r=pipeline.update(yaw(90),at(510),at(511),p);
            require(!r.valid && r.accepted && r.diagnostics.state==tracking_state::reacquiring,"reacquisition confirmation missing");
            double previous=0; bool first=true;
            for (int i=1;i<45;++i) {
                r=pipeline.update(yaw(90),at(510+i*13.),at(511+i*13.),p);
                if (r.valid) {
                    require(std::abs(delta(r.value[3],previous,3))<=9.361,"recovery exceeded angular speed bound");
                    if (first) { require(r.value[3]<10,"recovery snapped to new camera pose"); first=false; }
                    previous=r.value[3];
                }
            }
            require(r.diagnostics.state==tracking_state::tracking && r.prediction_allowed && std::abs(r.value[3]-90)<.01,"recovery never converged");
            require(r.diagnostics.reacquisitions==1 && r.diagnostics.loss_duration_ms==0,"reacquisition accounting wrong");
        } else if (test=="reliability") {
            neuralnet_pipeline other; prime(pipeline,p); prime(other,p);
            pipeline.update({},at(208),at(209),p); other.update({},at(208),at(209),p);
            for (int time=220;time<=400;time+=10) pipeline.poll(at(time));
            const auto a=pipeline.poll(at(400)), b=other.poll(at(400));
            require(std::abs(a.diagnostics.reliability-b.diagnostics.reliability)<1e-9 && a.diagnostics.reliability<.1,
                "reliability depends on polling frequency or ignores ongoing loss");
        } else if (test=="gap") {
            prime(pipeline,p);
            const auto r=pipeline.update(yaw(90),at(600),at(601),p);
            require(!r.valid && r.diagnostics.state==tracking_state::reacquiring,"silent capture gap bypassed time-based recovery");
            const auto expired=pipeline.poll(at(800));
            require(!expired.valid && expired.diagnostics.state==tracking_state::lost,"stalled confirmation retained REACQUIRING forever");
            require(!pipeline.update(yaw(90),at(820),at(821),p).valid,"stalled confirmation bypassed fresh reacquisition");
            neuralnet_pipeline unpolled; prime(unpolled,p);
            unpolled.update(yaw(90),at(600),at(601),p);
            const auto restart=unpolled.update(yaw(90),at(900),at(901),p);
            require(!restart.valid && restart.diagnostics.reacquisitions==2,"long reacquisition gap bypassed confirmation without polls");
        } else if (test=="disconnect") {
            prime(pipeline,p);
            auto r=pipeline.update(yaw(0),at(208),at(209),p,false);
            require(!r.valid && !r.holding && r.diagnostics.state==tracking_state::lost,"camera disconnect held or published in-flight inference");
        } else if (test=="recenter") {
            prime(pipeline,p); pipeline.update({},at(208),at(209),p); pipeline.poll(at(400));
            pipeline.update(yaw(90),at(410),at(411),p); pipeline.reset();
            const auto r=pipeline.update(yaw(-45),at(423),at(424),p);
            require(r.accepted && r.valid && r.value[3]==-45 && !r.diagnostics.recovery_active,"recenter retained old recovery anchor");
            require(r.diagnostics.reacquisitions==1,"recenter reset session counters");
            // Also recenter during valid blending: center must use the observed pose,
            // not a halfway blend that would make the subsequent mapped output jump.
            pipeline.update({},at(436),at(437),p); pipeline.poll(at(700));
            pipeline.update(yaw(90),at(710),at(711),p);
            neuralnet_result blending;
            for (int i=1;i<=6;++i) blending=pipeline.update(yaw(90),at(710+i*13.),at(711+i*13.),p);
            require(blending.valid && blending.diagnostics.recovery_active && blending.value[3]<90,"blending recenter setup failed");
            const auto center_pose=pipeline.validated_measurement(at(790));
            require(center_pose && center_pose->first[3]==90,"recenter selected intermediate recovery output");
            pipeline.reset(); pipeline.update(center_pose->first,center_pose->second,at(790),p);
            const auto after=pipeline.update(yaw(90),at(801),at(802),p);
            require(after.valid && std::abs(delta(after.value[3],center_pose->first[3],3))<1e-9,"recenter during blending retained a stale anchor");
        } else if (test=="toggles" || test=="disabled") {
            for (int i=0;i<200;++i) {
                p.enabled=test=="disabled" ? false : i%2==0;
                auto r=pipeline.update(yaw(i%2 ? 170 : -170),at(i*13.),at(i*13.+1),p);
                require(r.valid && r.value==yaw(i%2 ? 170 : -170),"enable/disable reused stale validator history");
            }
        } else if (test=="timestamp") {
            prime(pipeline,p);
            require(!pipeline.update(yaw(1),at(195),at(209),p).accepted,"duplicate timestamp accepted");
            require(!pipeline.update(yaw(1),at(210),at(209),p).accepted,"future timestamp accepted");
            require(!pipeline.update(yaw(1),at(220),at(500),p).accepted,"old inference accepted");
        } else if (test=="stages") {
            unsigned calls=0;
            auto covariance=[&](const pose& raw) { ++calls; auto output=raw; output[3]-=.1; return output; };
            auto r=pipeline.update(yaw(10),at(0),at(1),p,true,covariance);
            require(r.accepted && calls==1 && r.value[3]==9.9,"covariance stage did not follow accepted raw pose");
            r=pipeline.update(yaw(150),at(13),at(14),p,true,covariance);
            require(!r.accepted && calls==1 && r.holding,"rejected pose entered covariance stage");
            r=pipeline.update(yaw(11),at(26),at(27),p,true,covariance);
            require(r.accepted && calls==2 && r.value[3]==10.9,"validation used filtered reference coordinates");
        } else if (test=="feature-toggles") {
            prime(pipeline,p); pipeline.update({},at(208),at(209),p); pipeline.poll(at(500));
            require(pipeline.update(yaw(90),at(510),at(511),p).diagnostics.recovery_active,"recovery setup failed");
            p.recovery=false;
            const auto r=pipeline.update(yaw(90),at(523),at(524),p);
            require(r.valid && !r.diagnostics.recovery_active && r.value[3]==90,"disabling recovery retained confirmation or blending");
        } else if (test=="options") {
            p.hold_ms=0; p.recovery=false; prime(pipeline,p);
            require(!pipeline.update({},at(208),at(209),p).valid,"recovery off still holds");
            require(pipeline.update(yaw(90),at(300),at(301),p).value[3]==90,"recovery off still blends");
            p.validation=false; require(pipeline.update(yaw(-90),at(313),at(314),p).accepted,"validation off still rejects velocity");
            p.spike_strength=std::numeric_limits<double>::quiet_NaN();
            require(pipeline.update(yaw(-89),at(326),at(327),p).accepted,"invalid INI value poisoned processing");
        } else if (test=="filter" || test=="prediction" || test=="mapping") {
            adaptive_filter filter; adaptive_parameters fp; fp.enabled=true; fp.motion_aware=true; fp.validated=true; fp.recovery_managed=true;
            predictor prediction;
            for (int i=0;i<60;++i) {
                const auto r=pipeline.update(yaw(std::min(i*4.,90.)),at(i*13.),at(i*13.+1),p);
                fp.motion=r.diagnostics.motion; fp.holding=r.holding;
                const auto output=filter.update(r.value,r.captured,fp);
                require(finite(output),"adaptive interaction returned corrupt values");
                prediction.update(output,r.captured,r.prediction_allowed);
                const auto predicted=prediction.predict(at(i*13.+2),{true,15,3,.5});
                require(std::abs(delta(predicted.value[3],output[3],3))<=3.001,"prediction cap lost");
            }
            const auto r=pipeline.update({},at(800),at(801),p);
            prediction.update(r.value,r.captured,r.prediction_allowed);
            require(prediction.predict(at(802),{true,15,3,.5}).horizon_ms==0,"held/lost pose extrapolated");
            require(pipeline.update(yaw(90),at(820),at(821),p).value==yaw(90),"mapping units or coordinates changed");
        } else if (test=="benchmark") {
            std::vector<pose> inputs(30000); std::vector<double> elapsed; elapsed.reserve(inputs.size());
            for (std::size_t i=0;i<inputs.size();++i) inputs[i]=yaw(i%1000==500 ? 170 : 40*std::sin(double(i)*.01));
            const auto before=memory();
            for (bool on : {false,true}) {
                pipeline.reset(); p.enabled=on; elapsed.clear(); double checksum=0,total=0,max_error=0;
                for (std::size_t i=0;i<inputs.size();++i) {
                    const auto begin=clock::now();
                    const auto r=pipeline.update(inputs[i],at(double(i)*13.333),at(double(i)*13.333+1),p);
                    const auto end=clock::now(); checksum+=r.value[3]; const double duration=milliseconds(begin,end);
                    total+=duration; elapsed.push_back(duration);
                    if (r.accepted) max_error=std::max(max_error,std::abs(delta(r.value[3],inputs[i][3],3)));
                }
                std::sort(elapsed.begin(),elapsed.end());
                std::cout << "synthetic_pose_replay," << (on ? "ON" : "OFF") << ",p50_ms=" << elapsed[elapsed.size()/2]
                    << ",p95_ms=" << elapsed[elapsed.size()*95/100] << ",p99_ms=" << elapsed[elapsed.size()*99/100]
                    << ",mean_ms=" << total/double(inputs.size())
                    << ",rejected=" << pipeline.poll(at(double(inputs.size())*13.333)).diagnostics.rejected
                    << ",accepted_pose_max_change_degrees=" << max_error << ",checksum=" << checksum << '\n';
            }
            std::cout << "pipeline_bytes=" << sizeof(neuralnet_pipeline) << ",working_set_before=" << before << ",working_set_after=" << memory()
                << ",synthetic_only_no_capture_or_inference_measurement\n";
        } else throw std::runtime_error("unknown test");
        std::cout << "neuralnet2-" << test << ": PASS\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
