#include "enhanced/motion.hpp"
#include "enhanced/latest-frame.hpp"
#include "enhanced/reconnect.hpp"
#include "enhanced/ipc-lock.hpp"
#include <iostream>
#include <thread>
#include <atomic>
#include <stdexcept>
#include <string>

using namespace enhanced;
static timestamp at(double ms) { return timestamp{} + std::chrono::duration_cast<clock::duration>(std::chrono::duration<double, std::milli>(ms+1000)); }
static void require(bool passed, const char* message) { if (!passed) throw std::runtime_error(message); }
static pose yaw(double angle) { pose p{}; p[3]=angle; return p; }
static adaptive_parameters enabled() { adaptive_parameters p; p.enabled=true; return p; }

int main(int argc, char** argv)
{
    try {
        const std::string test = argc>1 ? argv[1] : "";
        if (test == "timestamps") {
            require(clock::is_steady, "clock must be monotonic");
            require(std::abs(milliseconds(at(0), at(.125))-.125)<1e-6, "submillisecond timestamps lost");
        } else if (test == "percentiles") {
            window<100> values; require(!values.mean(), "unavailable data reported as zero");
            for (int i=1; i<=100; ++i) values.add(i);
            require(*values.percentile(.5)==51 && *values.percentile(.95)==96 && *values.percentile(.99)==100, "quantiles incorrect");
            telemetry t; t.capture(at(0),640,480,75); t.capture(at(10),640,480,75);
            t.processed(at(10),at(12),at(15),2,true,false);
            auto d=t.read(at(20)); require(d.capture_fps && std::abs(*d.capture_fps-100)<1e-6 && *d.processing_ms==5 && *d.frame_age_ms==2, "telemetry wrong");
            t.output(at(10),at(20)); t.output(at(10),at(30));
            d=t.read(at(30)); require(d.output_samples==1 && *d.application_ms==10 && *d.application_p95_ms==10, "output latency double-counted or mismatched sample timestamp");
            t.disconnected(); require(!t.read(at(20)).capture_fps, "disconnected camera rate must be unavailable");
        } else if (test == "queue") {
            latest_frame<int> queue; int producer=1, consumer=0; timestamp time;
            require(!queue.publish(producer,at(1)), "first frame counted as replacement");
            producer=2; require(queue.publish(producer,at(2)), "replacement missing");
            require(queue.take(consumer,time,std::chrono::milliseconds(0)) && consumer==2 && time==at(2), "not latest first");
            queue.stop(); require(!queue.take(consumer,time,std::chrono::milliseconds(0)), "stopped queue returned frame");
        } else if (test == "queue_threads") {
            latest_frame<int> queue; std::atomic<bool> done=false;
            std::thread producer([&] { int p=0; for (int i=1;i<=10000;++i) { p=i; queue.publish(p,at(i)); } done=true; });
            int last=0, c=0; timestamp time;
            while (!done || last<10000) if (queue.take(c,time,std::chrono::milliseconds(100))) {
                require(c>last && time==at(c), "pose/frame timestamp torn or order regressed"); last=c;
            }
            producer.join(); require(last==10000, "latest final frame missing");
        } else if (test == "ipc") {
            std::uint32_t word=0, sequence=0;
            require(try_frame_lock(word) && !try_frame_lock(word), "shared frame lock admitted two owners");
            publish_sequence(sequence,42); require(frame_sequence(sequence)==42, "shared sequence unavailable");
            unlock_frame(word); require(try_frame_lock(word), "shared lock did not release"); unlock_frame(word);
        } else if (test == "stale") {
            require(!stale(at(0),at(50),50) && stale(at(0),at(50.1),50), "frame age boundary incorrect");
            require(stale(at(10),at(0),50) && stale({},at(0),50), "invalid timestamps accepted");
        } else if (test == "jitter") {
            adaptive_filter filter; auto p=enabled(); filter.update(yaw(0),at(0),p);
            double sum=0; for (int i=1;i<2000;++i) { auto out=filter.update(yaw(i%2 ? .1 : -.1),at(i*10),p); sum+=out[3]*out[3]; }
            require(std::sqrt(sum/1999)<.04, "stationary jitter not attenuated");
        } else if (test == "fast") {
            adaptive_filter filter; auto p=enabled(); filter.update(yaw(0),at(0),p);
            pose out; for (int i=1;i<=30;++i) out=filter.update(yaw(i*3),at(i*10),p);
            require(out[3]>86, "intentional movement delayed excessively");
        } else if (test == "wrap") {
            adaptive_filter filter; auto p=enabled(); filter.update(yaw(179),at(0),p);
            auto out=filter.update(yaw(-179),at(10),p);
            require(std::abs(delta(out[3],179,3))<2.01, "yaw crossed long way around wrap");
        } else if (test == "recovery") {
            adaptive_filter filter; auto p=enabled(); filter.update(yaw(0),at(0),p);
            filter.update(yaw(std::numeric_limits<double>::quiet_NaN()),at(10),p);
            require(filter.update(yaw(90),at(500),p)[3]==0, "jump on reacquisition");
            require(finite(filter.update(yaw(90),at(510),p)), "invalid state after loss");
            require(filter.update(yaw(90),at(509),p)[3] == filter.update(yaw(90),at(509),p)[3], "clock regression corrupted state");
        } else if (test == "spike") {
            adaptive_filter filter; auto p=enabled(); filter.update(yaw(0),at(0),p);
            require(filter.update(yaw(70),at(10),p)[3]==0, "single implausible spike accepted");
            require(std::abs(filter.update(yaw(0),at(20),p)[3])<.01, "spike polluted filter state");
            filter.reset(); filter.update(yaw(0),at(0),p);
            filter.update(yaw(70),at(10),p); filter.update(yaw(70),at(20),p);
            const auto recovering=filter.update(yaw(70),at(30),p);
            const auto next=filter.update(yaw(70),at(40),p);
            require(recovering[3]<=3.61 && next[3]-recovering[3]<=3.61, "persistent outlier recovery jumped");
        } else if (test == "intervals") {
            adaptive_filter a,b; auto p=enabled(); p.responsiveness=0; p.jitter=0;
            a.update(yaw(0),at(0),p); b.update(yaw(0),at(0),p);
            pose x,y;
            for (int i=1;i<=100;++i) x=a.update(yaw(10),at(i*10),p);
            for (int i=1;i<=50;++i) y=b.update(yaw(10),at(i*20),p);
            require(std::abs(x[3]-y[3])<.001, "smoothing depends on sample rate");
        } else if (test == "recenter") {
            adaptive_filter filter; auto p=enabled(); filter.update(yaw(80),at(0),p); filter.reset();
            require(filter.update(yaw(0),at(10),p)[3]==0, "filter retained old center");
            predictor prediction; prediction.update(yaw(0),at(0),true); prediction.update(yaw(3),at(10),true); prediction.reset();
            prediction.update(yaw(0),at(20),true); require(prediction.predict(at(21),{true}).rotation==0, "prediction retained center velocity");
        } else if (test == "confidence") {
            adaptive_filter filter; auto p=enabled(); p.confidence_aware=true; filter.update(yaw(0),at(0),p,1.);
            require(filter.update(yaw(20),at(10),p,0.)[3]==0, "invalid confidence measurement moved filter");
            predictor prediction; prediction.update(yaw(0),at(0),true); prediction.update(yaw(3),at(10),true);
            require(prediction.predict(at(11),{true},0.).rotation==0, "prediction ignored confidence");
        } else if (test == "prediction" || test == "overshoot" || test == "direction" || test == "loss") {
            predictor prediction; for (int i=0;i<30;++i) prediction.update(yaw(i*5),at(i*10),true);
            auto result=prediction.predict(at(291),{true,15,.5,.01});
            if (test == "prediction") {
                require(result.rotation>0, "no prediction during consistent motion");
                require(prediction.predict(at(292),{true,15,.5,.01}).value==result.value, "prediction accumulates on duplicate polls");
                require(prediction.predict(at(291),{false}).rotation==0, "disable path predicts");
            } else if (test == "overshoot") require(result.rotation<=.5 && result.translation<=.01 && result.horizon_ms<=15, "overshoot exceeds configured cap");
            else if (test == "direction") { prediction.update(yaw(140),at(300),true); require(prediction.predict(at(301),{true}).rotation==0, "prediction overshot reversal"); }
            else { prediction.update(yaw(0),at(300),false); require(prediction.predict(at(301),{true}).rotation==0, "prediction during loss"); prediction.update(yaw(20),at(400),true); require(prediction.predict(at(401),{true}).rotation==0, "prediction on reacquisition"); }
        } else if (test == "reconnect") {
            reconnect_state state; require(state.due(at(0),3), "initial retry missing");
            require(!state.due(at(100),3) && state.due(at(250),3), "retry backoff wrong");
            require(state.due(at(750),3) && !state.due(at(5000),3), "unbounded retry count");
            state.recovered(); require(state.due(at(5000),3), "recovery failed to reset retries");
        } else throw std::runtime_error("unknown test");
        std::cout << test << ": PASS\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
