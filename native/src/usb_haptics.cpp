#include "usb_haptics.h"

#ifdef _WIN32

#include <initguid.h>
#include <windows.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>

#include <cstdint>
#include <cwchar>
#include <vector>

namespace ds {

namespace {

template <typename T>
void release(T *&p) {
    if (p) p->Release();
    p = nullptr;
}

std::string narrow(const wchar_t *w) {
    if (!w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

bool is_controller(const wchar_t *name) {
    return name && (wcsstr(name, L"Wireless Controller") || wcsstr(name, L"DualSense"));
}

// First active render endpoint whose name belongs to a DualSense.
IMMDevice *find_controller(IMMDeviceEnumerator *en, std::string &name) {
    IMMDeviceCollection *list = nullptr;
    if (FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &list))) return nullptr;
    UINT count = 0;
    list->GetCount(&count);
    IMMDevice *found = nullptr;
    for (UINT i = 0; i < count && !found; i++) {
        IMMDevice *dev = nullptr;
        if (FAILED(list->Item(i, &dev))) continue;
        IPropertyStore *props = nullptr;
        PROPVARIANT pv;
        PropVariantInit(&pv);
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props)) &&
                SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR &&
                is_controller(pv.pwszVal)) {
            name = narrow(pv.pwszVal);
            found = dev;
            dev = nullptr;
        }
        PropVariantClear(&pv);
        release(props);
        release(dev);
    }
    release(list);
    return found;
}

}  // namespace

bool usb_haptics_run(std::atomic<bool> &running,
        const std::function<bool(float *quad, int frames, int rate)> &pull, UsbHapticsInfo &info) {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator *en = nullptr;
    IMMDevice *dev = nullptr;
    IAudioClient *client = nullptr;
    IAudioRenderClient *render = nullptr;
    WAVEFORMATEX *fmt = nullptr;
    bool ok = false;
    do {
        if (FAILED(CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, IID_IMMDeviceEnumerator,
                    reinterpret_cast<void **>(&en)))) {
            info.error = "no MMDeviceEnumerator";
            break;
        }
        dev = find_controller(en, info.device);
        if (!dev) {
            info.error = "controller audio device not found (USB cable only)";
            break;
        }
        if (FAILED(dev->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, reinterpret_cast<void **>(&client))) ||
                FAILED(client->GetMixFormat(&fmt))) {
            info.error = "could not open the controller audio device";
            break;
        }
        info.channels = fmt->nChannels;
        info.sample_rate = int(fmt->nSamplesPerSec);
        info.float_samples = fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                        IsEqualGUID(reinterpret_cast<WAVEFORMATEXTENSIBLE *>(fmt)->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT));
        if (info.channels < 4) {
            info.error = "Windows is using the controller in stereo; channels 3-4 (actuators) do not exist";
            break;
        }
        if (!info.float_samples && fmt->wBitsPerSample != 16) {
            info.error = "unsupported audio format";
            break;
        }
        // 40 ms buffer (in 100 ns units), shared mode: mixed with the game's own audio.
        if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 400000, 0, fmt, nullptr)) ||
                FAILED(client->GetService(IID_IAudioRenderClient, reinterpret_cast<void **>(&render)))) {
            info.error = "failed to start the audio stream";
            break;
        }
        UINT32 buffer_frames = 0;
        client->GetBufferSize(&buffer_frames);
        client->Start();
        ok = true;
        std::vector<float> quad;
        while (running.load()) {
            UINT32 padding = 0;
            client->GetCurrentPadding(&padding);
            const UINT32 avail = buffer_frames - padding;
            // Keep ~10 ms queued: responsive without clicks.
            const UINT32 target = UINT32(info.sample_rate / 100);
            if (padding < target && avail > 0) {
                const UINT32 want = target - padding + UINT32(info.sample_rate / 200);
                const UINT32 n = avail < want ? avail : want;
                quad.assign(size_t(n) * 4, 0.0f);
                const bool keep = pull(quad.data(), int(n), info.sample_rate);
                BYTE *data = nullptr;
                if (SUCCEEDED(render->GetBuffer(n, &data))) {
                    const int ch = info.channels;
                    for (UINT32 i = 0; i < n; i++) {
                        for (int c = 0; c < ch; c++) {
                            // Channels 0-1: speaker/headphones; 2-3: actuators; others: silence.
                            const float v = c < 4 ? quad[4 * size_t(i) + size_t(c)] : 0.0f;
                            if (info.float_samples) {
                                reinterpret_cast<float *>(data)[size_t(i) * ch + c] = v;
                            } else {
                                reinterpret_cast<int16_t *>(data)[size_t(i) * ch + c] = int16_t(v * 32767.0f);
                            }
                        }
                    }
                    render->ReleaseBuffer(n, 0);
                }
                if (!keep) break;
            }
            Sleep(2);
        }
        client->Stop();
    } while (false);
    if (fmt) CoTaskMemFree(fmt);
    release(render);
    release(client);
    release(dev);
    release(en);
    if (SUCCEEDED(co)) CoUninitialize();
    return ok;
}

}  // namespace ds

#else

namespace ds {

bool usb_haptics_run(std::atomic<bool> &, const std::function<bool(float *, int, int)> &, UsbHapticsInfo &info) {
    info.error = "USB HD haptics/speaker are Windows-only for now";
    return false;
}

}  // namespace ds

#endif
