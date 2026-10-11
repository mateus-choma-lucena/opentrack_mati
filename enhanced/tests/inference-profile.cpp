#include "tracker-neuralnet/model_adapters.h"
#include "enhanced/telemetry.hpp"
#include <opencv2/imgproc.hpp>
#include <QLibrary>
#include <QCoreApplication>
#include <QDir>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif
namespace neuralnet_tracker_ns {
cv::Quatf image_to_world(cv::Quatf q) {std::swap(q[1],q[3]);q[1]=-q[1];q[2]=-q[2];q[3]=-q[3];return q;}
}
using namespace neuralnet_tracker_ns;
using namespace enhanced;
static std::uint64_t working_set_kb()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if(GetProcessMemoryInfo(GetCurrentProcess(),&counters,sizeof(counters)))return counters.WorkingSetSize/1024;
#endif
    return 0;
}
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    if(argc<2)return 1;
    const QString stage=QString::fromLocal8Bit(argv[1]);
    const bool smoke=argc>2 && std::string(argv[2])=="smoke";
    QLibrary library(QDir(stage).filePath("modules/onnxruntime.dll"));
    if(!library.load()) {std::cerr<<library.errorString().toStdString();return 1;}
    const auto get_api=reinterpret_cast<const OrtApiBase*(ORT_API_CALL*)()>(library.resolve("OrtGetApiBase"));
    if(!get_api)return 1;
    Ort::InitApi(get_api()->GetApi(ORT_API_VERSION));
    try {
        cv::setNumThreads(1);
        std::cout<<"ONNX Runtime "<<get_api()->GetVersionString()<<"; available providers:";
        for(const auto& provider:Ort::GetAvailableProviders())std::cout<<' '<<provider;
        std::cout<<"\nCPU only, fixed grayscale synthetic inputs and forced pose ROI; no tracking-quality or sensor/display claim.\n";
        Ort::Env env(ORT_LOGGING_LEVEL_ERROR,"enhanced-inference-profile");
        auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
        if(argc>2 && std::string(argv[2])=="inspect") {
            Ort::AllocatorWithDefaultOptions allocator;
            for(const auto* model:{"head-pose-0.2-small.onnx","head-pose-0.2-big.onnx","head-pose-0.5-small.onnx","head-pose-0.5-big.onnx"}) {
                const auto path=QDir(stage).filePath(QString("modules/models/")+model).toStdWString();
                Ort::SessionOptions options;options.SetGraphOptimizationLevel(ORT_DISABLE_ALL);Ort::Session session(env,path.c_str(),options);
                std::cout<<model<<" version="<<session.GetModelMetadata().GetVersion()<<'\n';
                for(std::size_t i=0;i<session.GetOutputCount();++i) {
                    auto name=session.GetOutputNameAllocated(i,allocator);auto info=session.GetOutputTypeInfo(i);auto tensor=info.GetTensorTypeAndShapeInfo();
                    std::cout<<name.get()<<" shape=";for(auto d:tensor.GetShape())std::cout<<d<<' ';std::cout<<" type="<<tensor.GetElementType()<<'\n';
                }
            }return 0;
        }
        cv::Mat input(480,640,CV_8UC1);cv::RNG rng(123);rng.fill(input,cv::RNG::UNIFORM,0,255);
        cv::Mat small;cv::resize(input,small,{320,240});
        const cv::Rect roi(70,30,180,180);
        const std::array levels={ORT_DISABLE_ALL,ORT_ENABLE_BASIC,ORT_ENABLE_EXTENDED,ORT_ENABLE_ALL};
        std::cout<<"model,threads,graph,p50_total_ms,p95_total_ms,p99_total_ms,p50_run_ms,p95_run_ms,preprocess_p50_ms,working_set_before_kb,working_set_after_kb\n";
        for(const auto* model:{"head-localizer.onnx","head-pose-0.2-small.onnx","head-pose-0.2-big.onnx","head-pose-0.5-small.onnx","head-pose-0.5-big.onnx"})
            for(int threads:{1,2,4}) for(std::size_t optimization=0;optimization<levels.size();++optimization) {
                if(smoke && (threads!=1 || (optimization!=0 && optimization!=3)))continue;
                Ort::SessionOptions options;options.SetIntraOpNumThreads(threads);options.SetInterOpNumThreads(1);options.SetGraphOptimizationLevel(levels[optimization]);
                const auto path=QDir(stage).filePath(QString("modules/models/")+model).toStdWString();
                std::optional<Localizer> localizer;std::optional<PoseEstimator> estimator;
                if(std::string(model)=="head-localizer.onnx")localizer.emplace(memory,Ort::Session(env,path.c_str(),options));
                else estimator.emplace(memory,Ort::Session(env,path.c_str(),options));
                window<48> total,runs,preprocessing;
                std::uint64_t before=0;
                std::array<const void*,3> addresses{};
                for(int i=0;i<(smoke?5:43);++i) {
                    if(i==3) {before=working_set_kb();addresses=localizer?localizer->buffer_addresses():estimator->buffer_addresses();}
                    const auto start=clock::now();double run;
                    if(localizer) {localizer->run(small);run=localizer->last_inference_time_millis();}
                    else {estimator->run(small,roi);run=estimator->last_inference_time_millis();}
                    const double elapsed=milliseconds(start,clock::now());
                    if(i>=3 && addresses!=(localizer?localizer->buffer_addresses():estimator->buffer_addresses()))throw std::runtime_error("model adapter fixed-shape buffers reallocated");
                    if(i>=3){total.add(elapsed);runs.add(run);preprocessing.add(std::max(0.,elapsed-run));}
                }
                std::cout<<model<<','<<threads<<','<<optimization<<','<<total.percentile(.5).value_or(0)<<','<<total.percentile(.95).value_or(0)<<','<<total.percentile(.99).value_or(0)<<','
                    <<runs.percentile(.5).value_or(0)<<','<<runs.percentile(.95).value_or(0)<<','<<preprocessing.percentile(.5).value_or(0)<<','<<before<<','<<working_set_kb()<<'\n';
            }
        cv::Mat copy, resized, normalized;window<100> copies,gray_resize,normalize;const unsigned char* pointer=nullptr;unsigned reallocations=0;
        for(int i=0;i<100;++i) {
            auto t=clock::now();input.copyTo(copy);copies.add(milliseconds(t,clock::now()));
            t=clock::now();cv::resize(input,resized,{288,224},0,0,cv::INTER_AREA);gray_resize.add(milliseconds(t,clock::now()));
            t=clock::now();normalize_brightness(resized,normalized);normalize.add(milliseconds(t,clock::now()));
            if(pointer && normalized.data!=pointer)++reallocations;pointer=normalized.data;
        }
        std::cout<<"Reusable buffer probe after first allocation: pointer changes="<<reallocations<<"; copy P50 ms="<<copies.percentile(.5).value_or(0)<<"; resize P50 ms="<<gray_resize.percentile(.5).value_or(0)<<"; brightness P50 ms="<<normalize.percentile(.5).value_or(0)<<'\n';
        if(reallocations)throw std::runtime_error("fixed-shape buffer was reallocated");
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
