#include "module.hpp"
#include "compat/library-path.hpp"
#include "compat/sleep.hpp"
#include "compat/run-in-thread.hpp"
#include "ps3eye-driver/ps3eye.hpp"
#include <QCheckBox>
#include <cstring>
#include "enhanced/ipc-lock.hpp"

#include <cstddef>
#include <thread>

#include <QCoreApplication>
#include <QMessageBox>

#include <libusb.h>

using namespace options;

#ifdef __GNUG__
#   pragma clang diagnostic ignored "-Wcast-qual"
#endif

int device_count()
{
    libusb_context * ctx = nullptr;
    libusb_device **list = nullptr;
    ssize_t sz = 0;
    int rc = 0, cnt = 0;

    constexpr int vendor_id = 0x1415;
    constexpr int product_id = 0x2000;

    rc = libusb_init(&ctx);

    if (rc)
        goto end;

    sz = libusb_get_device_list(ctx, &list);

    if (sz < 0)
        goto end;

    for (int i = 0; i < sz; ++i) {
        libusb_device *device = list[i];
        libusb_device_descriptor desc = {};

        if (libusb_get_device_descriptor(device, &desc))
            continue;

        if (desc.idVendor == vendor_id && desc.idProduct == product_id)
            cnt++;
    }

end:
    if (list)
        libusb_free_device_list(list, 1);
    if (ctx)
        libusb_exit(ctx);

    return cnt;
}

#ifdef __linux__
#   include <unistd.h>
#endif

bool check_device_exists()
{
#ifdef __linux__
    // don't show when system driver exists
    if (!access("/sys/module/gspca_ov534", R_OK|X_OK))
        return false;
#endif

    return device_count() > 0;
}

static const QString camera_name = QStringLiteral("PS3 Eye open driver");

std::vector<QString> ps3eye_camera_::camera_names() const
{
    if (check_device_exists())
        return { camera_name };
    else
        return {};
}
std::unique_ptr<camera> ps3eye_camera_::make_camera(const QString& name)
{
    if (name == camera_name && check_device_exists())
        return std::make_unique<ps3eye_camera>();
    else
        return {};
}

static bool show_dialog_()
{
    auto& dlg = *new dialog;
    dlg.setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);
    dlg.setAttribute(Qt::WA_DeleteOnClose);
    dlg.adjustSize(); dlg.setFixedSize(dlg.size());
    dlg.show();
    return true;
}

bool ps3eye_camera_::show_dialog(const QString&)
{
    return show_dialog_();
}

bool ps3eye_camera_::can_show_dialog(const QString& name)
{
    return name == camera_name && check_device_exists();
}

video::camera_capabilities ps3eye_camera_::capabilities(const QString& name) const
{
    video::camera_capabilities result;
    if (name != camera_name || !check_device_exists()) return result;
    result.usb_vid = 0x1415; result.usb_pid = 0x2000;
    result.exposure = result.gain = result.automatic_exposure = true;
    for (const auto mode : {ps3eye::res_VGA, ps3eye::res_QVGA})
        for (int fps=15; fps <= (mode == ps3eye::res_VGA ? 75 : 187); ++fps)
            if (ps3eye::camera::normalize_framerate(fps, mode) == fps)
                result.modes.push_back({mode == ps3eye::res_VGA ? 640 : 320, mode == ps3eye::res_VGA ? 480 : 240, fps});
    return result;
}

ps3eye_camera::ps3eye_camera() = default;

ps3eye_camera::~ps3eye_camera()
{
    stop();
}

void ps3eye_camera::stop()
{
    open = false;

    if (shm.success())
    {
        volatile auto& ptr = *(ps3eye::shm*)shm.ptr();
        ptr.in.do_exit = 1;
        std::atomic_thread_fence(std::memory_order_seq_cst);
        run_in_thread_sync(qApp, [this] {
            if (!wrapper) return;
            wrapper->waitForFinished(1000);

            if (wrapper->state() != QProcess::NotRunning)
                wrapper->kill();
            wrapper->waitForFinished(1000);
            wrapper = std::nullopt;
        });
    }
}

bool ps3eye_camera::start(info& args)
{
    if (!shm.success())
        return false;

    stop();

    std::memset(shm.ptr(), 0, sizeof(ps3eye::shm));
    timecode = 0; dropped = 0;

    volatile auto& ptr = *(ps3eye::shm*)shm.ptr();
    QString error;

    using mode = ps3eye::shm_in::mode;

    open = false;
    fr = {};
    fr.channels = args.num_channels == 1 ? 1 : 3;
    fr.channel_size = 1;

    if (!args.width || args.width > 320)
    {
        ptr.in.resolution = mode::vga;
        fr.width = 640; fr.height = 480;
    }
    else
    {
        ptr.in.resolution = mode::qvga;
        fr.width = 320; fr.height = 240;
    }

    ptr.in.auto_gain = *s.auto_gain;
    const auto driver_mode = ptr.in.resolution == mode::vga ? ps3eye::res_VGA : ps3eye::res_QVGA;
    const int requested = args.fps > 0 ? args.fps : 30;
    const int normalized = ps3eye::camera::normalize_framerate(std::clamp(requested, 15, driver_mode == ps3eye::res_VGA ? 75 : 187), driver_mode);
    if (requested != normalized) qWarning() << "PS3 Eye requested FPS" << requested << "negotiated" << normalized;
    ptr.in.framerate = std::uint8_t(normalized);
    args.width = fr.width; args.height = fr.height; args.fps = normalized;
    ptr.in.gain = (uint8_t)s.gain;
    ptr.in.exposure = (uint8_t)s.exposure;
    ptr.in.channels = args.num_channels == 1 ? 1 : 3;

    sleep_ms = std::clamp(int(std::floor(450./ptr.in.framerate)), 1, 10);

    static const QString library_path(OPENTRACK_BASE_PATH + OPENTRACK_LIBRARY_PATH);

    run_in_thread_sync(qApp, [this] {
        wrapper.emplace(qApp);

        wrapper->setWorkingDirectory(library_path);
#ifdef _WIN32
        wrapper->setProgram(library_path + "/ps3eye-subprocess.exe");
        wrapper->setProcessChannelMode(QProcess::ForwardedChannels);
#else
        wrapper->setProgram(library_path + "/ps3eye-subprocess");
#endif
        wrapper->start();
    });

    constexpr int sleep_ms = 10, max_sleeps = 2000/sleep_ms;

    for (int i = 0; i < max_sleeps; i++)
    {
        if (enhanced::frame_sequence(static_cast<ps3eye::shm*>(shm.ptr())->out.timecode) > 0)
            goto ok;
        portable::sleep(sleep_ms);
    }

    if (ptr.out.error_string[0] == '\0')
        error = QString{};
    else
        error = QString::fromLatin1((const char*)ptr.out.error_string,
                                    strnlen((const char*)ptr.out.error_string, sizeof(ptr.out.error_string)));

    if (!args.quiet)
        run_in_thread_async(qApp, [error = std::move(error)] { dialog::show_open_failure_msgbox(error); });
    else
        qWarning() << "PS3 Eye reconnect failed:" << error;

    return false;

ok:
    open = true;
    fr.dropped_frames = 0;
    return true;
}

std::tuple<const frame&, bool> ps3eye_camera::get_frame()
{
    if (!shm.success() || !open) return {fr, false};
    auto& shared = *static_cast<ps3eye::shm*>(shm.ptr());
    const int elapsed = std::min(int(std::ceil(t.elapsed_ms())), 100);
    portable::sleep(std::max(0, sleep_ms-elapsed));
    for (int i = 0; i < 2000; i++)
    {
        if (enhanced::frame_sequence(shared.out.timecode) != timecode && enhanced::try_frame_lock(shared.out.frame_lock))
        {
            const auto tc = enhanced::frame_sequence(shared.out.timecode);
            if (timecode) dropped += std::uint32_t(tc-timecode)-1;
            timecode = tc;
            std::memcpy(data, shared.out.data_640x480, std::size_t(fr.width)*fr.height*fr.channels);
            enhanced::unlock_frame(shared.out.frame_lock);
            fr.data = data; fr.dropped_frames = dropped; t.start();
            return {fr, true};
        }
        portable::sleep(1);
    }

    open = false;
    return { fr, false };

}

bool ps3eye_camera::show_dialog()
{
    return show_dialog_();
}

OTR_REGISTER_CAMERA(ps3eye_camera_)

dialog::dialog(QWidget* parent) : QWidget(parent)
{
    ui.setupUi(this);
    auto* automatic = new QCheckBox(tr("Automatic exposure / gain"), this);
    ui.verticalLayout->insertWidget(1, automatic);
    tie_setting(s.auto_gain, automatic);
    connect(automatic, &QCheckBox::toggled, this, [this] { t.stop(); t.start(); });
    t.setInterval(500); t.setSingleShot(true);
    tie_setting(s.exposure, ui.exposure_slider);
    tie_setting(s.gain, ui.gain_slider);
    ui.exposure_label->setValue((int)*s.exposure);
    ui.gain_label->setValue((int)*s.gain);
    connect(&s.exposure, value_::value_changed<slider_value>(), this, [this](const slider_value&) { t.stop(); t.start(); });
    connect(&s.gain, value_::value_changed<slider_value>(), this, [this](const slider_value&) { t.stop(); t.start(); });
    connect(ui.exposure_slider, &QSlider::valueChanged, ui.exposure_label, &QSpinBox::setValue);
    connect(ui.gain_slider, &QSlider::valueChanged, ui.gain_label, &QSpinBox::setValue);
    connect(ui.buttonBox, &QDialogButtonBox::accepted, this, &dialog::do_ok);
    connect(ui.buttonBox, &QDialogButtonBox::rejected, this, &dialog::do_cancel);
    connect(&t, &QTimer::timeout, this, [this] { s.set_exposure(); s.set_gain(); s.set_auto_gain(); });
}
void dialog::show_open_failure_msgbox(const QString& error)
{
    const QString& error_ = error.isNull() ? tr("Unknown error") : error;
    QMessageBox::critical(nullptr,
                          tr("Can't open camera"),
                          tr("PS3 Eye driver error: %1").arg(error_),
                          QMessageBox::Close);
}

// XXX copypasta -sh 20200329
void settings::set_gain()
{
    if (!shm.success())
        return;

    auto& ptr = *(ps3eye::shm volatile*)shm.ptr();
    ptr.in.gain = (unsigned char)*gain;
    ++ptr.in.settings_updated;
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

void settings::set_exposure()
{
    if (!shm.success())
        return;

    auto& ptr = *(ps3eye::shm volatile*)shm.ptr();
    ptr.in.exposure = (unsigned char)*exposure;
    ++ptr.in.settings_updated;
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

void settings::set_auto_gain()
{
    if (!shm.success()) return;
    auto& ptr = *(ps3eye::shm volatile*)shm.ptr();
    ptr.in.auto_gain = *auto_gain; ++ptr.in.settings_updated;
    std::atomic_thread_fence(std::memory_order_seq_cst);
}
