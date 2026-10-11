#include "tracker-neuralnet/optical-flow.hpp"
#include "enhanced/neuralnet2.hpp"
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <stdexcept>
using namespace enhanced;
using namespace neuralnet_tracker_ns;
static timestamp at(double ms) {return timestamp{}+std::chrono::duration_cast<clock::duration>(std::chrono::duration<double,std::milli>(ms+1000));}
static void check(bool ok,const char* m){if(!ok)throw std::runtime_error(m);}
static cv::Mat texture()
{
    cv::Mat m(480,640,CV_8UC1);cv::RNG rng(123);rng.fill(m,cv::RNG::UNIFORM,0,255);cv::GaussianBlur(m,m,{3,3},0);return m;
}
static cv::Mat shifted(const cv::Mat& src,double x,double y=0)
{cv::Mat result;cv::warpAffine(src,result,cv::Mat(cv::Matx23d(1,0,x,0,1,y)),src.size(),cv::INTER_LINEAR,cv::BORDER_REFLECT);return result;}
int main(int argc,char** argv)
{
    try {
        cv::setNumThreads(1);
        const std::string mode=argc>1?argv[1]:"";auto image=texture();
        optical_flow flow;flow_anchor anchor{{.25f,.2f,.5f,.6f},at(0),1,1,true};
        flow.update(image,at(0),anchor,1,true);
        if(mode=="translation") {
            auto f=flow.update(shifted(image,2,1),at(13.333),anchor,1,true);
            check(f.valid && std::abs(f.dx-1)<.2 && std::abs(f.dy-.5)<.2 && f.points>=8 && f.captured==at(13.333),"LK image translation/timestamp incorrect");
        } else if(mode=="quality") {
            for(int q=0;q<3;++q) {
                optical_flow candidate;candidate.update(image,at(0),anchor,q,true);
                auto f=candidate.update(shifted(image,2),at(13),anchor,q,true);
                check(f.valid && f.points<=unsigned(q==0?32:q==1?48:80) && std::abs(f.dx-(q==2?1.25:1.))<.25,"quality mode failed or exceeded point bound");
            }
        } else if(mode=="drift") {
            bool bounded=false;for(int i=1;i<=7;++i) {auto f=flow.update(shifted(image,i*4),at(i*7),anchor,1,true);if(!f.valid) bounded=true;check(!f.valid || f.drift_px<=8,"drift escaped cap");}
            check(bounded,"accumulated flow never reinitialized");
        } else if(mode=="rejection") {
            auto f=flow.update(shifted(image,80),at(13),anchor,1,true);check(!f.valid,"implausible large flow accepted");
            cv::Mat blank(image.size(),CV_8UC1,cv::Scalar(0));flow.reset();flow.update(blank,at(0),anchor,1,true);
            check(!flow.update(blank,at(13),anchor,1,true).valid,"weak featureless face accepted");
        } else if(mode=="occlusion") {
            auto occluded=image.clone();occluded(cv::Rect(160,96,200,288)).setTo(0);
            auto f=flow.update(occluded,at(13),anchor,1,true);check(!f.valid,"major partial occlusion was trusted");
            anchor.valid=false;check(!flow.update(image,at(26),anchor,1,true).valid,"face lost hallucinated flow");
        } else if(mode=="background") {
            auto changed=shifted(image,40);image(cv::Rect(160,96,320,288)).copyTo(changed(cv::Rect(160,96,320,288)));
            auto f=flow.update(changed,at(13),anchor,1,true);check(f.valid && f.speed_px_s<1,"background contaminated face motion");
        } else if(mode=="reset") {
            anchor.valid=false;check(!flow.update(image,at(13),anchor,1,true).valid,"disconnect flow retained");
            anchor.valid=true;anchor.captured=at(26);++anchor.generation;
            auto first=flow.update(image,at(26),anchor,1,true);check(!first.valid,"reacquisition must seed");
            check(flow.update(shifted(image,2),at(39),anchor,1,true).valid,"reacquisition did not resume");
            ++anchor.generation;check(!flow.update(image,at(40),anchor,1,true).valid,"recenter must reset");
            cv::Mat smaller;cv::resize(image,smaller,{320,240});check(!flow.update(smaller,at(41),anchor,1,true).valid,"format change must reset");
        } else if(mode=="timestamps") {
            check(!flow.update(image,at(-1),anchor,1,true).valid,"future anchor accepted");
            check(!flow.update(image,at(200),anchor,1,true).valid,"stale anchor accepted");
            check(!flow.update(image,{},anchor,1,true).valid,"zero timestamp accepted");
        } else if(mode=="disabled") {
            auto f=flow.update(image,at(13),anchor,1,false);check(!f.enabled && !f.valid,"off is not exact fallback");
        } else if(mode=="benchmark") {
            std::cout<<"Synthetic texture + identical synthetic poses; pose-stage wall time, excludes ONNX/camera/display.\nmode,p50_ms,p95_ms,p99_ms,valid_outputs,accepted_measurements,held_outputs,rejected,flow_valid,flow_correction_deg\n";
            for(int variant=0;variant<4;++variant) {
                neuralnet_pipeline core;predictor predictor;optical_flow optical;window<1024> times;unsigned valid=0,accepted=0,held=0,flow_valid=0;double correction=0;neuralnet_parameters params;params.enabled=variant!=0;
                flow_anchor a{{.25f,.2f,.5f,.6f},at(0),1,1,true};neuralnet_result result;
                for(int i=0;i<1024;++i) {
                    auto frame=shifted(image,4*std::sin(i*.02));pose raw{};raw[3]=20*std::sin(i*.02);if(i%173==172)raw[3]+=100;
                    const auto stamp=at(i*13.333),start=clock::now();flow_observation f;
                    if(variant==2) f=optical.update(frame,stamp,a,1,true);
                    result=core.update(raw,stamp,stamp+std::chrono::milliseconds(1),params,true);
                    predictor.update(result.value,result.captured,result.valid && result.prediction_allowed);
                    auto prediction=predictor.predict(stamp+std::chrono::milliseconds(8),{true,8,3,.5,variant==3,15},{},result.value,result.diagnostics.reliability,{f,1});
                    if(variant==2) {a.captured=stamp;a.valid=result.valid && result.prediction_allowed;a.reliability=result.diagnostics.reliability;}
                    if(i>=24)times.add(milliseconds(start,clock::now()));
                    valid+=result.valid;accepted+=result.accepted;held+=result.holding;flow_valid+=f.valid;correction+=std::abs(prediction.flow_correction[3]);
                }
                std::cout<<variant<<','<<times.percentile(.5).value_or(0)<<','<<times.percentile(.95).value_or(0)<<','<<times.percentile(.99).value_or(0)<<','<<valid<<','<<accepted<<','<<held<<','<<result.diagnostics.rejected<<','<<flow_valid<<','<<correction<<'\n';
                if(variant==2)check(correction==0,"75/75 sequence must have no intermediate-frame assistance");
            }
        } else throw std::runtime_error("unknown test");
        std::cout<<mode<<": PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
