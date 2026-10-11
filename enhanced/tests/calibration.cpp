#include "enhanced/calibration.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace enhanced;
using namespace enhanced::calibration;
static void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
static timestamp at(int ms) { return timestamp{}+std::chrono::milliseconds(1000+ms); }
static observation sample(int ms,axes p={}) { return {p,at(ms),at(ms+10),7,true,.95,.9}; }
static void feed(measurement& m,axes p={},int count=301,int dt=10) {
    for (int i=0;i<count;++i) m.add(sample(i*dt,p));
}
int main(int argc,char** argv) {
    try {
        const std::string test=argc>1?argv[1]:"stable";
        measurement m;
        if (test=="stable") {
            axes p{2,3,4,10,8,5}; m.begin(-1); feed(m,p);
            const auto j=m.stationary(); require(m.status()==state::ready && j.valid_samples==301,"stable samples not ready");
            for (int i=0;i<6;++i) require(j.median[i]==p[i] && j.mad[i]==0 && j.p95[i]==0 && j.drift[i]==0 && j.deadzone[i]==0,"stable jitter incorrect");
            require(std::abs(j.valid_hz-100)<1e-9 && j.interval_p95_ms==10 && !j.ambiguous_motion,"stable timestamps incorrect");
        } else if (test=="jitter") {
            m.begin(-1);
            for (int k=0;k<501;++k) { axes p{}; for (int i=0;i<6;++i) p[i]=(i<3?.01:.1)*std::sin(k*1.731); m.add(sample(k*13,p)); }
            const auto j=m.stationary(); require(!j.ambiguous_motion && j.p95[3]>.09 && j.mad[3]>.06 && j.deadzone[3]>.1,"noise estimate failed");
            require(j.interval_p50_ms==13 && j.valid_hz>76 && j.reliability>.89,"jitter quality failed");
        } else if (test=="drift") {
            m.begin(-1); for (int k=0;k<301;++k) { axes p{}; p[3]=k*.02; m.add(sample(k*10,p)); }
            require(m.stationary().ambiguous_motion,"intentional movement hidden as noise");
        } else if (test=="asymmetric" || test=="axes") {
            for (int i=0;i<6;++i) for (int sign:{-1,1}) {
                axes neutral{}; neutral[i]=i<3?4.:170.; axes p=neutral; const double extent=(i<3?3.:20.)*(sign<0?1.:1.5);
                p[i]+=sign*extent; if (i>=3) p[i]=std::remainder(p[i],360.);
                m.begin(i,neutral); feed(m,p);
                require(!m.movement(false),"missing comfort confirmation accepted"); const auto r=m.movement(true);
                require(r && r->first==sign && std::abs(r->second.magnitude-extent)<1e-9 && r->second.samples==301,"axis sign/range failed");
            }
        } else if (test=="accidental") {
            m.begin(3); for (int k=0;k<30;++k) m.add(sample(k*10)); axes p{}; p[3]=80; m.add(sample(300,p));
            require(m.status()==state::retry && !m.movement(true),"quick accidental turn accepted");
            m.begin(3); for (int k=0;k<301;++k) { p[3]=k<20?55:25; m.add(sample(k*10,p)); }
            require(!m.movement(true),"transient extreme contaminated held endpoint");
            m.begin(3); for (int k=0;k<301;++k) { p[3]=25+10*std::sin(k*.02); m.add(sample(k*10,p)); }
            require(!m.movement(true),"unsteady endpoint accepted");
        } else if (test=="insufficient") {
            m.begin(3); axes p{}; p[3]=20; feed(m,p,40,100); require(m.status()!=state::ready && !m.movement(true),"sample minimum bypassed");
            m.begin(3); feed(m,p,120,1); require(m.status()!=state::ready,"duration minimum bypassed");
            m.begin(3); for (int k=0;k<500;++k) m.add(sample(0,p)); require(m.count()==1,"duplicate polls count as measurements");
        } else if (test=="loss" || test=="invalid" || test=="recenter" || test=="timestamps" || test=="confidence") {
            m.begin(3); feed(m,{},100);
            auto s=sample(1000);
            if (test=="loss") s.valid=false;
            if (test=="invalid") s.physical[2]=std::numeric_limits<double>::quiet_NaN();
            if (test=="recenter") ++s.center_epoch;
            if (test=="timestamps") s.captured=at(1500);
            if (test=="confidence") { s.confidence=.2; for (int k=0;k<40;++k) { s.captured=at(1000+k*10); s.received=s.captured; m.add(s); } }
            else m.add(s);
            require(m.status()==state::retry && !m.movement(true),"bad tracking segment accepted");
            m.begin(-1); s=sample(0); s.physical[5]=std::numeric_limits<double>::infinity(); m.add(s); require(m.status()==state::retry,"infinite pose accepted");
            m.begin(-1); feed(m); require(m.status()==state::ready,"retry could not recover");
        } else if (test=="curves") {
            for (int i=0;i<6;++i) for (double shape:{1.2,2.,3.}) {
                axis_result r; r.positive={30,.9,3,300,true}; r.negative={20,.9,3,300,true}; r.target_positive=150; r.target_negative=120; r.deadzone=.5; r.shape=shape;
                require(generate(r),"curve generation failed"); const auto first=r.positive_points;
                require(generate(r) && r.positive_points.size()==first.size(),"generation not deterministic");
                for (std::size_t k=0;k<first.size();++k) require(first[k].input==r.positive_points[k].input && first[k].output==r.positive_points[k].output,"generation changed");
                for (const auto* p:{&r.positive_points,&r.negative_points}) { require(p->front().input==0 && p->front().output==0,"origin discontinuity"); for (std::size_t k=1;k<p->size();++k) require((*p)[k].input>(*p)[k-1].input && (*p)[k].output>=(*p)[k-1].output,"control points not monotonic"); }
                require(r.positive_points.back().input==30 && r.negative_points.back().input==20,"physical range lost");
                r.deadzone=25; require(!generate(r),"invalid deadzone accepted"); r.deadzone=0; r.shape=std::numeric_limits<double>::quiet_NaN(); require(!generate(r),"NaN shape accepted");
            }
        } else if (test=="queue") {
            stream queue; queue.push(sample(0)); require(queue.drain().empty(),"disabled calibration collected data"); queue.enable(true);
            for (int k=0;k<1100;++k) queue.push(sample(k*10)); auto samples=queue.drain(); require(samples.size()==1024 && !samples.front().valid,"overflow did not force retry");
            queue.enable(false); queue.push(sample(0)); require(queue.drain().empty(),"inactive queue collected");
        } else if (test=="queue-threads") {
            stream queue; queue.enable(true); std::atomic<bool> done=false; unsigned valid=0;
            std::thread producer([&] { for (int k=0;k<30000;++k) { queue.push(sample(k*10)); if (k%20==0) std::this_thread::yield(); } done.store(true); });
            timestamp previous{}; do { for (const auto& s:queue.drain()) if (s.valid) { require(s.captured>previous && s.center_epoch==7,"queue ordering/data race"); previous=s.captured; ++valid; } } while (!done.load());
            producer.join(); require(valid>0,"thread queue empty");
        } else throw std::runtime_error("unknown calibration test");
        std::cout<<"calibration "<<test<<": PASS\n"; return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
