#define _CRT_SECURE_NO_WARNINGS  // getenv
// Windows' sound output (audio_device.h): WASAPI shared mode, event driven, on one MMCSS thread that also runs the game's
// sound (the render callback). The default endpoint is followed; with none, the thread paces itself and retries.
#include "galaxy/audio_device.h"
#include "galaxy/run_options.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace galaxy::host {
namespace {

using Microsoft::WRL::ComPtr;

// The fill the thread keeps in the device's buffer, as multiples of the device's period (10 ms in shared mode); the game's own
// frames (17.5 ms) add to it only while one is half used.
constexpr std::uint32_t kFillPeriods = 2u;
constexpr REFERENCE_TIME kBufferDuration = 400000;  // 40 ms

void set_mmcss() {
    using SetFn = HANDLE(WINAPI*)(LPCWSTR, LPDWORD);
    using PriorityFn = BOOL(WINAPI*)(HANDLE, int);
    HMODULE avrt = LoadLibraryW(L"avrt.dll");
    if (avrt == nullptr) {
        return;
    }
    const auto set = reinterpret_cast<SetFn>(GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW"));
    const auto priority = reinterpret_cast<PriorityFn>(GetProcAddress(avrt, "AvSetMmThreadPriority"));
    if (set == nullptr) {
        return;
    }
    DWORD index = 0;
    HANDLE task = set(L"Pro Audio", &index);
    if (task != nullptr && priority != nullptr) {
        constexpr int kAvrtPriorityCritical = 2;
        priority(task, kAvrtPriorityCritical);
    }
}

// Default endpoint changes and lost devices set the event.
class EndpointWatch final : public IMMNotificationClient {
public:
    explicit EndpointWatch(HANDLE changed) : changed_(changed) {}

    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --references_;
        if (left == 0u) {
            delete this;
        }
        return left;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (id == __uuidof(IUnknown) || id == __uuidof(IMMNotificationClient)) {
            *out = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eConsole) {
            SetEvent(changed_);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override {
        SetEvent(changed_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    std::atomic<ULONG> references_{1};
    HANDLE changed_;
};

class WasapiDevice final : public AudioDevice {
public:
    WasapiDevice() {
        ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        changed_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        samples_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        std::thread([this] { thread_main(); }).detach();
        WaitForSingleObject(ready_, INFINITE);
    }

    [[nodiscard]] std::uint32_t rate() const override { return rate_.load(std::memory_order_relaxed); }

    [[nodiscard]] std::uint32_t queued_frames() const override { return queued_.load(std::memory_order_relaxed); }

    void start(Render render) override {
        {
            std::lock_guard<std::mutex> lock(start_lock_);
            render_ = std::move(render);
        }
        start_wake_.notify_one();
    }

    void set_paused(bool paused) override { paused_.store(paused, std::memory_order_relaxed); }

private:
    bool open_endpoint(IMMDeviceEnumerator* endpoints) {
        ComPtr<IMMDevice> device;
        if (FAILED(endpoints->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
            return false;
        }
        if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client_.ReleaseAndGetAddressOf())))) {
            return false;
        }
        WAVEFORMATEX* mix = nullptr;
        if (FAILED(client_->GetMixFormat(&mix)) || mix == nullptr) {
            return false;
        }
        const DWORD rate = mix->nSamplesPerSec;
        CoTaskMemFree(mix);
        if (rate < 8000u || rate > 192000u) {
            return false;
        }
        WAVEFORMATEXTENSIBLE format{};
        format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        format.Format.nChannels = 2;
        format.Format.nSamplesPerSec = rate;
        format.Format.wBitsPerSample = 32;
        format.Format.nBlockAlign = 8;
        format.Format.nAvgBytesPerSec = rate * 8u;
        format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        format.Samples.wValidBitsPerSample = 32;
        format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        format.SubFormat = GUID{0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};  // IEEE float
        const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        if (FAILED(client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, kBufferDuration, 0, &format.Format, nullptr))) {
            return false;
        }
        REFERENCE_TIME period = 0;
        if (FAILED(client_->GetBufferSize(&buffer_frames_)) || FAILED(client_->GetDevicePeriod(&period, nullptr)) ||
            FAILED(client_->SetEventHandle(samples_)) ||
            FAILED(client_->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(renderer_.ReleaseAndGetAddressOf())))) {
            return false;
        }
        const std::uint32_t period_frames = static_cast<std::uint32_t>(period * rate / 10000000);
        fill_frames_ = std::min<std::uint32_t>(period_frames * kFillPeriods, buffer_frames_);
        rate_.store(rate, std::memory_order_relaxed);
        std::cout << "[audio] output " << rate << " Hz, buffer " << buffer_frames_ << ", fill " << fill_frames_ << ", period "
                  << period_frames << " frames\n";
        return true;
    }

    void close_endpoint() {
        if (client_ != nullptr) {
            client_->Stop();
        }
        renderer_.Reset();
        client_.Reset();
    }

    // Until the endpoint fails or the default changes. True when the device gave up (it is reopened); the fill never blocks on
    // the device: only the render callback's own locks can hold this thread.
    void play() {
        std::vector<float> first(static_cast<std::size_t>(fill_frames_) * 2u);
        render_(first.data(), fill_frames_, rate_.load(std::memory_order_relaxed));
        BYTE* data = nullptr;
        if (FAILED(renderer_->GetBuffer(fill_frames_, &data))) {
            return;
        }
        std::memcpy(data, first.data(), first.size() * sizeof(float));
        if (FAILED(renderer_->ReleaseBuffer(fill_frames_, 0)) || FAILED(client_->Start())) {
            return;
        }
        const HANDLE waits[2] = {samples_, changed_};
        for (;;) {
            const DWORD woke = WaitForMultipleObjects(2, waits, FALSE, 500);
            if (woke == WAIT_OBJECT_0 + 1u) {
                return;
            }
            UINT32 padding = 0;
            if (FAILED(client_->GetCurrentPadding(&padding))) {
                return;
            }
            if (padding >= fill_frames_) {
                continue;
            }
            queued_.store(padding, std::memory_order_relaxed);
            const UINT32 frames = fill_frames_ - padding;
            if (FAILED(renderer_->GetBuffer(frames, &data))) {
                return;
            }
            // Paused: the device plays silence, and the mixer, the game's sound and a movie's place stand still.
            if (paused_.load(std::memory_order_relaxed)) {
                if (FAILED(renderer_->ReleaseBuffer(frames, AUDCLNT_BUFFERFLAGS_SILENT))) {
                    return;
                }
                continue;
            }
            render_(reinterpret_cast<float*>(data), frames, rate_.load(std::memory_order_relaxed));
            if (FAILED(renderer_->ReleaseBuffer(frames, 0))) {
                return;
            }
        }
    }

    // No device: the clock pulls, and each second the default endpoint is tried again.
    void wait_for_endpoint() {
        const std::uint32_t rate = rate_.load(std::memory_order_relaxed);
        const std::uint32_t frames = rate / 100u;
        std::vector<float> out(static_cast<std::size_t>(frames) * 2u);
        for (int i = 0; i < 100; i++) {
            render_(out.data(), frames, rate);
            if (WaitForSingleObject(changed_, 10) == WAIT_OBJECT_0) {
                return;
            }
        }
    }

    void thread_main() {
        set_audio_thread_name();
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        set_mmcss();
        ComPtr<IMMDeviceEnumerator> endpoints;
        CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&endpoints));
        if (endpoints != nullptr) {
            endpoints->RegisterEndpointNotificationCallback(new EndpointWatch(changed_));
        }
        bool open = endpoints != nullptr && open_endpoint(endpoints.Get());
        SetEvent(ready_);
        {
            std::unique_lock<std::mutex> lock(start_lock_);
            start_wake_.wait(lock, [this] { return render_ != nullptr; });
        }
        for (;;) {
            if (open) {
                play();
                close_endpoint();
            } else {
                wait_for_endpoint();
            }
            open = endpoints != nullptr && open_endpoint(endpoints.Get());
            if (!open) {
                close_endpoint();
            }
        }
    }

    HANDLE ready_{};
    HANDLE changed_{};
    HANDLE samples_{};
    std::mutex start_lock_;
    std::condition_variable start_wake_;
    Render render_;
    std::atomic<std::uint32_t> queued_{0u};
    std::atomic<bool> paused_{false};
    std::atomic<std::uint32_t> rate_{48000u};
    ComPtr<IAudioClient> client_;
    ComPtr<IAudioRenderClient> renderer_;
    UINT32 buffer_frames_{};
    std::uint32_t fill_frames_{};
};

}  // namespace

std::unique_ptr<AudioDevice> open_audio_device() {
    return std::make_unique<WasapiDevice>();
}

void set_audio_thread_name() {
    SetThreadDescription(GetCurrentThread(), L"Nebula Host Audio");
}

}  // namespace galaxy::host
