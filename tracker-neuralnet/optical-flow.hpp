#pragma once
#include "enhanced/flow.hpp"
#include <opencv2/core.hpp>
#include <vector>

namespace neuralnet_tracker_ns {
struct flow_anchor
{
    cv::Rect2f normalized_roi{};
    enhanced::timestamp captured{};
    double reliability=0;
    std::uint64_t generation=0;
    bool valid=false;
};
// One capture-thread owner. Inference exchanges only a small face anchor.
class optical_flow
{
    cv::Mat gray_, previous_, mask_, resized_;
    cv::Size format_{};
    std::vector<cv::Point2f> points_, next_, back_, kept_;
    std::vector<unsigned char> forward_status_, backward_status_;
    std::vector<float> errors_, backward_errors_, dx_, dy_, residuals_;
    std::vector<cv::Mat> previous_pyramid_, next_pyramid_;
    enhanced::timestamp last_{}, anchor_{}, seeded_{};
    std::uint64_t generation_=0, resets_=0;
    int quality_=-1;
    double drift_x_=0, drift_y_=0;
    bool active_=false;
    void seed(const cv::Rect& roi, int count, enhanced::timestamp time);
public:
    optical_flow();
    void reset();
    enhanced::flow_observation update(const cv::Mat& image, enhanced::timestamp time, const flow_anchor& anchor, int quality, bool enabled);
};
}
