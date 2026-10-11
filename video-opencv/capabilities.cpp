#include "impl.hpp"
#include "compat/camera-names.hpp"

#ifdef _WIN32
#define NO_DSHOW_STRSAFE
#include <dshow.h>
#include <dvdmedia.h>
#include <wrl/client.h>
#include <QRegularExpression>
#include <cmath>

namespace {
using Microsoft::WRL::ComPtr;
struct com_scope {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~com_scope() { if (SUCCEEDED(result)) CoUninitialize(); }
};
void free_media_type(AM_MEDIA_TYPE* type)
{
    if (!type) return;
    CoTaskMemFree(type->pbFormat);
    if (type->pUnk) type->pUnk->Release();
    CoTaskMemFree(type);
}
}
#endif

namespace opencv_camera_impl {
video::camera_capabilities metadata::capabilities(const QString& name) const
{
    video::camera_capabilities result;
#ifdef _WIN32
    const int selected = camera_name_to_index(name);
    if (selected < 0) return result;
    com_scope apartment;
    if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE) return result;
    ComPtr<ICreateDevEnum> devices;
    if (FAILED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&devices)))) return result;
    ComPtr<IEnumMoniker> enumeration;
    if (devices->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enumeration, 0) != S_OK) return result;
    ComPtr<IMoniker> moniker;
    int index = 0;
    while (enumeration->Next(1, moniker.ReleaseAndGetAddressOf(), nullptr) == S_OK)
    {
        ComPtr<IPropertyBag> bag;
        if (FAILED(moniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&bag)))) continue;
        if (index++ != selected) continue;
        VARIANT path; VariantInit(&path);
        if (SUCCEEDED(bag->Read(L"DevicePath", &path, nullptr)) && path.vt == VT_BSTR)
        {
            const QRegularExpression usb(QStringLiteral("vid_([0-9a-f]{4})&pid_([0-9a-f]{4})"), QRegularExpression::CaseInsensitiveOption);
            auto match = usb.match(QString::fromWCharArray(path.bstrVal));
            if (match.hasMatch()) { result.usb_vid = match.captured(1).toUShort(nullptr, 16); result.usb_pid = match.captured(2).toUShort(nullptr, 16); }
        }
        VariantClear(&path);
        ComPtr<IBaseFilter> filter;
        if (FAILED(moniker->BindToObject(nullptr, nullptr, IID_PPV_ARGS(&filter)))) return result;
        ComPtr<ICaptureGraphBuilder2> builder;
        if (FAILED(CoCreateInstance(CLSID_CaptureGraphBuilder2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&builder)))) return result;
        ComPtr<IAMStreamConfig> config;
        if (FAILED(builder->FindInterface(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, filter.Get(), IID_IAMStreamConfig, reinterpret_cast<void**>(config.GetAddressOf())))) return result;
        int count = 0, size = 0;
        if (FAILED(config->GetNumberOfCapabilities(&count, &size)) || size != sizeof(VIDEO_STREAM_CONFIG_CAPS) || count > 4096) return result;
        for (int i=0; i<count; ++i)
        {
            AM_MEDIA_TYPE* media = nullptr;
            VIDEO_STREAM_CONFIG_CAPS caps{};
            if (FAILED(config->GetStreamCaps(i, &media, reinterpret_cast<BYTE*>(&caps)))) continue;
            int width=0, height=0; REFERENCE_TIME interval=0;
            if (media && media->formattype == FORMAT_VideoInfo && media->cbFormat >= sizeof(VIDEOINFOHEADER))
            {
                const auto* info = reinterpret_cast<const VIDEOINFOHEADER*>(media->pbFormat);
                width=info->bmiHeader.biWidth; height=std::abs(info->bmiHeader.biHeight); interval=info->AvgTimePerFrame;
            }
            else if (media && media->formattype == FORMAT_VideoInfo2 && media->cbFormat >= sizeof(VIDEOINFOHEADER2))
            {
                const auto* info = reinterpret_cast<const VIDEOINFOHEADER2*>(media->pbFormat);
                width=info->bmiHeader.biWidth; height=std::abs(info->bmiHeader.biHeight); interval=info->AvgTimePerFrame;
            }
            // Enumerated format's exact nominal interval; never invent rates inside a min/max range.
            if (width>0 && height>0 && interval>0)
            {
                video::capture_mode mode{width,height,int(std::lround(10000000./interval))};
                const auto same = [&mode](const video::capture_mode& other) { return other.width==mode.width && other.height==mode.height && other.fps==mode.fps; };
                if (std::none_of(result.modes.begin(),result.modes.end(),same)) result.modes.push_back(mode);
            }
            free_media_type(media);
        }
        ComPtr<IAMCameraControl> exposure;
        if (SUCCEEDED(filter.As(&exposure)))
        {
            long lo,hi,step,def,flags;
            if (SUCCEEDED(exposure->GetRange(CameraControl_Exposure,&lo,&hi,&step,&def,&flags)))
            { result.exposure = flags & CameraControl_Flags_Manual; result.automatic_exposure = flags & CameraControl_Flags_Auto; }
        }
        ComPtr<IAMVideoProcAmp> gain;
        if (SUCCEEDED(filter.As(&gain)))
        {
            long lo,hi,step,def,flags;
            result.gain = SUCCEEDED(gain->GetRange(VideoProcAmp_Gain,&lo,&hi,&step,&def,&flags));
        }
        break;
    }
#else
    (void)name;
#endif
    return result;
}
} // namespace opencv_camera_impl
