#define _CRT_SECURE_NO_WARNINGS  // getenv
// Windows' sound output (audio_device.h): WASAPI shared mode, event driven, on one MMCSS thread that also runs the game's
// sound (the render callback). The default endpoint is followed; with none, the thread paces itself and retries.
#include "galaxy/audio_device.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include "galaxy/windows_api.h"
#include "galaxy/windows_power.h"
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

// The buffer when the system offers no low-latency stream.
constexpr REFERENCE_TIME kBufferDuration = 400000;  // 40 ms
// The fill starts at one engine period, the least there is. A wake or a render that took too long of it raises the fill a
// period at a time, up to this many; ten quiet seconds lower it a period.
constexpr std::uint32_t kMostFillPeriods = 4u;
constexpr LONGLONG kQuietSecondsToLower = 10;

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

    [[nodiscard]] std::span<const Speaker> speakers() const noexcept override {
        return std::span<const Speaker>(speakers_.data(), speaker_count_);
    }

    void set_surround(bool on) override {
        if (surround_.exchange(on, std::memory_order_relaxed) != on) {
            SetEvent(changed_);
        }
    }

private:
    // The channels of a channel mask, in the order the device takes them.
    void set_speakers(DWORD mask) {
        speaker_count_ = 0u;
        for (DWORD bit = 1u; bit != 0u && speaker_count_ < speakers_.size(); bit <<= 1u) {
            if ((mask & bit) == 0u) {
                continue;
            }
            Speaker speaker = Speaker::Other;
            switch (bit) {
            case SPEAKER_FRONT_LEFT: speaker = Speaker::Left; break;
            case SPEAKER_FRONT_RIGHT: speaker = Speaker::Right; break;
            case SPEAKER_FRONT_CENTER: speaker = Speaker::Center; break;
            case SPEAKER_SIDE_LEFT: speaker = Speaker::SideLeft; break;
            case SPEAKER_SIDE_RIGHT: speaker = Speaker::SideRight; break;
            case SPEAKER_BACK_LEFT: speaker = Speaker::BackLeft; break;
            case SPEAKER_BACK_RIGHT: speaker = Speaker::BackRight; break;
            default: break;
            }
            speakers_[speaker_count_++] = speaker;
        }
    }

    bool activate(IMMDevice* device) {
        return SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                          reinterpret_cast<void**>(client_.ReleaseAndGetAddressOf())));
    }

    // The shortest period the audio engine offers for this format (IAudioClient3), where the system and driver have it. Only
    // the mix format's own rate and channels qualify; otherwise the plain stream.
    bool initialize_low_latency(IMMDevice* device, WAVEFORMATEX* format) {
        ComPtr<IAudioClient3> client;
        if (FAILED(device->Activate(__uuidof(IAudioClient3), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf())))) {
            return false;
        }
        UINT32 standard = 0u, fundamental = 0u, least = 0u, most = 0u;
        if (FAILED(client->GetSharedModeEnginePeriod(format, &standard, &fundamental, &least, &most)) || least == 0u ||
            FAILED(client->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, least, format, nullptr))) {
            return false;
        }
        client_ = client;
        return true;
    }

    bool initialize(IMMDevice* device, DWORD rate, DWORD mask) {
        const WORD channels = static_cast<WORD>(std::popcount(mask));
        WAVEFORMATEXTENSIBLE format{};
        format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        format.Format.nChannels = channels;
        format.Format.nSamplesPerSec = rate;
        format.Format.wBitsPerSample = 32;
        format.Format.nBlockAlign = static_cast<WORD>(channels * 4u);
        format.Format.nAvgBytesPerSec = rate * channels * 4u;
        format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        format.Samples.wValidBitsPerSample = 32;
        format.dwChannelMask = mask;
        format.SubFormat = GUID{0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};  // IEEE float
        if (initialize_low_latency(device, &format.Format)) {
            set_speakers(mask);
            return true;
        }
        if (!activate(device)) {
            return false;
        }
        const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        if (FAILED(client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, kBufferDuration, 0, &format.Format, nullptr))) {
            return false;
        }
        set_speakers(mask);
        return true;
    }

    bool open_endpoint(IMMDeviceEnumerator* endpoints) {
        constexpr DWORD kStereo = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        constexpr DWORD kRear = SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
        set_speakers(kStereo);
        ComPtr<IMMDevice> device;
        if (FAILED(endpoints->GetDefaultAudioEndpoint(eRender, eConsole, &device)) || !activate(device.Get())) {
            return false;
        }
        // The mix format is asked of a plain client; initialize() chooses the stream.
        WAVEFORMATEX* mix = nullptr;
        if (FAILED(client_->GetMixFormat(&mix)) || mix == nullptr) {
            return false;
        }
        const DWORD rate = mix->nSamplesPerSec;
        DWORD mask = kStereo;
        if (surround_.load(std::memory_order_relaxed) && mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            const DWORD device_mask = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix)->dwChannelMask;
            if ((device_mask & kRear) != 0u && (device_mask & kStereo) == kStereo) {
                mask = device_mask;
            }
        }
        CoTaskMemFree(mix);
        if (rate < 8000u || rate > 192000u) {
            return false;
        }
        if (!initialize(device.Get(), rate, mask)) {
            if (mask == kStereo || !activate(device.Get()) || !initialize(device.Get(), rate, kStereo)) {
                return false;
            }
        }
        REFERENCE_TIME period = 0;
        if (FAILED(client_->GetBufferSize(&buffer_frames_)) || FAILED(client_->GetDevicePeriod(&period, nullptr)) ||
            FAILED(client_->SetEventHandle(samples_)) ||
            FAILED(client_->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(renderer_.ReleaseAndGetAddressOf())))) {
            return false;
        }
        std::uint32_t period_frames = static_cast<std::uint32_t>(period * rate / 10000000);
        ComPtr<IAudioClient3> client3;
        UINT32 engine_format_period = 0u;
        WAVEFORMATEX* current = nullptr;
        if (SUCCEEDED(client_.As(&client3)) && SUCCEEDED(client3->GetCurrentSharedModeEnginePeriod(&current, &engine_format_period)) &&
            engine_format_period != 0u) {
            period_frames = engine_format_period;
        }
        CoTaskMemFree(current);
        period_frames_ = std::max(period_frames, 1u);
        fill_frames_ = std::min(period_frames_, buffer_frames_);
        rate_.store(rate, std::memory_order_relaxed);
        std::cout << "[audio] output " << rate << " Hz, " << speaker_count_ << " channels, buffer " << buffer_frames_
                  << ", fill " << fill_frames_ << ", period " << period_frames << " frames\n";
        return true;
    }

    void raise_fill() {
        const std::uint32_t raised = std::min({fill_frames_ + period_frames_, period_frames_ * kMostFillPeriods, buffer_frames_});
        if (raised != fill_frames_) {
            fill_frames_ = raised;
            std::cout << "[audio] fill raised to " << fill_frames_ << " frames\n";
        }
    }

    void close_endpoint() {
        if (client_ != nullptr) {
            client_->Stop();
        }
        renderer_.Reset();
        client_.Reset();
        set_speakers(SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
    }

    // Until the endpoint fails or the default changes. True when the device gave up (it is reopened); the fill never blocks on
    // the device: only the render callback's own locks can hold this thread.
    void play() {
        std::vector<float> first(static_cast<std::size_t>(fill_frames_) * speaker_count_);
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
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        const LONGLONG period_ticks = static_cast<LONGLONG>(period_frames_) * frequency.QuadPart / rate_.load(std::memory_order_relaxed);
        LARGE_INTEGER last_wake{};
        QueryPerformanceCounter(&last_wake);
        LARGE_INTEGER last_trouble = last_wake;
        for (;;) {
            const DWORD woke = WaitForMultipleObjects(2, waits, FALSE, 500);
            if (woke == WAIT_OBJECT_0 + 1u) {
                return;
            }
            UINT32 padding = 0;
            if (FAILED(client_->GetCurrentPadding(&padding))) {
                return;
            }
            // This machine cannot keep to the fill: a little more of it, not a gap in the sound.
            LARGE_INTEGER wake{};
            QueryPerformanceCounter(&wake);
            if (wake.QuadPart - last_wake.QuadPart > period_ticks * 3 / 2) {
                raise_fill();
                last_trouble = wake;
            } else if (fill_frames_ > period_frames_ && wake.QuadPart - last_trouble.QuadPart > frequency.QuadPart * kQuietSecondsToLower) {
                fill_frames_ -= period_frames_;
                last_trouble = wake;
            }
            last_wake = wake;
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
            LARGE_INTEGER rendered{};
            QueryPerformanceCounter(&rendered);
            if (rendered.QuadPart - wake.QuadPart > period_ticks * 3 / 4) {
                raise_fill();
                last_trouble = rendered;
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
        // The game's sound is mixed and ticked here: it keeps full speed when the process asks for efficiency (EcoQoS,
        // the window behind another or idle), so its clocks do not run late.
        galaxy::host::set_current_thread_efficiency(false);
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
    std::atomic<bool> surround_{false};
    std::atomic<std::uint32_t> rate_{48000u};
    std::array<Speaker, 18> speakers_{Speaker::Left, Speaker::Right};
    std::uint32_t speaker_count_{2u};
    ComPtr<IAudioClient> client_;
    ComPtr<IAudioRenderClient> renderer_;
    UINT32 buffer_frames_{};
    std::uint32_t fill_frames_{};
    std::uint32_t period_frames_{1u};
};

}  // namespace

std::unique_ptr<AudioDevice> open_audio_device() {
    return std::make_unique<WasapiDevice>();
}

void set_audio_thread_name() {
    SetThreadDescription(GetCurrentThread(), L"Nebula Host Audio");
}

}  // namespace galaxy::host
