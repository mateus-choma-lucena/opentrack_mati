#include "enhanced/head-controls.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace enhanced;
static timestamp at(double ms) { return timestamp{}+std::chrono::duration_cast<clock::duration>(std::chrono::duration<double,std::milli>(ms+1000)); }
static pose yaw(double value) { pose p{}; p[3]=value; return p; }
static void check(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
static const pose limits{75,75,75,180,90,180};
static bool near(double a,double b,double tolerance=1e-7) { return std::abs(a-b)<tolerance; }
int main(int argc,char** argv)
{
    try {
        const std::string test=argc>1?argv[1]:"";
        if (test=="mapping" || test=="saturation") {
            check(amplification_gain(0)==.25&&amplification_gain(42)==1&&amplification_gain(100)==4,"neutral/gain anchors wrong");
            for (int i=1;i<=100;++i) check(amplification_gain(i)>amplification_gain(i-1),"gain not monotonic");
            head_movement_controls c; head_control_parameters p{true,true,100,55,{0,42,100,42,0,100}};
            c.configure(at(0),p); c.configure(at(120),p);
            check(c.process({4,4,4,4,4,4},{4,4,4,4,4,4},at(120),at(120),limits)==pose{1,4,16,4,1,16},"per-axis gains multiplied global value");
            p={true,false,100,55}; c.configure(at(120),p); c.configure(at(240),p);
            check(c.process({50,-50,70,170,-80,100},{},at(240),at(240),limits)==pose{75,-75,75,180,-90,180},"mapped output exceeded signed axis limits");
            if (test=="saturation") {
                p.speed=0; c.configure(at(240),p); c.configure(at(360),p);
                c.process(yaw(170),yaw(170),at(360),at(360),limits);
                pose reduced=limits; reduced[3]=30;
                check(c.process(yaw(170),yaw(170),at(370),at(370),reduced)[3]<=30,"cached response escaped a reduced output limit");
            }
        } else if (test=="neutral") {
            head_movement_controls c; head_control_parameters p{false,false,100,0}; pose original{1,-2,3,15,-10,9};
            c.configure(at(0),p); check(c.process(original,yaw(100),at(0),at(0),limits)==original,"disabled feature altered original output");
            p={true}; c.configure(at(10),p); c.configure(at(150),p);
            check(c.process(original,yaw(100),at(150),at(150),limits)==original,"neutral configuration changed selected response");
        } else if (test=="speed" || test=="intervals" || test.rfind("fps",0)==0) {
            const int fps=test.rfind("fps",0)==0?std::stoi(test.substr(3)):120;
            output_response slow,fast; slow.seed(yaw(0),at(0)); fast.seed(yaw(0),at(0)); pose s{},f{};
            for (int i=1;i<=fps;++i) {
                const double ms=i*1000./fps;
                s=slow.update(yaw(20),yaw(20),yaw(20),at(ms),at(ms),.125);
                f=fast.update(yaw(20),yaw(20),yaw(20),at(ms),at(ms),5.);
                check(f[3]>=s[3]&&s[3]<=20&&f[3]<=20,"higher Speed did not converge faster or overshot");
                if (i==1) check(f[3]>s[3],"Speed settings have identical response");
            }
            check(near(s[3],20*(1-std::exp(-6.283185307179586*1.5)),1e-6),"response depends on camera FPS");
            check(slow.update(yaw(50),yaw(50),yaw(50),at(1000),at(1000),.125)==s,"same timestamp advanced response");
            check(slow.update(yaw(50),yaw(50),yaw(50),at(999),at(999),.125)==s,"regressed timestamp advanced response");
            if (test=="intervals") {
                output_response variable; variable.seed(yaw(0),at(0)); pose result{}; double t=0;
                for (int i=0;i<100;++i) { t+=i%2?15:5; result=variable.update(yaw(20),yaw(20),yaw(20),at(t),at(t),.125); }
                check(near(result[3],s[3],1e-6),"variable frame intervals changed convergence");
                check(variable.update(yaw(20),yaw(20),yaw(20),at(1010),at(1000),.125)[3]>result[3],"output cannot catch up between camera frames");
            }
        } else if (test=="turns" || test=="stops" || test=="reversals") {
            output_response response; response.seed(yaw(0),at(0)); double previous=0;
            for (int i=1;i<=30;++i) previous=response.update(yaw(i*3),yaw(i*3),yaw(i*3),at(i*10),at(i*10),5.)[3];
            check(previous>88&&previous<=90,"fast turns lag excessively");
            for (int i=31;i<130;++i) {
                const double target=test=="reversals"?-90.:90.;
                const double next=response.update(yaw(target),yaw(target),yaw(target),at(i*10),at(i*10),5.)[3];
                check(next>=-90&&next<=90,"stop/reversal overshot output limits");
                check(test=="reversals"?next<=previous:next>=previous,"response oscillates or reverses incorrectly"); previous=next;
            }
            response.reset(); response.seed(yaw(170),at(0));
            const auto crossing=response.update(yaw(-170),yaw(-170),yaw(-170),at(10),at(10),.125);
            check(crossing[3]<170&&crossing[3]>-170,"mapped yaw used physical-angle wrapping");
        } else if (test=="recenter" || test=="loss") {
            head_movement_controls c; head_control_parameters p{true,false,100,0}; c.configure(at(0),p); c.configure(at(120),p);
            c.process(yaw(40),yaw(80),at(120),at(120),limits);
            if (test=="recenter") { c.reset_motion(); check(c.process(yaw(0),yaw(0),at(130),at(130),limits)[3]==0,"recenter retained output state"); }
            else {
                output_response r; r.seed(yaw(10),at(0)); r.tracking_lost();
                check(r.update(yaw(100),yaw(100),yaw(100),at(500),at(500),5.)[3]==10,"tracking reacquisition jumped");
                const auto recovered=r.update(yaw(100),yaw(100),yaw(100),at(504),at(500),.125);
                check(recovered[3]>10&&recovered[3]<100,"tracking recovery failed to converge");
            }
        } else if (test=="smoothing") {
            output_response r; r.seed(yaw(0),at(0)); r.update(yaw(0),yaw(0),yaw(0),at(1),at(1),1.);
            double reference=0; pose out{};
            for (int i=1;i<=100;++i) { reference+=alpha(2.,.01)*(20-reference); out=r.update(yaw(20),yaw(reference),yaw(20),at(1+i*10),at(1+i*10),.125); }
            check(near(r.cutoffs()[3],.25,1e-6)&&near(out[3],20*(1-std::exp(-6.283185307179586*.25)),1e-5),"selected smoothing stacked instead of serving as reference");
            adaptive_filter filter; adaptive_parameters p; p.enabled=true; p.jitter=0;
            filter.update(yaw(0),at(0),p); const auto filtered=filter.update(yaw(10),at(10),p);
            check(filter.mapping_target()[3]==10&&filtered[3]<=10,"adaptive did not provide guarded target");
            filter.update(yaw(100),at(20),p); check(filter.mapping_target()[3]==10,"spike entered mapped target");
        } else if (test=="prediction") {
            predictor prediction; for (int i=0;i<30;++i) prediction.update(yaw(i*2),at(i*10),true);
            const auto predicted=prediction.predict(at(291),{true,8,.5,.1});
            check(predicted.horizon_ms==8&&predicted.rotation<=.5,"prediction bounds changed");
            for (int speed:{0,55,100}) {
                head_movement_controls c; c.configure(at(0),{true,false,100,speed}); c.configure(at(120),{true,false,100,speed});
                const auto mapped=yaw(predicted.value[3]*2); c.process(mapped,mapped,at(120),at(290),limits);
                check(prediction.predict(at(292),{true,8,.5,.1}).value==predicted.value,"output Speed altered prediction state/horizon");
            }
        } else if (test=="transitions" || test=="extremes") {
            head_movement_controls c; c.configure(at(0),{}); c.process(yaw(10),yaw(10),at(0),at(0),limits);
            head_control_parameters p{true,false,100,55}; c.configure(at(10),p);
            check(c.process(yaw(10),yaw(10),at(10),at(10),limits)[3]==10,"enable setting jumped");
            double previous=10;
            for (int i=11;i<=130;++i) { c.configure(at(i),p); const double v=c.process(yaw(10),yaw(10),at(i),at(i),limits)[3]; check(v>=previous&&v<=40,"gain transition overshot"); previous=v; }
            p.enabled=false; c.configure(at(130),p); check(c.process(yaw(10),yaw(10),at(130),at(130),limits)[3]==40,"disable setting jumped");
            c.configure(at(250),p); check(c.process(yaw(10),yaw(10),at(250),at(250),limits)[3]==10,"disable did not restore original output");
            if (test=="extremes") for (int v:{0,100}) { head_movement_controls x; head_control_parameters q{true,false,v,v}; x.configure(at(0),q); x.configure(at(120),q);
                for (int i=0;i<100;++i) check(finite(x.process({1,-2,3,179,-90,170},{1,-2,3,179,-90,170},at(120+i*10),at(120+i*10),limits)),"slider endpoint corrupted output"); }
        } else if (test=="diagnostics") {
            head_movement_controls c; c.configure(at(0),{true,false,100,0}); c.configure(at(120),{true,false,100,0});
            c.process(yaw(5),yaw(10),at(120),at(120),limits,true); c.process(yaw(5),yaw(10),at(130),at(130),limits,true);
            const auto d=c.snapshot(yaw(10),yaw(20),at(130),at(132),true);
            check(d.enabled&&d.shared_response&&d.fallback_response&&d.gain==4&&d.response_cutoffs[3]>0&&d.estimated_lag_ms,"mapped diagnostics unavailable");
            check(d.mapped_target[3]==40&&d.raw[3]==10&&d.processed[3]==20&&d.submitted==at(132),"mapped target/pose/time incorrect");
        } else throw std::runtime_error("unknown test");
        std::cout << test << ": PASS\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
