// SPDX-License-Identifier: GPL-3.0-only
// LumaCore's capture services (docs/modding/reference/services/audio.md): `audio.microphone` and
// `capture.camera`. Each open starts a thread of its own that opens the device, reads it, converts what it gives
// (the microphone to float at the mod's rate and channels; the camera to RGBA rows top first) and posts the item's
// node; the game thread delivers at a frame's start. No call waits for a device: closing marks the item and lets the
// thread end by itself (it holds the item through a shared_ptr, so nothing it touches is freed under it).
//
// Devices: Windows, WASAPI (shared mode, the default capture endpoint) and Media Foundation (the first video
// capture source, its video processor converting to RGB32); Apple's systems, AVAudioEngine and AVCaptureSession
// (luma_capture_apple.mm); Android and Linux, SDL's recording streams and cameras (the default recording device; the
// first camera, converted to RGBA by SDL). The item, its lock and the hand-offs are luma_capture_backend.h's.
#include "galaxy/luma_capture_service.h"
#include "luma_capture_backend.h"

#include <luma_capture.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <audioclient.h>
#include <ks.h>
#include <ksmedia.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmdeviceapi.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#elif defined(__APPLE__)
// AVFoundation: luma_capture_apple.mm.
#elif defined(NEBULA_CAPTURE_SDL)
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_camera.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_timer.h>
#endif

namespace galaxy::luma_capture {
namespace {

static_assert(sizeof(LumaAudioMicrophoneDesc) == 16u, "LumaAudioMicrophoneDesc 1.0 is 16 bytes");
static_assert(sizeof(LumaCaptureCameraDesc) == 16u, "LumaCaptureCameraDesc 1.0 is 16 bytes");

using namespace backend;

std::vector<std::shared_ptr<Item>> g_items;  // g_lock; an item leaves once closed and not queued
const Item* g_delivering = nullptr;          // g_lock
std::atomic<const LumaServiceKernel*> g_kernel{nullptr};
std::atomic<LumaHandle> g_next_handle{1};

void report(const LumaMod* mod, const char* where, std::uint32_t code, const char* a = nullptr) noexcept {
    const char* args[1] = {a};
    if (const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire)) {
        kernel->report(mod, where, code, args, a == nullptr ? 0u : 1u);
    }
}

std::int32_t enter(const LumaMod* mod, const char* where) noexcept {
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    if (kernel == nullptr) {
        return LUMA_E_CANCELLED;
    }
    if (mod == nullptr || kernel->mod_id(mod) == nullptr) {
        kernel->unknown_mod(where);
        return LUMA_E_ARGUMENT;
    }
    if (kernel->mod_live(mod) == 0) {
        return LUMA_E_CANCELLED;
    }
    if (kernel->game_thread() == 0) {
        report(mod, where, 100u);  // Service.Thread
        return LUMA_E_THREAD;
    }
    return LUMA_OK;
}

std::int32_t argument(const LumaMod* mod, const char* where, const char* name) noexcept {
    report(mod, where, 101u, name);  // Service.Argument
    return LUMA_E_ARGUMENT;
}

std::int32_t struct_size(const LumaMod* mod, const char* where, const char* name, std::uint32_t found,
                         std::uint32_t expected) noexcept {
    const std::string a = std::to_string(found);
    const std::string b = std::to_string(expected);
    const char* args[3] = {name, a.c_str(), b.c_str()};
    if (const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire)) {
        kernel->report(mod, where, 103u, args, 3u);  // Service.StructSize
    }
    return LUMA_E_ARGUMENT;
}

// Game thread, with g_lock: lets go of the closed items no queue or delivery holds (their threads may still run on
// their own reference).
void reap_locked() {
    g_items.erase(std::remove_if(g_items.begin(), g_items.end(),
                                 [](const std::shared_ptr<Item>& item) {
                                     return item->closed && !item->posted && item.get() != g_delivering;
                                 }),
                  g_items.end());
}

}  // namespace

// ---- What the backends share (luma_capture_backend.h) --------------------------------------------------------------

namespace backend {

std::mutex g_lock;

void post_locked(Item* item) noexcept {
    if (item->posted || item->closed) {
        return;
    }
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    if (kernel != nullptr && kernel->post(&item->node.event) == LUMA_OK) {
        item->posted = true;
    }
}

void end_locked(Item* item, std::int32_t status) noexcept {
    if (!item->closed && item->ended == LUMA_OK) {
        item->ended = status;
        post_locked(item);
    }
}

std::uint64_t steady_ns() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

void hand_picture(Item& item, std::vector<std::uint8_t>& picture, std::uint32_t width, std::uint32_t height,
                  std::uint64_t time_ns) {
    const std::lock_guard<std::mutex> lock(g_lock);
    if (item.closed) {
        return;
    }
    item.skipped |= item.fresh;
    item.ready.swap(picture);
    item.fresh = true;
    item.ready_width = width;
    item.ready_height = height;
    item.ready_time = time_ns;
    post_locked(&item);
}

// The microphone's conversion (its thread): linear between the device's frames.
void Converter::push(Item& item, const float* in, std::size_t frames, std::uint32_t in_channels, std::uint32_t in_rate) {
    const double step = static_cast<double>(in_rate) / static_cast<double>(item.rate);
    const std::size_t out_channels = item.channels;
    if (filling.size() != LUMA_AUDIO_MICROPHONE_BLOCK_FRAMES * out_channels) {
        filling.assign(LUMA_AUDIO_MICROPHONE_BLOCK_FRAMES * out_channels, 0.0f);
        filled = 0;
    }
    for (std::size_t f = 0; f < frames; ++f) {
        const float* frame = in + f * in_channels;
        const float left = frame[0];
        const float right = in_channels >= 2u ? frame[1] : frame[0];
        if (!started) {
            last[0] = left;
            last[1] = right;
            started = true;
            continue;
        }
        // Outputs between the last device frame and this one.
        while (place < 1.0) {
            const auto t = static_cast<float>(place);
            const float l = last[0] + (left - last[0]) * t;
            const float r = last[1] + (right - last[1]) * t;
            float* to = filling.data() + filled * out_channels;
            if (out_channels == 1u) {
                to[0] = 0.5f * (l + r);
            } else {
                to[0] = l;
                to[1] = r;
            }
            if (++filled == LUMA_AUDIO_MICROPHONE_BLOCK_FRAMES) {
                filled = 0;
                hand(item);
            }
            place += step;
        }
        place -= 1.0;
        last[0] = left;
        last[1] = right;
    }
}

void Converter::hand(Item& item) {
    const std::lock_guard<std::mutex> lock(g_lock);
    if (item.closed) {
        return;
    }
    if (item.ready_count == kBlocks) {
        item.lost = true;
        return;
    }
    const std::size_t block = LUMA_AUDIO_MICROPHONE_BLOCK_FRAMES * item.channels;
    const std::size_t slot = (item.ready_first + item.ready_count) % kBlocks;
    std::copy(filling.begin(), filling.end(), item.blocks.begin() + static_cast<std::ptrdiff_t>(slot * block));
    item.ready_count++;
    post_locked(&item);
}

#if defined(_WIN32)

template <typename T>
void let_go(T*& p) noexcept {
    if (p != nullptr) {
        p->Release();
        p = nullptr;
    }
}

// The default capture endpoint until stop: ABSENT when there is none (or it is refused), IO when it goes.
void microphone_thread(std::shared_ptr<Item> item) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* devices = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    WAVEFORMATEX* format = nullptr;
    HANDLE ready = nullptr;
    std::int32_t status = LUMA_E_ABSENT;
    Converter converter;
    std::vector<float> floats;
    do {
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void**>(&devices))) ||
            FAILED(devices->GetDefaultAudioEndpoint(eCapture, eConsole, &device)) ||
            FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client))) ||
            FAILED(client->GetMixFormat(&format))) {
            break;  // none, or the system's privacy setting refuses it: ABSENT
        }
        bool is_float = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
            is_float = IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != 0;
        }
        const std::uint32_t bits = format->wBitsPerSample;
        const std::uint32_t in_channels = std::max<std::uint32_t>(format->nChannels, 1u);
        const std::uint32_t in_rate = format->nSamplesPerSec;
        if ((is_float && bits != 32u) || (!is_float && bits != 16u && bits != 24u && bits != 32u) || in_rate == 0u) {
            break;
        }
        ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (ready == nullptr ||
            FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 200000, 0, format,
                                      nullptr)) ||
            FAILED(client->SetEventHandle(ready)) ||
            FAILED(client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture))) ||
            FAILED(client->Start())) {
            break;
        }
        status = LUMA_OK;
        const std::uint32_t bytes = bits / 8u;
        while (!item->stop.load(std::memory_order_acquire)) {
            (void)WaitForSingleObject(ready, 100);
            UINT32 packet = 0;
            HRESULT result = S_OK;
            while (SUCCEEDED(result = capture->GetNextPacketSize(&packet)) && packet > 0u) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                if (FAILED(result = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                    break;
                }
                floats.resize(static_cast<std::size_t>(frames) * in_channels);
                for (std::size_t i = 0; i < floats.size(); ++i) {
                    float value = 0.0f;
                    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0u) {
                        const BYTE* at = data + i * bytes;
                        if (is_float) {
                            std::memcpy(&value, at, 4u);
                        } else if (bits == 16u) {
                            std::int16_t v;
                            std::memcpy(&v, at, 2u);
                            value = static_cast<float>(v) / 32768.0f;
                        } else if (bits == 24u) {
                            const std::int32_t v = static_cast<std::int32_t>(static_cast<std::uint32_t>(at[0]) << 8u |
                                                                             static_cast<std::uint32_t>(at[1]) << 16u |
                                                                             static_cast<std::uint32_t>(at[2]) << 24u);
                            value = static_cast<float>(v) / 2147483648.0f;
                        } else {
                            std::int32_t v;
                            std::memcpy(&v, at, 4u);
                            value = static_cast<float>(v) / 2147483648.0f;
                        }
                    }
                    floats[i] = value;
                }
                (void)capture->ReleaseBuffer(frames);
                converter.push(*item, floats.data(), frames, in_channels, in_rate);
            }
            if (FAILED(result)) {
                status = LUMA_E_IO;  // AUDCLNT_E_DEVICE_INVALIDATED and the like: unplugged or taken
                break;
            }
        }
        (void)client->Stop();
    } while (false);
    if (status != LUMA_OK) {
        const std::lock_guard<std::mutex> lock(g_lock);
        end_locked(item.get(), status);
    }
    let_go(capture);
    let_go(client);
    let_go(device);
    let_go(devices);
    if (format != nullptr) {
        CoTaskMemFree(format);
    }
    if (ready != nullptr) {
        CloseHandle(ready);
    }
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
}

// The first video capture source until stop, converted to RGBA rows top first.
void camera_thread(std::shared_ptr<Item> item) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool started = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    IMFAttributes* find = nullptr;
    IMFActivate** sources = nullptr;
    UINT32 source_count = 0;
    IMFMediaSource* source = nullptr;
    IMFAttributes* settings = nullptr;
    IMFSourceReader* reader = nullptr;
    IMFMediaType* wanted = nullptr;
    IMFMediaType* given = nullptr;
    std::int32_t status = LUMA_E_ABSENT;
    std::vector<std::uint8_t> picture;
    do {
        if (!started || FAILED(MFCreateAttributes(&find, 1)) ||
            FAILED(find->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)) ||
            FAILED(MFEnumDeviceSources(find, &sources, &source_count)) || source_count == 0u ||
            FAILED(sources[0]->ActivateObject(IID_PPV_ARGS(&source))) || FAILED(MFCreateAttributes(&settings, 1)) ||
            FAILED(settings->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE)) ||
            FAILED(MFCreateSourceReaderFromMediaSource(source, settings, &reader)) ||
            FAILED(MFCreateMediaType(&wanted)) || FAILED(wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
            FAILED(wanted->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32))) {
            break;
        }
        if (item->width != 0u) {
            (void)MFSetAttributeSize(wanted, MF_MT_FRAME_SIZE, item->width, item->height);
        }
        if (item->fps != 0u) {
            (void)MFSetAttributeRatio(wanted, MF_MT_FRAME_RATE, item->fps, 1u);
        }
        const DWORD stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
        if (FAILED(reader->SetCurrentMediaType(stream, nullptr, wanted))) {
            // The size or rate asked is not one the camera gives: its own, still RGB32.
            (void)wanted->DeleteItem(MF_MT_FRAME_SIZE);
            (void)wanted->DeleteItem(MF_MT_FRAME_RATE);
            if (FAILED(reader->SetCurrentMediaType(stream, nullptr, wanted))) {
                break;
            }
        }
        UINT32 width = 0;
        UINT32 height = 0;
        if (FAILED(reader->GetCurrentMediaType(stream, &given)) ||
            FAILED(MFGetAttributeSize(given, MF_MT_FRAME_SIZE, &width, &height)) || width == 0u || height == 0u) {
            break;
        }
        // RGB32's default stride is negative for rows bottom first.
        const auto stride = static_cast<std::int32_t>(MFGetAttributeUINT32(given, MF_MT_DEFAULT_STRIDE, width * 4u));
        status = LUMA_OK;
        picture.resize(static_cast<std::size_t>(width) * height * 4u);
        while (!item->stop.load(std::memory_order_acquire)) {
            DWORD index = 0;
            DWORD flags = 0;
            LONGLONG when = 0;
            IMFSample* sample = nullptr;
            if (FAILED(reader->ReadSample(stream, 0, &index, &flags, &when, &sample))) {
                status = LUMA_E_IO;
                break;
            }
            if ((flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM)) != 0u) {
                let_go(sample);
                status = LUMA_E_IO;
                break;
            }
            if (sample == nullptr) {
                continue;
            }
            IMFMediaBuffer* buffer = nullptr;
            BYTE* bytes = nullptr;
            DWORD length = 0;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)) &&
                SUCCEEDED(buffer->Lock(&bytes, nullptr, &length))) {
                const std::size_t row = static_cast<std::size_t>(width) * 4u;
                const std::size_t step = static_cast<std::size_t>(stride < 0 ? -stride : stride);
                if (step >= row && length >= step * (height - 1u) + row) {
                    for (UINT32 y = 0; y < height; ++y) {
                        const BYTE* from = bytes + (stride < 0 ? (height - 1u - y) : y) * step;
                        std::uint8_t* to = picture.data() + y * row;
                        for (UINT32 x = 0; x < width; ++x) {  // BGRA to RGBA, alpha opaque
                            to[x * 4u] = from[x * 4u + 2u];
                            to[x * 4u + 1u] = from[x * 4u + 1u];
                            to[x * 4u + 2u] = from[x * 4u];
                            to[x * 4u + 3u] = 255u;
                        }
                    }
                    hand_picture(*item, picture, width, height, steady_ns());
                    picture.resize(static_cast<std::size_t>(width) * height * 4u);
                }
                (void)buffer->Unlock();
            }
            let_go(buffer);
            let_go(sample);
        }
    } while (false);
    if (status != LUMA_OK) {
        const std::lock_guard<std::mutex> lock(g_lock);
        end_locked(item.get(), status);
    }
    let_go(given);
    let_go(wanted);
    let_go(reader);
    let_go(settings);
    if (source != nullptr) {
        (void)source->Shutdown();
        let_go(source);
    }
    for (UINT32 i = 0; i < source_count; ++i) {
        let_go(sources[i]);
    }
    if (sources != nullptr) {
        CoTaskMemFree(sources);
    }
    let_go(find);
    if (started) {
        (void)MFShutdown();
    }
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
}

constexpr bool kHasDevices = true;
#elif defined(__APPLE__)
constexpr bool kHasDevices = true;  // luma_capture_apple.mm
#elif defined(NEBULA_CAPTURE_SDL)

// ---- SDL: Android and Linux ----------------------------------------------------------------------------------
// SDL's recording streams (AAudio or OpenSL ES, PipeWire, PulseAudio or ALSA, Core Audio) and its cameras (Camera2,
// V4L2 or PipeWire, AVFoundation). Each asks the system for the device the first time where the system asks the
// player (Android's RECORD_AUDIO and CAMERA, Apple's microphone and camera privacy); refused: ABSENT.

// SDL's camera part, started once on the main thread (SDL_InitSubSystem's rule) when a camera is first opened: a game
// no mod asks a camera of never starts it. 0 not asked, 1 asked, 2 running, -1 failed.
std::atomic<int> g_cameras{0};

void SDLCALL start_cameras(void*) {
    g_cameras.store(SDL_InitSubSystem(SDL_INIT_CAMERA) ? 2 : -1, std::memory_order_release);
}

// Whether SDL's cameras run: false when they cannot start, or when the item is closed before they do.
bool cameras_running(const Item& item) {
    int expected = 0;
    if (g_cameras.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
        if (!SDL_RunOnMainThread(start_cameras, nullptr, false)) {
            g_cameras.store(-1, std::memory_order_release);
        }
    }
    for (;;) {
        const int state = g_cameras.load(std::memory_order_acquire);
        if (state != 1) {
            return state == 2;
        }
        if (item.stop.load(std::memory_order_acquire)) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// The default recording device until stop, at the mod's rate and channels (SDL converts): ABSENT when there is none
// or the app may not record.
void microphone_thread(std::shared_ptr<Item> item) {
    const SDL_AudioSpec wanted{SDL_AUDIO_F32, static_cast<int>(item->channels), static_cast<int>(item->rate)};
    SDL_AudioStream* const stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &wanted, nullptr,
                                                              nullptr);
    if (stream == nullptr || !SDL_ResumeAudioStreamDevice(stream)) {
        if (stream != nullptr) {
            SDL_DestroyAudioStream(stream);
        }
        const std::lock_guard<std::mutex> lock(g_lock);
        end_locked(item.get(), LUMA_E_ABSENT);
        return;
    }
    Converter converter;
    const std::size_t frame_bytes = sizeof(float) * item->channels;
    std::vector<float> floats(static_cast<std::size_t>(LUMA_AUDIO_MICROPHONE_BLOCK_FRAMES) * item->channels);
    while (!item->stop.load(std::memory_order_acquire)) {
        const int got = SDL_GetAudioStreamData(stream, floats.data(), static_cast<int>(floats.size() * sizeof(float)));
        if (got < 0) {
            const std::lock_guard<std::mutex> lock(g_lock);
            end_locked(item.get(), LUMA_E_IO);
            break;
        }
        if (got < static_cast<int>(frame_bytes)) {
            // A block is a fiftieth of a second at 48 kHz: a look every 5 ms keeps the wait short.
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        converter.push(*item, floats.data(), static_cast<std::size_t>(got) / frame_bytes, item->channels, item->rate);
    }
    SDL_DestroyAudioStream(stream);
}

// The camera's own spec for the item: the size asked where the camera gives it, else its own (SDL lists its best
// first); at that size the rate nearest the one asked. MJPEG is passed over (SDL does not decode it). Null: none.
const SDL_CameraSpec* camera_spec(SDL_CameraSpec** specs, int count, std::uint32_t width, std::uint32_t height,
                                  std::uint32_t fps) {
    const SDL_CameraSpec* chosen = nullptr;
    double chosen_gap = 0.0;
    for (int i = 0; i < count; ++i) {
        const SDL_CameraSpec* spec = specs[i];
        if (spec == nullptr || spec->width <= 0 || spec->height <= 0 || spec->format == SDL_PIXELFORMAT_MJPG) {
            continue;
        }
        if (width != 0u && (static_cast<std::uint32_t>(spec->width) != width ||
                            static_cast<std::uint32_t>(spec->height) != height)) {
            continue;
        }
        if (chosen != nullptr && (spec->width != chosen->width || spec->height != chosen->height)) {
            break;  // the first size stands
        }
        const double rate = spec->framerate_denominator > 0
                                ? static_cast<double>(spec->framerate_numerator) / spec->framerate_denominator
                                : 0.0;
        const double gap = std::abs(rate - static_cast<double>(fps));
        if (chosen == nullptr || (fps != 0u && gap < chosen_gap)) {
            chosen = spec;
            chosen_gap = gap;
        }
    }
    if (chosen == nullptr && width != 0u) {
        return camera_spec(specs, count, 0u, 0u, fps);
    }
    return chosen;
}

// Whether camera `id` is still attached.
bool camera_attached(SDL_CameraID id) {
    int count = 0;
    SDL_CameraID* const ids = SDL_GetCameras(&count);
    bool found = false;
    for (int i = 0; ids != nullptr && i < count; ++i) {
        found = found || ids[i] == id;
    }
    SDL_free(ids);
    return found;
}

// The first camera until stop, converted to RGBA rows top first: ABSENT when there is none or the app may not use
// it, IO when it goes.
void camera_thread(std::shared_ptr<Item> item) {
    SDL_Camera* camera = nullptr;
    SDL_CameraID id = 0;
    std::int32_t status = LUMA_E_ABSENT;
    do {
        if (!cameras_running(*item)) {
            break;
        }
        int count = 0;
        SDL_CameraID* const ids = SDL_GetCameras(&count);
        id = ids != nullptr && count > 0 ? ids[0] : 0;
        SDL_free(ids);
        if (id == 0) {
            break;
        }
        // RGBA at the camera's own size and rate: SDL converts each frame from the camera's format.
        SDL_CameraSpec wanted{};
        wanted.format = SDL_PIXELFORMAT_RGBA32;
        wanted.colorspace = SDL_COLORSPACE_SRGB;
        int spec_count = 0;
        SDL_CameraSpec** const specs = SDL_GetCameraSupportedFormats(id, &spec_count);
        if (const SDL_CameraSpec* own = specs != nullptr ? camera_spec(specs, spec_count, item->width, item->height, item->fps)
                                                         : nullptr) {
            wanted.width = own->width;
            wanted.height = own->height;
            wanted.framerate_numerator = own->framerate_numerator;
            wanted.framerate_denominator = own->framerate_denominator;
        }
        SDL_free(specs);
        camera = SDL_OpenCamera(id, &wanted);
        if (camera == nullptr) {
            break;
        }
        // The system's question to the player (the first time), answered on its own thread.
        SDL_CameraPermissionState allowed = SDL_GetCameraPermissionState(camera);
        while (allowed == SDL_CAMERA_PERMISSION_STATE_PENDING && !item->stop.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            allowed = SDL_GetCameraPermissionState(camera);
        }
        if (allowed != SDL_CAMERA_PERMISSION_STATE_APPROVED) {
            break;
        }
        status = LUMA_OK;
        std::vector<std::uint8_t> picture;
        // SDL's frame times count from SDL's start; the service's are the steady clock's.
        auto last_frame = std::chrono::steady_clock::now();
        while (!item->stop.load(std::memory_order_acquire)) {
            Uint64 taken = 0;
            SDL_Surface* const frame = SDL_AcquireCameraFrame(camera, &taken);
            if (frame == nullptr) {
                // None yet. A camera silent for a second is looked for; gone: IO.
                if (std::chrono::steady_clock::now() - last_frame > std::chrono::seconds(1)) {
                    if (!camera_attached(id)) {
                        status = LUMA_E_IO;
                        break;
                    }
                    last_frame = std::chrono::steady_clock::now();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
                continue;
            }
            last_frame = std::chrono::steady_clock::now();
            const auto w = static_cast<std::size_t>(frame->w);
            const auto h = static_cast<std::size_t>(frame->h);
            picture.resize(w * h * 4u);
            const bool converted =
                w > 0u && h > 0u &&
                SDL_ConvertPixelsAndColorspace(frame->w, frame->h, frame->format, SDL_GetSurfaceColorspace(frame),
                                               SDL_GetSurfaceProperties(frame), frame->pixels, frame->pitch,
                                               SDL_PIXELFORMAT_RGBA32, SDL_COLORSPACE_SRGB, 0, picture.data(),
                                               frame->w * 4);
            const std::uint64_t now_sdl = SDL_GetTicksNS();
            const std::uint64_t now = steady_ns();
            SDL_ReleaseCameraFrame(camera, frame);
            if (!converted) {
                continue;
            }
            hand_picture(*item, picture, static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h),
                         taken != 0u && taken <= now_sdl ? now - (now_sdl - taken) : now);
        }
    } while (false);
    if (status != LUMA_OK) {
        const std::lock_guard<std::mutex> lock(g_lock);
        end_locked(item.get(), status);
    }
    if (camera != nullptr) {
        SDL_CloseCamera(camera);
    }
}

constexpr bool kHasDevices = true;
#else
constexpr bool kHasDevices = false;
void microphone_thread(std::shared_ptr<Item>) {}
void camera_thread(std::shared_ptr<Item>) {}
#endif

}  // namespace backend

namespace {

// ---- Deliveries (game thread) --------------------------------------------------------------------------------------

void deliver(LumaEventNode* node) {
    Item* const item = reinterpret_cast<Node*>(node)->item;
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    const char* const service = item->camera ? LUMA_CAPTURE_CAMERA_NAME : LUMA_AUDIO_MICROPHONE_NAME;
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        item->posted = false;
        if (item->closed) {
            reap_locked();
            return;
        }
        g_delivering = item;
    }
    std::int32_t ended = LUMA_OK;
    for (;;) {
        LumaEvent event{};
        event.size = sizeof(event);
        event.handle = item->handle;
        LumaCaptureCameraFrame frame{};
        {
            const std::lock_guard<std::mutex> lock(g_lock);
            if (item->closed) {
                break;
            }
            if (item->camera && item->fresh) {
                item->fresh = false;
                item->shown.swap(item->ready);
                frame.size = sizeof(frame);
                frame.width = item->ready_width;
                frame.height = item->ready_height;
                frame.stride = item->ready_width * 4u;
                frame.time_ns = item->ready_time;
                frame.pixels = item->shown.data();
                event.kind = LUMA_CAPTURE_CAMERA_EVENT_FRAME;
                event.status = std::exchange(item->skipped, false) ? LUMA_E_BUSY : LUMA_OK;
                event.data = &frame;
                event.data_size = sizeof(frame);
            } else if (!item->camera && item->ready_count > 0u) {
                const std::size_t block = LUMA_AUDIO_MICROPHONE_BLOCK_FRAMES * item->channels;
                const float* from = item->blocks.data() + item->ready_first * block;
                std::copy(from, from + block, item->handing.begin());
                item->ready_first = (item->ready_first + 1u) % kBlocks;
                item->ready_count--;
                event.kind = LUMA_AUDIO_MICROPHONE_EVENT_SAMPLES;
                event.status = std::exchange(item->lost, false) ? LUMA_E_BUSY : LUMA_OK;
                event.data = item->handing.data();
                event.data_size = block * sizeof(float);
                event.value = LUMA_AUDIO_MICROPHONE_BLOCK_FRAMES;
                event.flags = item->rate;
            } else if (item->ended != LUMA_OK) {
                ended = item->ended;
                item->closed = true;  // its last event
                event.kind = item->camera ? LUMA_CAPTURE_CAMERA_EVENT_ENDED : LUMA_AUDIO_MICROPHONE_EVENT_ENDED;
                event.status = ended;
            } else {
                break;
            }
        }
        if (kernel == nullptr || kernel->call(item->mod, service, item->fn, item->user, &event) != LUMA_OK ||
            ended != LUMA_OK) {
            break;
        }
    }
    const std::lock_guard<std::mutex> lock(g_lock);
    g_delivering = nullptr;
    reap_locked();
}

void drop(LumaEventNode* node) {
    Item* const item = reinterpret_cast<Node*>(node)->item;
    const std::lock_guard<std::mutex> lock(g_lock);
    item->posted = false;
    reap_locked();
}

// ---- The calls (game thread) ---------------------------------------------------------------------------------------

std::int32_t open_item(const LumaMod* mod, const char* where, bool camera, LumaEventFn fn, void* user,
                       LumaHandle* out, const std::shared_ptr<Item>& item) {
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        for (const std::shared_ptr<Item>& other : g_items) {
            if (other->mod == mod && other->camera == camera && !other->closed) {
                report(mod, where, 104u, "1");  // Service.Limit
                return LUMA_E_LIMIT;
            }
        }
    }
    if (!kHasDevices) {
        return LUMA_E_ABSENT;
    }
    item->node.event.mod = mod;
    item->node.event.deliver = deliver;
    item->node.event.drop = drop;
    item->node.item = item.get();
    item->mod = mod;
    item->fn = fn;
    item->user = user;
    item->camera = camera;
    try {
        const std::lock_guard<std::mutex> lock(g_lock);
        g_items.reserve(g_items.size() + 1u);
        std::thread(camera ? camera_thread : microphone_thread, item).detach();
        item->handle = g_next_handle.fetch_add(1, std::memory_order_relaxed);
        *out = item->handle;
        g_items.push_back(item);
    } catch (...) {
        item->stop.store(true, std::memory_order_release);
        return LUMA_E_MEMORY;
    }
    return LUMA_OK;
}

std::int32_t close_item(const LumaMod* mod, const char* where, bool camera, LumaHandle handle) {
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    const std::lock_guard<std::mutex> lock(g_lock);
    for (const std::shared_ptr<Item>& item : g_items) {
        if (item->mod == mod && item->handle == handle && item->camera == camera && !item->closed) {
            item->closed = true;
            item->stop.store(true, std::memory_order_release);
            reap_locked();
            return LUMA_OK;
        }
    }
    report(mod, where, 102u);  // Service.Handle
    return LUMA_E_HANDLE;
}

std::int32_t microphone_open(const LumaMod* mod, const LumaAudioMicrophoneDesc* desc, LumaEventFn fn, void* user,
                             LumaHandle* out) noexcept {
    static constexpr const char* where = "audio.microphone.open";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (desc == nullptr) {
        return argument(mod, where, "desc");
    }
    if (desc->size < sizeof(LumaAudioMicrophoneDesc)) {
        return struct_size(mod, where, "LumaAudioMicrophoneDesc", desc->size, sizeof(LumaAudioMicrophoneDesc));
    }
    const std::uint32_t rate = desc->rate == 0u ? 48000u : desc->rate;
    if (rate < 8000u || rate > 192000u) {
        return argument(mod, where, "desc.rate");
    }
    if (desc->channels != 1u && desc->channels != 2u) {
        return argument(mod, where, "desc.channels");
    }
    if (fn == nullptr) {
        return argument(mod, where, "fn");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    try {
        auto item = std::make_shared<Item>();
        item->rate = rate;
        item->channels = desc->channels;
        item->blocks.assign(kBlocks * LUMA_AUDIO_MICROPHONE_BLOCK_FRAMES * desc->channels, 0.0f);
        item->handing.assign(LUMA_AUDIO_MICROPHONE_BLOCK_FRAMES * desc->channels, 0.0f);
        return open_item(mod, where, false, fn, user, out, item);
    } catch (...) {
        return LUMA_E_MEMORY;
    }
}

std::int32_t microphone_close(const LumaMod* mod, LumaHandle microphone) noexcept {
    try {
        return close_item(mod, "audio.microphone.close", false, microphone);
    } catch (...) {
        return LUMA_E_MEMORY;
    }
}

std::int32_t camera_open(const LumaMod* mod, const LumaCaptureCameraDesc* desc, LumaEventFn fn, void* user,
                         LumaHandle* out) noexcept {
    static constexpr const char* where = "capture.camera.open";
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    if (desc == nullptr) {
        return argument(mod, where, "desc");
    }
    if (desc->size < sizeof(LumaCaptureCameraDesc)) {
        return struct_size(mod, where, "LumaCaptureCameraDesc", desc->size, sizeof(LumaCaptureCameraDesc));
    }
    if (desc->width > LUMA_CAPTURE_CAMERA_WIDTH_MOST || desc->height > LUMA_CAPTURE_CAMERA_HEIGHT_MOST ||
        (desc->width == 0u) != (desc->height == 0u)) {
        return argument(mod, where, "desc.width");
    }
    if (desc->fps > LUMA_CAPTURE_CAMERA_FPS_MOST) {
        return argument(mod, where, "desc.fps");
    }
    if (fn == nullptr) {
        return argument(mod, where, "fn");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    try {
        auto item = std::make_shared<Item>();
        item->width = desc->width;
        item->height = desc->height;
        item->fps = desc->fps;
        return open_item(mod, where, true, fn, user, out, item);
    } catch (...) {
        return LUMA_E_MEMORY;
    }
}

std::int32_t camera_close(const LumaMod* mod, LumaHandle camera) noexcept {
    try {
        return close_item(mod, "capture.camera.close", true, camera);
    } catch (...) {
        return LUMA_E_MEMORY;
    }
}

// ---- The providers -------------------------------------------------------------------------------------------------

// After the mod's stop: its devices close (their threads end by themselves); nothing is posted for it from now on.
void mod_gone(void*, const LumaMod* mod) {
    const std::lock_guard<std::mutex> lock(g_lock);
    for (const std::shared_ptr<Item>& item : g_items) {
        if (item->mod == mod) {
            item->closed = true;
            item->stop.store(true, std::memory_order_release);
        }
    }
    reap_locked();
}

void mod_end(void*, const LumaMod*) {
    const std::lock_guard<std::mutex> lock(g_lock);
    reap_locked();
}

void registered(void*, const LumaServiceKernel* kernel) {
    g_kernel.store(kernel, std::memory_order_release);
}

const LumaAudioMicrophoneService kMicrophone = {
    {sizeof(LumaAudioMicrophoneService), LUMA_AUDIO_MICROPHONE_MAJOR, LUMA_AUDIO_MICROPHONE_MINOR},
    microphone_open,
    microphone_close,
};
const LumaCaptureCameraService kCamera = {
    {sizeof(LumaCaptureCameraService), LUMA_CAPTURE_CAMERA_MAJOR, LUMA_CAPTURE_CAMERA_MINOR},
    camera_open,
    camera_close,
};

// Both report only the registry's rows (100-104); the permission is checked when the mod binds.
const LumaServiceProvider kMicrophoneProvider = {
    sizeof(LumaServiceProvider), LUMA_AUDIO_MICROPHONE_NAME, &kMicrophone.base, nullptr, nullptr, 0u, nullptr,
    nullptr, mod_gone, mod_end, registered,
};
const LumaServiceProvider kCameraProvider = {
    sizeof(LumaServiceProvider), LUMA_CAPTURE_CAMERA_NAME, &kCamera.base, nullptr, nullptr, 0u, nullptr,
    nullptr, mod_gone, mod_end, registered,
};
const LumaServiceProvider* const kProviders[] = {&kMicrophoneProvider, &kCameraProvider};

}  // namespace

std::span<const LumaServiceProvider* const> providers(void (*)(const char*)) noexcept {
    return kProviders;
}

}  // namespace galaxy::luma_capture
