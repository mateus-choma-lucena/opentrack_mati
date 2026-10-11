/* Copyright (c) 2021 Michael Welter <michael@welter-4d.de>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#pragma once

#include "deadzone_filter.h"
#include "model_adapters.h"
#include "preview.h"
#include "ui_neuralnet-trackercontrols.h"

#include "api/plugin-api.hpp"
#include "compat/timer.hpp"
#include "cv/affine.hpp"
#include "cv/translation-calibrator.hpp"
#include "cv/video-widget.hpp"
#include "options/options.hpp"
#include "video/camera.hpp"
#include "enhanced/latest-frame.hpp"
#include "enhanced/reconnect.hpp"
#include "enhanced/options.hpp"
#include "enhanced/neuralnet2-options.hpp"

#include <QDialog>
#include <QHBoxLayout>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <QTimer>

#include <array>
#include <cinttypes>
#include <limits>
#include <memory>
#include <thread>
#include <atomic>
#include <QLabel>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace neuralnet_tracker_ns
{

using namespace options;

enum fps_choices
{
    fps_default = 0,
    fps_30 = 1,
    fps_60 = 2,
    fps_75 = 3,
    fps_125 = 4,
    fps_200 = 5,
    fps_50 = 6,
    fps_100 = 7,
    fps_120 = 8,
    fps_300 = 9,
    fps_250 = 10,
    fps_90 = 11,
    fps_MAX,
};

struct resolution_tuple
{
    int width;
    int height;
};

static const std::array<resolution_tuple, 10> resolution_choices = { {
    { 320, 240 },
    { 640, 400 },
    { 640, 480 },
    { 800, 600 },
    { 1024, 768 },
    { 1280, 720 },
    { 1920, 1080 },
    { 2560, 1440 },
    { 3840, 2160 },
    { 0, 0 },
} };

struct Settings : opts
{
    value<int> offset_fwd{ b, "offset-fwd", -100 }, // Millimeters
        offset_up{ b, "offset-up", 0 }, offset_right{ b, "offset-right", 0 };
    value<QString> camera_name{ b, "camera-name", "" };
    value<int> fov{ b, "field-of-view", 56 };
    value<fps_choices> force_fps{ b, "force-fps", fps_default };
    value<bool> show_network_input{ b, "show-network-input", false };
    value<double> roi_filter_alpha{ b, "roi-filter-alpha", 1. };
    value<double> roi_zoom{ b, "roi-zoom", 1. };
    value<bool> use_mjpeg{ b, "use-mjpeg", false };
    value<int> num_threads{ b, "num-threads", 2 };
    value<int> resolution{ b, "force-resolution", 0 };
    value<double> deadzone_size{ b, "deadzone-size", 1. };
    value<double> deadzone_hardness{ b, "deadzone-hardness", 1.5 };
    value<QString> posenet_file{ b, "posenet-file", "head-pose-0.5-small.onnx" };
    value<bool> internal_filter_enabled{ b, "internal-filter-enabled", true };
    value<bool> low_latency{ b, "enhanced-low-latency", false };
    value<double> max_frame_age{ b, "enhanced-max-frame-age-ms", 50. };
    value<bool> reconnect{ b, "enhanced-reconnect", false };
    value<int> reconnect_attempts{ b, "enhanced-reconnect-attempts", 5 };
    value<bool> diagnostics{ b, "enhanced-diagnostics", false };
    Settings();
};

struct CamIntrinsics
{
    float focal_length_w;
    float focal_length_h;
    float fov_w;
    float fov_h;
};


class ImagePyramid
{
public:
    // Stores the image as finest level using ref counting.
    // Then fills the pyramid levels. Only allocates on successive calls when image size changes.
    // Also generates greyscale versions.
    void init(const cv::Mat& image);
    const cv::Mat& image(int level=0) const { return images_[level]; }
    const cv::Mat& greyscale(int level=0) const { return greyscale_[level]; }
    const cv::Mat& coarsest_greyscale() const { return greyscale_[depth_-1]; }
    const cv::Mat& coarsest_image() const { return images_[depth_-1]; }
    int depth() const { return depth_; }
    static constexpr int max_levels = 4;
private:
    std::array<cv::Mat, static_cast<std::size_t>(max_levels)> images_ = {}; // Image pyramid
    std::array<cv::Mat, static_cast<std::size_t>(max_levels)> greyscale_ = {};
    int depth_ = 0;
};


// Tries to find a pyramid level for the "desired_roi_with".
// Given "current_roi_width" on the "current_level" getting the same crop on a coarser or finer level
// decreases or increases the resolution respectively. Thus this function figures out a level which
// matches the desired resolution, returning the level with the smallest resolution which is still higher than
// the desired one.
// Return scaling factor and desired level.
std::tuple<float, int> get_matching_level(const ImagePyramid& pyramid, int current_level, float current_roi_width, float desired_roi_width);


class NeuralNetTracker : protected virtual QThread, public ITracker
{
    // Q_OBJECT
public:
    NeuralNetTracker();
    ~NeuralNetTracker() override;
    module_status start_tracker(QFrame* frame) override;
    void data(double* data) override;
    void data_with_info(double* data, tracking_sample_info& sample) override;
    bool center() override;
    void enhanced_feedback(bool filter, double horizon, double rotation, double translation) override;
    void run() override;
    Affine pose();
    std::tuple<cv::Size, double, double> stats() const;
    enhanced::snapshot diagnostics() const;
    std::optional<enhanced::snapshot> performance_snapshot() const override { return diagnostics(); }
    void head_movement_feedback(const enhanced::head_controls_snapshot& value) override { telemetry_.head_controls(value); }
    void output_finished(const tracking_sample_info& sample, enhanced::timestamp finished) override
    { telemetry_.output(sample.captured, finished); }

    QMutex camera_mtx_;
    std::unique_ptr<video::impl::camera> camera_;

private:
    bool detect();
    bool open_camera(bool reconnecting = false);
    void copy_frame(const video::frame& frame, cv::Mat& dest) const;
    static bool maybe_load_onnxruntime_dynamically();
    bool acquire(cv::Mat& dest, enhanced::timestamp& time);
    void capture_loop();
    void invalidate_tracking();
    bool load_and_initialize_model();
    void draw_gizmos(const std::optional<PoseEstimator::Face>& face, const Affine& pose);
    void update_fps(double dt);
    // Secretly applies filtering while computing the pose in 3d space.
    QuatPose compute_filtered_pose(const PoseEstimator::Face& face);
    // Compute the pose in 3d space taking the network outputs
    QuatPose transform_to_world_pose(const cv::Quatf& face_rotation, const cv::Point2f& face_xy, const float face_size) const;
    QString get_posenet_filename() const;

    Settings settings_;
    neuralnet2_settings nn2_settings_;
    mutable enhanced::neuralnet_pipeline nn2_pipeline_;
    mutable enhanced::neuralnet_result nn2_result_;
    enhanced::neuralnet_parameters nn2_processed_parameters_; // protected by mtx_
    std::optional<PoseEstimator::Face> pending_face_;
    enhanced::pose pending_raw_{};
    bool nn2_frame_enabled_ = false;
    std::atomic<std::uint64_t> center_generation_{0};
    bool center_pending_ = false; // protected by mtx_
    enhanced::telemetry telemetry_;
    enhanced::latest_frame<cv::Mat> frames_;
    enhanced::reconnect_state reconnect_state_;
    std::atomic<bool> capture_stop_{false};
    std::atomic<bool> capture_valid_{true};
    std::atomic<enhanced::clock::duration::rep> invalidated_at_{0};
    std::thread capture_thread_;
    bool low_latency_ = false;
    enhanced::timestamp current_frame_time_{}, published_frame_time_{};
    double frame_dt_ = 0;

    // CAUTION: destruction order matters here. The env and allocator must
    // live longer than the ORT sessions. It looked like they were reference
    // counted but apparently they are not.
    // Even null ORT handles call the API from their destructors. Keep them
    // disengaged until the runtime API has actually initialized.
    std::optional<Ort::MemoryInfo> allocator_info_;
    std::optional<Ort::Env> env_;
    std::optional<Localizer> localizer_;
    std::optional<PoseEstimator> poseestimator_;

    cv::Mat frame_copy_ = {};
    ImagePyramid pyramid_ = {};
    CamIntrinsics intrinsics_ = {};

    std::optional<cv::Rect2f> last_localizer_roi_;
    std::optional<cv::Rect2f> last_roi_;
    static constexpr float HEAD_SIZE_MM = 200.f; // In the vertical. Approximately.

    mutable QMutex stats_mtx_;
    double fps_ = 0;
    double inference_time_ = 0;
    cv::Size resolution_ = {};

    static constexpr double RC = .25;
    int num_threads_ = 1;
    bool is_visible_ = true;

    mutable QMutex mtx_ = {}; // Protects the pose
    std::optional<QuatPose> last_pose_ = {};
    Affine last_pose_affine_ = {};
    Affine pending_pose_affine_ = {};
    bool tracking_valid_ = false;

    Preview preview_;
    std::unique_ptr<cv_video_widget> video_widget_;
    std::unique_ptr<QHBoxLayout> layout_;
};

class NeuralNetDialog : public ITrackerDialog
{
    Q_OBJECT
public:
    NeuralNetDialog();
    void register_tracker(ITracker* x) override;
    void unregister_tracker() override;

    bool embeddable() noexcept override;
    void set_buttons_visible(bool x) override;

private:
    void make_fps_combobox();
    void make_resolution_combobox();

    Ui::Form ui_;
    Settings settings_;
    enhanced_settings enhanced_settings_;
    neuralnet2_settings nn2_settings_;
    QLabel* diagnostics_label_ = nullptr;
    QLabel* camera_modes_label_ = nullptr;
    void make_enhanced_controls();
    // Calibration code mostly taken from point tracker
    QTimer calib_timer_;
    TranslationCalibrator trans_calib_;
    QMutex calibrator_mutex_;
    QTimer tracker_status_poll_timer_;
    NeuralNetTracker* tracker_ = nullptr;

private Q_SLOTS:
    void save() override;
    void reload() override;
    void doOK();
    void doCancel();
    void camera_settings();
    void update_camera_settings_state(const QString& name);
    void startstop_trans_calib(bool start);
    void trans_calib_step();
    void status_poll();
    void onSelectPoseNetFile();
};

class NeuralNetMetadata : public Metadata
{
    Q_OBJECT
    QString name() override { return QString("NeuralNet Tracker"); }
    QIcon icon() override { return QIcon(":/images/neuralnet.png"); }
};

} // namespace neuralnet_tracker_ns

namespace neuralnet_tracker_tests
{

void run();

}

using neuralnet_tracker_ns::NeuralNetDialog;
using neuralnet_tracker_ns::NeuralNetMetadata;
using neuralnet_tracker_ns::NeuralNetTracker;
