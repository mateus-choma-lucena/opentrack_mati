/* Copyright (c) 2019 Stanislaw Halik <sthalik@misaki.pl>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#pragma once

#include "video/camera.hpp"
#include <optional>
#include <opencv2/videoio.hpp>

namespace opencv_camera_impl {

using namespace video::impl;

struct metadata : camera_
{
    metadata();
    std::vector<QString> camera_names() const override;
    std::unique_ptr<camera> make_camera(const QString& name) override;
    bool can_show_dialog(const QString& camera_name) override;
    bool show_dialog(const QString& camera_name) override;
    video::camera_capabilities capabilities(const QString& camera_name) const override;
};

struct cam final : camera
{
static constexpr int video_capture_backend =
#ifdef _WIN32
    cv::CAP_DSHOW;
#elif !defined __APPLE__
    cv::CAP_V4L2;
#else
    // macOS: force AVFoundation. Was cv::CAP_ANY, which lets OpenCV
    // auto-pick between AVFoundation, FFmpeg, and GStreamer - each
    // uses a different device enumeration and index order, so the
    // user-visible camera name in the dropdown couldn't be mapped to
    // a stable device index. Our get_camera_names() (via
    // compat/camera-names-apple.mm) enumerates with AVFoundation's
    // devicesWithMediaType:, so pinning the OpenCV backend to the
    // same API guarantees the index we return is the index OpenCV
    // will open.
    cv::CAP_AVFOUNDATION;
#endif

    cam(int idx);
    ~cam() override;

    bool start(info& args) override;
    void stop() override;
    bool is_open() override;
    std::tuple<const frame&, bool> get_frame() override;
    bool show_dialog() override;

    bool get_frame_();

    std::optional<cv::VideoCapture> cap;
    cv::Mat mat;
    frame frame_;
    int idx = -1;
};

} // ns opencv_camera_impl
