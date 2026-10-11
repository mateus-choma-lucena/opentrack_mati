#include "optical-flow.hpp"
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

namespace neuralnet_tracker_ns {
namespace {
float median(std::vector<float>& values)
{
    if (values.empty()) return 0;
    auto middle=values.begin()+values.size()/2;
    std::nth_element(values.begin(),middle,values.end()); return *middle;
}
}
optical_flow::optical_flow()
{
    for (auto* p : {&points_,&next_,&back_,&kept_}) p->reserve(80);
    for (auto* p : {&errors_,&backward_errors_,&dx_,&dy_,&residuals_}) p->reserve(80);
    forward_status_.reserve(80); backward_status_.reserve(80);
    previous_pyramid_.reserve(6); next_pyramid_.reserve(6);
}
void optical_flow::reset()
{
    points_.clear(); previous_.release(); previous_pyramid_.clear(); next_pyramid_.clear();
    last_=anchor_=seeded_={}; drift_x_=drift_y_=0; active_=false; ++resets_;
}
void optical_flow::seed(const cv::Rect& roi, int count, enhanced::timestamp time)
{
    mask_.create(gray_.size(),CV_8UC1); mask_.setTo(0);
    // An inset ellipse limits features to the facial interior rather than the
    // rectangle's background corners. ROI alone cannot identify all occluders.
    cv::ellipse(mask_,cv::Point(roi.x+roi.width/2,roi.y+roi.height/2),
                cv::Size(roi.width*35/100,roi.height*38/100),0,0,360,cv::Scalar(255),-1);
    cv::goodFeaturesToTrack(gray_,points_,count,.02,5,mask_,5,false,.04);
    gray_.copyTo(previous_); last_=seeded_=time;
    previous_pyramid_.clear(); drift_x_=drift_y_=0;
}
enhanced::flow_observation optical_flow::update(const cv::Mat& image, enhanced::timestamp time,
                                               const flow_anchor& a, int quality, bool enabled)
{
    const auto started=enhanced::clock::now();
    enhanced::flow_observation out; out.enabled=enabled; out.captured=time; out.anchor=a.captured;
    auto finish=[&] { out.resets=resets_; out.cpu_ms=enhanced::milliseconds(started,enhanced::clock::now()); return out; };
    if (!enabled || !a.valid || a.reliability<=0 || !std::isfinite(a.reliability) || image.empty() ||
        image.depth()!=CV_8U || (image.channels()!=1 && image.channels()!=3) ||
        enhanced::stale(a.captured,time,50))
    { if (active_) reset(); return finish(); }
    quality=std::clamp(quality,0,2);
    const auto r=a.normalized_roi;
    if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.width) || !std::isfinite(r.height) || r.width<=0 || r.height<=0)
    { if (active_) reset(); return finish(); }
    const int width=quality==2?400:320;
    cv::resize(image,resized_,cv::Size(width,std::max(8,image.rows*width/image.cols)),0,0,cv::INTER_AREA);
    if (image.channels()==3) cv::cvtColor(resized_,gray_,cv::COLOR_BGR2GRAY); else resized_.copyTo(gray_);
    const cv::Rect bounds(0,0,gray_.cols,gray_.rows);
    const cv::Rect roi=cv::Rect(cv::Point(int(std::clamp(r.x,0.f,1.f)*gray_.cols),int(std::clamp(r.y,0.f,1.f)*gray_.rows)),
        cv::Size(int(std::clamp(r.width,0.f,1.f)*gray_.cols),int(std::clamp(r.height,0.f,1.f)*gray_.rows))) & bounds;
    if (roi.width<20 || roi.height<20) { reset(); return finish(); }
    const int count=quality==0?32:quality==1?48:80;
    const bool restart=!active_ || format_!=image.size() || quality_!=quality || generation_!=a.generation ||
        (last_!=enhanced::timestamp{} && (time<=last_ || enhanced::milliseconds(last_,time)>60));
    if (restart)
    {
        reset(); format_=image.size(); quality_=quality; generation_=a.generation;
        active_=true; anchor_=a.captured; seed(roi,count,time); out.points=unsigned(points_.size()); return finish();
    }
    if (anchor_!=a.captured) { anchor_=a.captured; drift_x_=drift_y_=0; }
    if (points_.size()<8 || enhanced::milliseconds(seeded_,time)>250)
    { seed(roi,count,time); out.points=unsigned(points_.size()); return finish(); }
    const double dt=enhanced::milliseconds(last_,time)*.001;
    if (dt<=0 || dt>.060) { reset(); return finish(); }
    const int levels=quality==0?1:2;
    const cv::Size window(15,15);
    cv::buildOpticalFlowPyramid(previous_,previous_pyramid_,window,levels);
    cv::buildOpticalFlowPyramid(gray_,next_pyramid_,window,levels);
    cv::calcOpticalFlowPyrLK(previous_pyramid_,next_pyramid_,points_,next_,forward_status_,errors_,window,levels,
        cv::TermCriteria(cv::TermCriteria::COUNT|cv::TermCriteria::EPS,quality==2?20:12,.03));
    cv::calcOpticalFlowPyrLK(next_pyramid_,previous_pyramid_,next_,back_,backward_status_,backward_errors_,window,levels,
        cv::TermCriteria(cv::TermCriteria::COUNT|cv::TermCriteria::EPS,quality==2?20:12,.03));
    dx_.clear(); dy_.clear(); kept_.clear();
    for (std::size_t i=0;i<points_.size();++i)
    {
        const auto d=next_[i]-points_[i];
        if (!forward_status_[i] || !backward_status_[i] || errors_[i]>20 || backward_errors_[i]>20 ||
            !std::isfinite(d.x) || !std::isfinite(d.y) || cv::norm(back_[i]-points_[i])>.8 ||
            cv::norm(d)>std::min(20.,600.*dt) || !roi.contains(cv::Point(int(next_[i].x),int(next_[i].y)))) continue;
        dx_.push_back(d.x); dy_.push_back(d.y); kept_.push_back(next_[i]);
    }
    const auto candidates=kept_.size();
    const float mx=median(dx_), my=median(dy_);
    residuals_.clear();
    // Recompute vectors because median sorting changes the component arrays.
    for (std::size_t i=0;i<points_.size();++i)
        if (forward_status_[i] && backward_status_[i] && std::isfinite(next_[i].x) && std::isfinite(next_[i].y))
            residuals_.push_back(float(cv::norm((next_[i]-points_[i])-cv::Point2f(mx,my))));
    const double tolerance=std::clamp(double(median(residuals_))*3.,.6,2.5);
    kept_.clear();
    for (std::size_t i=0;i<points_.size();++i)
    {
        const auto d=next_[i]-points_[i];
        if (forward_status_[i] && backward_status_[i] && std::isfinite(d.x) && std::isfinite(d.y) && errors_[i]<=20 && backward_errors_[i]<=20 &&
            cv::norm(back_[i]-points_[i])<=.8 && cv::norm(d)<=std::min(20.,600.*dt) &&
            cv::norm(d-cv::Point2f(mx,my))<=tolerance && roi.contains(cv::Point(int(next_[i].x),int(next_[i].y)))) kept_.push_back(next_[i]);
    }
    out.points=unsigned(kept_.size()); out.rejected=unsigned(points_.size()-kept_.size());
    const double fraction=double(kept_.size())/points_.size();
    bool coverage=false;
    if (kept_.size()>=8)
    { auto span=cv::boundingRect(kept_); coverage=span.width>roi.width*.25 && span.height>roi.height*.25; }
    if (candidates>=8 && kept_.size()>=8 && fraction>=.65 && coverage)
    {
        drift_x_+=mx; drift_y_+=my; out.drift_px=std::hypot(drift_x_,drift_y_);
        out.valid=out.drift_px<=8;
        out.dx=mx; out.dy=my; out.speed_px_s=std::hypot(mx,my)/dt;
        out.reliability=std::clamp(a.reliability*fraction*(1-std::min(tolerance,2.5)/5),0.,1.);
    }
    if (!out.valid) { seed(roi,count,time); return finish(); }
    points_.swap(kept_); gray_.copyTo(previous_); last_=time;
    return finish();
}
}
