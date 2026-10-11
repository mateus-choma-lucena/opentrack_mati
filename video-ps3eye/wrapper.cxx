#include "shm-layout.hpp"
#include "shm.hpp"

#include "ps3eye-driver/ps3eye.hpp"

#include <cstdlib>
#include <atomic>
#include <thread>
#include <chrono>
#include <memory>
#include <cstring>
#include "enhanced/ipc-lock.hpp"

#ifdef __clang__
#   pragma clang diagnostic ignored "-Watomic-implicit-seq-cst"
#endif

#ifdef __GNUG__
#   pragma GCC diagnostic ignored "-Wcast-qual"
#   pragma GCC diagnostic ignored "-Wformat-security"
#   pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif

template<int N, typename... xs>
[[noreturn]]
static void error(volatile ps3eye::shm_out& out, const char (&error)[N], const xs&... args)
{
    out.status_ = ps3eye::shm_out::status::fail;
    snprintf((char*)out.error_string, sizeof(ps3eye::shm_out::error_string), error, args...);
    std::quick_exit(2);
}

static void update_settings(ps3eye::camera& camera, const volatile ps3eye::shm_in& in)
{
    //camera.set_framerate(in.framerate);
    camera.set_auto_gain(in.auto_gain);
    camera.set_gain(in.gain);
    camera.set_exposure(in.exposure);
    camera.set_test_pattern_status(in.test_pattern);
}

static ps3eye::resolution get_mode(ps3eye::shm_in::mode res)
{
    switch (res)
    {
    default:
    case ps3eye::shm_in::mode::qvga:
        return ps3eye::res_QVGA;
    case ps3eye::shm_in::mode::vga:
        return ps3eye::res_VGA;
    }
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;
    shm_wrapper mem_("ps3eye-driver-shm", nullptr, sizeof(ps3eye::shm));
    if (!mem_.success()) return 2;
    volatile auto& ptr_ = *(ps3eye::shm*)mem_.ptr();
    volatile auto& in = ptr_.in;
    volatile auto& out = ptr_.out;
    int num_channels = in.channels;

    auto cameras = ps3eye::list_devices();

    out.status_ = ps3eye::shm_out::status::starting;

    if (cameras.empty())
        error(out, "no camera found");

    auto& camera = cameras[0];
    camera->set_debug(true);
    auto storage = std::make_unique<uint8_t[]>(sizeof(out.data_640x480));
    auto* frame = storage.get();
    auto& shared = *static_cast<ps3eye::shm*>(mem_.ptr());
    decltype(out.timecode) timecode = 0;

    auto fmt = num_channels == 1 ? ps3eye::format::Gray : ps3eye::format::BGR;

    {
        int framerate = in.framerate;
        if (framerate <= 0)
            framerate = 60;

        if (!camera->init(get_mode(in.resolution), framerate, fmt))
            error(out, "camera init failed: %s", camera->error_string());

        update_settings(*camera, in);

        if (!camera->start())
            error(out, "can't start camera: %s", camera->error_string());
    }

    enhanced::publish_sequence(shared.out.timecode, 0);
    out.status_ = ps3eye::shm_out::status::running;
    in.do_exit = false;
    std::atomic_thread_fence(std::memory_order_seq_cst);

    for (;;)
    {
        if (in.do_exit) break;
        {
            auto cookie = in.settings_updated;
            if (cookie != out.settings_updated_ack)
            {
                camera->stop();
                update_settings(*camera, in);
                int framerate = in.framerate;
                if (framerate <= 0)
                    framerate = 60;
                if (!camera->init(get_mode(in.resolution), framerate, fmt))
                    error(out, "camera init failed: %s", camera->error_string());
                if (!camera->start())
                    error(out, "can't start camera: %s", camera->error_string());
                out.settings_updated_ack = cookie;
            }
        }

        if (!camera->get_frame(frame))
        {
            out.status_ = ps3eye::shm_out::status::fail;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (!camera->is_open()) break;
            continue;
        }

        ++timecode;
        if (enhanced::try_frame_lock(shared.out.frame_lock))
        {
            std::memcpy(shared.out.data_640x480, frame, std::size_t(camera->width())*camera->height()*num_channels);
            enhanced::publish_sequence(shared.out.timecode, timecode);
            enhanced::unlock_frame(shared.out.frame_lock);
        }

        if (in.do_exit)
            break;

        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    camera->stop();
    out.status_ = ps3eye::shm_out::status::terminate;
    return 0;
}
