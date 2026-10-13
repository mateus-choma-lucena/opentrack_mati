#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/flann.hpp>
#include <opencv2/features2d.hpp>
#include <vector>

// This program is compiled and linked during dependency verification.
int main()
{
    cv::Mat image(64, 64, CV_8UC1, cv::Scalar(0));
    std::vector<cv::Point2f> points, next;
    std::vector<unsigned char> status, encoded;
    std::vector<float> errors;
    cv::goodFeaturesToTrack(image, points, 10, 0.01, 4);
    cv::calcOpticalFlowPyrLK(image, image, points, next, status, errors);
    cv::imencode(".png", image, encoded);
    cv::VideoCapture capture;
    cv::Mat rotation;
    cv::Rodrigues(cv::Vec3d(0, 0, 0), rotation);
    auto detector = cv::ORB::create();
    cv::flann::Index index;
    return static_cast<int>(cv::getWindowProperty("probe", cv::WND_PROP_VISIBLE));
}
