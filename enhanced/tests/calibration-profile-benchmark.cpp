#include "enhanced/calibration.hpp"
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif
using namespace enhanced;
using namespace enhanced::calibration;
static double cpu_ms() {
#ifdef _WIN32
    FILETIME created{},exited{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user);
    const auto value=[](FILETIME t){return (std::uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
    return (value(kernel)+value(user))/10000.;
#else
    return 0;
#endif
}
static void report(const char* name,const std::vector<double>& us) {
    std::cout<<name<<","<<percentile(us,.5)<<","<<percentile(us,.95)<<","<<percentile(us,.99)<<"\n";
}
int main() {
    stream queue;measurement recording;observation sample{{},clock::now(),clock::now(),1,true,.95,.9};
    std::vector<double> idle,push,drain,add,summary;
    std::cout<<"Synthetic calibration operations; steady 75 Hz timestamps. Times are microseconds. Batch-amortized producer timing.\noperation,p50_us,p95_us,p99_us\n";
    for(int batch=0;batch<1000;++batch) {
        auto start=clock::now();for(int i=0;i<1000;++i)queue.push(sample);idle.push_back(milliseconds(start,clock::now())); // ms/1000 * 1000 us
    }
    queue.enable(true);
    for(int batch=0;batch<1000;++batch) {
        auto start=clock::now();for(int i=0;i<300;++i)queue.push(sample);push.push_back(milliseconds(start,clock::now())*1000/300);
        start=clock::now();auto samples=queue.drain();drain.push_back(milliseconds(start,clock::now())*1000/samples.size());
        if(samples.size()!=300)return 1;
        recording.begin(-1);start=clock::now();for(auto& s:samples){sample.captured+=std::chrono::microseconds(13333);sample.received=sample.captured;s=sample;recording.add(s);}add.push_back(milliseconds(start,clock::now())*1000/300);
        start=clock::now();const auto statistics=recording.stationary();summary.push_back(milliseconds(start,clock::now())*1000);
        if(statistics.valid_samples!=300 || statistics.ambiguous_motion)return 2;
    }
    report("disabled_push",idle);report("active_push",push);report("drain_per_observation",drain);report("record_per_observation",add);report("stationary_summary_300",summary);
    for(bool active:{false,true}) {
        queue.enable(active);recording.begin(-1);const auto start=clock::now();const double cpu=cpu_ms();
        for(int i=0;i<301;++i) {
            std::this_thread::sleep_until(start+std::chrono::microseconds(13333LL*i));
            sample.captured=sample.received=clock::now();queue.push(sample);if(active)for(const auto& s:queue.drain())recording.add(s);
        }
        const double elapsed=milliseconds(start,clock::now()),used=cpu_ms()-cpu;
        std::cout<<(active?"paced_recording":"paced_disabled")<<" samples="<<recording.count()<<" wall_ms="<<elapsed<<" process_cpu_ms="<<used<<" one_core_cpu_percent="<<100*used/elapsed<<" (Windows CPU counter granularity applies)\n";
    }
    return recording.status()==state::ready?0:3;
}
