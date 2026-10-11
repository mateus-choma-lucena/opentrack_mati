#include "enhanced/analyzer.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace enhanced;
static timestamp at(double ms) { return timestamp{}+std::chrono::duration_cast<clock::duration>(std::chrono::duration<double,std::milli>(ms+1000)); }
static void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
static pose yaw(double y) {pose p{};p[3]=y;return p;}
int main(int argc,char** argv)
{
    try {
        const std::string mode=argc>1?argv[1]:"";
        predictor pred;for(int i=0;i<20;++i) pred.update(yaw(i*2),at(i*13.333),true);
        const auto last=at(19*13.333);
        const prediction_parameters dynamic{true,8,3,.5,true,15};
        if(mode=="dynamic-age") {
            auto zero=pred.predict(last,dynamic), young=pred.predict(last+std::chrono::milliseconds(4),dynamic), old=pred.predict(last+std::chrono::milliseconds(30),dynamic);
            check(zero.value==yaw(38) && young.horizon_ms<=4 && young.rotation>0 && old.horizon_ms==15 && old.rotation<=3,"age horizon/caps incorrect");
            auto p=dynamic;p.max_horizon_ms=0;check(pred.predict(last+std::chrono::milliseconds(8),p).value==yaw(38),"zero dynamic must be exact");
            check(pred.predict(last+std::chrono::milliseconds(101),dynamic).value==yaw(38),"stale prediction");
        } else if(mode=="dynamic-stop") {
            pred.update(yaw(38),last+std::chrono::milliseconds(13),true);
            check(pred.predict(last+std::chrono::milliseconds(24),dynamic).rotation==0,"stop overshoot");
            pred.update(yaw(36),last+std::chrono::milliseconds(26),true);
            check(pred.predict(last+std::chrono::milliseconds(34),dynamic).rotation==0,"reversal overshoot");
            auto p=dynamic;p.motion=motion_state::stopping;check(pred.predict(last+std::chrono::milliseconds(34),p).value==yaw(36),"raw stop classification ignored");
        } else if(mode=="dynamic-reliability") {
            auto full=pred.predict(last+std::chrono::milliseconds(10),dynamic,{}, {},1), low=pred.predict(last+std::chrono::milliseconds(10),dynamic,{}, {},.1);
            check(std::abs(low.rotation-full.rotation*.1)<1e-9,"reliability attenuation incorrect");
            check(pred.predict(last+std::chrono::milliseconds(10),dynamic,{}, {},0).rotation==0,"zero reliability predicts");
            pred.update(yaw(38),last,false);check(pred.predict(last+std::chrono::milliseconds(10),dynamic).rotation==0,"lost predicts");
        } else if(mode=="dynamic-polls") {
            const auto now=last+std::chrono::milliseconds(10);const auto a=pred.predict(now,dynamic);
            for(int i=0;i<1000;++i) {pred.update(yaw(38),last,true);check(pred.predict(now,dynamic).value==a.value,"poll compounded prediction");}
            pred.reset();check(pred.predict(now,dynamic).rotation==0,"recenter retains prediction");
        } else if(mode=="dynamic-noise") {
            pred.reset();for(int i=0;i<100;++i) pred.update(yaw((i%2?1:-1)*.01),at(i*13.333),true);
            auto r=pred.predict(at(99*13.333+10),dynamic);check(r.rotation<.01,"stationary noise amplified");
            pred.update(yaw(0),{},true);check(pred.predict(at(2000),dynamic).rotation==0,"zero timestamp accepted");
        } else if(mode=="fusion") {
            flow_assistance flow;auto& f=flow.observation;f.enabled=f.valid=true;f.anchor=last;f.captured=last+std::chrono::milliseconds(5);f.reliability=1;flow.strength=1;
            const auto now=last+std::chrono::milliseconds(10);auto base=pred.predict(now,dynamic), assisted=pred.predict(now,dynamic,{}, {},{},flow);
            check(std::abs(assisted.rotation-base.rotation*.7)<1e-9 && assisted.flow_correction[3]<0,"fusion is not bounded damping");
            f.captured=last;check(pred.predict(now,dynamic,{}, {},{},flow).value==base.value,"same-frame flow affects pose");
            f.captured=last+std::chrono::milliseconds(5);f.anchor=last-std::chrono::milliseconds(13);
            check(pred.predict(now,dynamic,{}, {},{},flow).value==base.value,"fresh NeuralNet failed to reset correction");
            f.anchor=last;f.valid=false;check(pred.predict(now,dynamic,{}, {},{},flow).value==base.value,"invalid/occluded flow did not fall back");
        } else if(mode=="analyzer") {
            analyzer_buffer buffer;analysis_sample s;s.captured=at(0);s.published=at(1);s.validated=s.captured;s.valid=s.raw_valid=true;
            buffer.push(s);check(buffer.read().samples.empty(),"disabled analyzer allocates samples");
            buffer.configure(true,true,2);
            for(int i=0;i<700;++i) {s.captured=at(i*10);s.validated=s.captured;s.published=at(i*10+1);buffer.push(s);}
            auto live=buffer.read(), record=buffer.read(true);check(live.samples.size()==512 && live.overwritten==188 && record.samples.size()==200 && !record.recording,"buffer/record bound incorrect");
            const auto saved=record.samples.back().captured;
            buffer.configure(false,false,2);check(buffer.read(true).samples.back().captured==saved,"recording was not preserved");
            buffer.configure(true,true,50);s.captured=at(7000);s.validated=s.captured;s.published=at(7001);buffer.push(s);
            buffer.output(s.captured,yaw(1),predicted_pose{yaw(2)},at(7002));buffer.output(s.captured,yaw(7),predicted_pose{yaw(8)},at(7003));
            record=buffer.read(true);check(record.samples.size()==1 && record.samples[0].filtered==yaw(7) && record.samples[0].predicted==yaw(8) && record.samples[0].output==at(7003),"output polls duplicate/compound recorded pose");
            buffer.push(s);check(buffer.read().dropped>0,"invalid timestamp not counted");
        } else if(mode=="analyzer-contention") {
            analyzer_buffer buffer;buffer.configure(true,true,50);std::atomic<bool> running{true};
            std::thread consumer([&]{while(running.load()) {auto snapshot=buffer.read();(void)snapshot;}});
            for(int i=0;i<10000;++i) {analysis_sample s;s.captured=at(i);s.published=at(i+1);buffer.push(s);}
            running=false;consumer.join();check(buffer.read().samples.size()<=512 && buffer.read(true).samples.size()<=4096,"unbounded telemetry");
        } else throw std::runtime_error("unknown test");
        std::cout<<mode<<": PASS\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
