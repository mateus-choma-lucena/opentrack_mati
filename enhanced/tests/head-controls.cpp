#include "enhanced/head-controls.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace enhanced;
static timestamp at(double ms) { return timestamp{}+std::chrono::duration_cast<clock::duration>(std::chrono::duration<double,std::milli>(ms+1000)); }
static pose yaw(double v) { pose p{}; p[3]=v; return p; }
static void check(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
static bool close(double a,double b,double tolerance=1e-8) { return std::abs(a-b)<tolerance; }
int main(int argc,char** argv)
{
    try {
        const std::string test=argc>1 ? argv[1] : "";
        if (test=="mapping") {
            check(close(amplification_gain(0),.25)&&amplification_gain(42)==1&&amplification_gain(100)==4,"gain anchors incorrect");
            for (int i=1;i<=100;++i) check(amplification_gain(i)>amplification_gain(i-1),"gain not monotonic");
            check(amplification_gain(-100)==.25&&amplification_gain(200)==4,"gain range not bounded");
            check(speed_response(0)==.125&&speed_response(55)==1&&speed_response(100)==5,"response anchors incorrect");
            head_movement_controls c; head_control_parameters p{true,true,100,55,{0,42,100,42,0,100}};
            c.configure(at(0),p); c.configure(at(120),p);
            const auto out=c.amplify({4,4,4,4,4,4});
            check(out==pose{1,4,16,4,1,16},"per-axis overrides multiplied global gain or incorrect axis order");
        } else if (test=="neutral") {
            head_movement_controls c; head_control_parameters p;
            pose input{1,2,3,15,-10,9}, selected{.5,1,2,10,-6,5};
            p.amplification=100; p.speed=0; c.configure(at(0),p);
            check(c.amplify(c.respond(input,selected,at(0),false))==selected,"disabled path changes filter output");
            p={true}; c.configure(at(10),p); c.configure(at(200),p);
            check(c.amplify(c.respond(input,selected,at(200),false))==selected,"neutral activation changes output");
        } else if (test=="speed") {
            response_filter slow,fast; slow.seed(yaw(0),at(0)); fast.seed(yaw(0),at(0));
            double s=0,f=0;
            for (int i=1;i<=5;++i) { s=slow.update(yaw(20),at(i*10),speed_response(0))[3]; f=fast.update(yaw(20),at(i*10),speed_response(100))[3]; }
            check(s>0&&s<20&&f>19&&f>s+.5,"Speed did not control time-aware responsiveness");
        } else if (test=="intervals") {
            response_filter regular,variable; regular.seed(yaw(0),at(0)); variable.seed(yaw(0),at(0));
            pose a{},b{}; double time=0;
            for (int i=1;i<=100;++i) { a=regular.update(yaw(20),at(i*10),.125); time+=i%2 ? 5 : 15; b=variable.update(yaw(20),at(time),.125); }
            check(std::abs(a[3]-b[3])<.02&&a[3]>19.99,"variable dt changes settled response");
            const auto repeated=variable.update(yaw(80),at(time),.125);
            check(repeated==b&&variable.update(yaw(80),at(time-1),.125)==b,"duplicate/regressed timestamps advance smoothing");
        } else if (test=="turns") {
            response_filter f; f.seed(yaw(0),at(0)); pose out{};
            for (int i=1;i<=30;++i) out=f.update(yaw(i*3),at(i*10),.125);
            check(out[3]>80&&out[3]<=90,"low Speed excessively delays a quick intentional turn");
        } else if (test=="stops"||test=="reversals") {
            response_filter f; f.seed(yaw(0),at(0));
            double previous=0;
            for (int i=1;i<=30;++i) previous=f.update(yaw(i),at(i*10),speed_response(100))[3];
            const double stop_start=previous;
            for (int i=31;i<130;++i) {
                const double target=test=="stops" ? 30. : -30.;
                const auto v=f.update(yaw(target),at(i*10),speed_response(100))[3];
                check(v>=(test=="stops"?stop_start:target)-1e-9&&v<=30+1e-9,"response overshoot/oscillation");
                if (test=="stops") check(v>=previous-1e-9,"stopped response oscillates");
                if (test=="reversals") check(v<=previous+1e-9,"response reverses in wrong direction");
                previous=v;
            }
            check(std::abs(previous-(test=="stops"?30:-30))<.001,"stop/reversal does not settle");
        } else if (test=="recenter") {
            head_movement_controls c; head_control_parameters p{true,false,100,0}; c.configure(at(0),p); c.configure(at(120),p);
            c.respond(yaw(80),yaw(40),at(120),false); c.reset_motion();
            check(c.amplify(c.respond(yaw(0),yaw(0),at(130),false))[3]==0,"recenter keeps old response/gain offset");
        } else if (test=="prediction") {
            for (int speed:{0,55,100}) {
                head_movement_controls c; head_control_parameters p{true,false,100,speed}; c.configure(at(0),p); c.configure(at(120),p);
                predictor predictor_; pose out{};
                for (int i=0;i<30;++i) { out=c.respond(yaw(i*2),yaw(i*2),at(120+i*10),false); predictor_.update(out,at(120+i*10),true); }
                const auto result=predictor_.predict(at(411),{true,8,.5,.1},{},out);
                check(result.horizon_ms==8&&result.rotation<=.5,"Speed changed horizon or prediction bound");
                check(c.amplify(result.value)[3]==result.value[3]*4,"prediction/amplification applied more than once");
                check(predictor_.predict(at(412),{true,8,.5,.1},{},out).value==result.value,"duplicate polls accumulate prediction");
            }
        } else if (test=="smoothing") {
            adaptive_filter slow,original,fast; adaptive_parameters p; p.enabled=true; p.jitter=0;
            slow.update(yaw(0),at(0),p); original.update(yaw(0),at(0),p); fast.update(yaw(0),at(0),p);
            p.response_multiplier=.125; const auto a=slow.update(yaw(1),at(10),p);
            p.response_multiplier=1; const auto b=original.update(yaw(1),at(10),p);
            p.response_multiplier=5; const auto d=fast.update(yaw(1),at(10),p);
            check(a[3]<b[3]&&b[3]<d[3],"shared Speed does not alter adaptive response");
            head_movement_controls c; c.configure(at(0),{true,false,42,0}); c.configure(at(120),{true,false,42,0});
            check(c.respond(yaw(100),b,at(120),true,original.estimated_lag_ms())==b,"shared adaptive response adds a second smoothing pass");
        } else if (test=="transitions") {
            head_movement_controls c; head_control_parameters p; c.configure(at(0),p);
            p={true,false,100,0}; c.configure(at(10),p);
            check(c.amplify(c.respond(yaw(20),yaw(10),at(10),false))[3]==10,"enable jumps immediately");
            double previous=1;
            for (int i=11;i<=130;++i) { c.configure(at(i),p); double gain=c.amplify(yaw(1))[3]; check(gain>=previous&&gain<=4,"gain transition oscillates"); previous=gain; }
            check(previous==4,"enable transition fails to reach target");
            p.enabled=false; c.configure(at(130),p); check(c.amplify(yaw(1))[3]==4,"disable jumps immediately");
            for (int i=131;i<=250;++i) { c.configure(at(i),p); const auto gain=c.amplify(yaw(1))[3]; check(gain<=previous&&gain>=1,"disable transition oscillates"); previous=gain; }
            check(c.amplify(c.respond(yaw(20),yaw(10),at(250),false))[3]==10,"disable does not restore original filter exactly");
        } else if (test=="extremes") {
            for (int v:{0,100}) { head_movement_controls c; head_control_parameters p{true,false,v,v};
                c.configure(at(0),p); c.configure(at(120),p);
                for (int i=0;i<100;++i) { const auto out=c.amplify(c.respond({1,2,3,179,-179,5},{1,2,3,179,-179,5},at(120+i*10),false)); check(finite(out)&&std::abs(out[0])>=.25,"slider endpoint invalid or freezes movement"); }
            }
        } else if (test=="diagnostics") {
            head_movement_controls c; c.configure(at(0),{true,false,100,0}); c.configure(at(120),{true,false,100,0});
            c.respond(yaw(10),yaw(5),at(120),false); c.respond(yaw(10),yaw(5),at(130),false);
            const auto d=c.snapshot(yaw(10),yaw(20),at(130),at(132),true);
            check(d.enabled&&d.fallback_response&&d.gain==4&&d.speed==0&&d.responsiveness==.125&&d.estimated_lag_ms,"diagnostics missing effective controls");
            check(d.raw[3]==10&&d.processed[3]==20&&d.captured==at(130)&&d.submitted==at(132)&&d.valid,"diagnostics pose/timestamps incorrect");
            telemetry telemetry_; telemetry_.head_controls(d); check(telemetry_.read(at(132)).head.processed==d.processed,"diagnostics feedback lost");
        } else throw std::runtime_error("unknown case");
        std::cout << test << ": PASS\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
