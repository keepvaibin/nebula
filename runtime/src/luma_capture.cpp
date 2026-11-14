// SPDX-License-Identifier: GPL-3.0-only
// LumaCore's capture services (docs/modding/services/audio.md section 7): `audio.microphone` and
// `capture.camera`. Each open starts a thread of its own that opens the device, reads it, converts what it gives
// (the microphone to float at the mod's rate and channels; the camera to RGBA rows top first) and posts the item's
// node; the game thread delivers at a frame's start. No call waits for a device: closing marks the item and lets the
// thread end by itself (it holds the item through a shared_ptr, so nothing it touches is freed under it).
//
// Devices: Windows, WASAPI (shared mode, the default capture endpoint) and Media Foundation (the first video
// capture source, its video processor converting to RGB32); Android, AAudio (the default input) and the NDK's
// Camera2 (the first camera through an image reader, YUV 4:2:0 converted to RGBA). Other platforms
// answer LUMA_E_ABSENT until their backends land (Core Audio and AVFoundation on Apple; PipeWire and V4L2 on Linux).
#include "galaxy/luma_capture_service.h"

#include <luma_capture.h>

#include <algorithm>
#include <atomic>
#include <chrono>
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
#elif defined(__ANDROID__)
// Android: AAudio (the microphone) and the NDK's Camera2 with an image reader (the camera).
#include <aaudio/AAudio.h>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCaptureRequest.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <SDL3/SDL_system.h>  // the system's question to the player for a device (the Android host is the SDL host)
#endif

namespace galaxy::luma_capture {
namespace {

static_assert(sizeof(LumaAudioMicrophoneDesc) == 16u, "LumaAudioMicrophoneDesc 1.0 is 16 bytes");
static_assert(sizeof(LumaCaptureCameraDesc) == 16u, "LumaCaptureCameraDesc 1.0 is 16 bytes");

constexpr std::size_t kBlocks = 8u;  // microphone blocks waiting: 170 ms at 48 kHz

struct Item;
struct Node {
    LumaEventNode event;  // first, and both plain members: the kernel's node is the Node
    Item* item;
};

// One open device. Members marked g_lock are shared with its thread.
struct Item {
    Node node{};
    const LumaMod* mod = nullptr;
    LumaHandle handle = 0;
    LumaEventFn fn = nullptr;
    void* user = nullptr;
    bool camera = false;
    std::atomic<bool> stop{false};  // closed, or its mod gone: the thread ends at its next look
    bool closed = false;            // g_lock: no event from now on
    bool posted = false;            // g_lock
    std::int32_t ended = LUMA_OK;   // g_lock: ENDED to give (its status), not LUMA_OK
    // Microphone (g_lock): blocks of LUMA_AUDIO_MICROPHONE_BLOCK_FRAMES × channels.
    std::uint32_t rate = 48000u;
    std::uint32_t channels = 1u;
    std::vector<float> blocks;
    std::size_t ready_first = 0;
    std::size_t ready_count = 0;
    bool lost = false;
    std::vector<float> handing;  // the game thread's copy
    // Camera (g_lock): the newest picture, swapped in by the thread, out by the delivery.
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t fps = 0;
    std::vector<std::uint8_t> ready;
    bool fresh = false;
    bool skipped = false;
    std::uint32_t ready_width = 0;
    std::uint32_t ready_height = 0;
    std::uint64_t ready_time = 0;
    std::vector<std::uint8_t> shown;  // the game thread's
};

std::mutex g_lock;
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

// With g_lock: queue the item's node (once). Any thread.
void post_locked(Item* item) noexcept {
    if (item->posted || item->closed) {
        return;
    }
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    if (kernel != nullptr && kernel->post(&item->node.event) == LUMA_OK) {
        item->posted = true;
    }
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

// The device's thread, with g_lock: it ended on its own (status ABSENT or IO).
[[maybe_unused]] void end_locked(Item* item, std::int32_t status) noexcept {
    if (!item->closed && item->ended == LUMA_OK) {
        item->ended = status;
        post_locked(item);
    }
}

// ---- The microphone's conversion (its thread) ----------------------------------------------------------------------

// Device frames (float, `in_channels` each) to the mod's rate and channels, into blocks.
struct Converter {
    double place = 0.0;    // between `last` and the next device frame
    float last[2] = {0.0f, 0.0f};
    bool started = false;
    std::vector<float> filling;
    std::size_t filled = 0;

    void push(Item& item, const float* in, std::size_t frames, std::uint32_t in_channels, std::uint32_t in_rate) {
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

    void hand(Item& item) {
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
};

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
                    const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count());
                    const std::lock_guard<std::mutex> lock(g_lock);
                    if (!item->closed) {
                        item->skipped |= item->fresh;
                        item->ready.swap(picture);
                        item->fresh = true;
                        item->ready_width = width;
                        item->ready_height = height;
                        item->ready_time = now;
                        post_locked(item.get());
                    }
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
#elif defined(__ANDROID__)

// ---- Android: AAudio and the NDK's Camera2 --------------------------------------------------------------------------

// The player's answer to the system's question for a device: -1 until it comes.
struct Asked {
    std::atomic<int> answer{-1};
};

void asked_answer(void* userdata, const char*, bool granted) {
    // The callback's own reference: the asking thread may have gone on (stop) before the player answered.
    auto* const held = static_cast<std::shared_ptr<Asked>*>(userdata);
    (*held)->answer.store(granted ? 1 : 0, std::memory_order_release);
    delete held;
}

// Whether the app may use the device `permission` names: the system asks the player the first time (a mod reaches
// here only after the player consented to its capture permission); false when refused, when the
// question cannot be asked, or when the item is closed before the answer.
bool may_use(const Item& item, const char* permission) {
    const auto asked = std::make_shared<Asked>();
    auto* const held = new std::shared_ptr<Asked>(asked);
    if (!SDL_RequestAndroidPermission(permission, asked_answer, held)) {
        delete held;
        return false;
    }
    while (asked->answer.load(std::memory_order_acquire) < 0) {
        if (item.stop.load(std::memory_order_acquire)) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return asked->answer.load(std::memory_order_acquire) == 1;
}

// The default input until stop: ABSENT when there is none or the app may not record (the player refused
// RECORD_AUDIO), IO when it goes (unplugged, taken by a call).
void microphone_thread(std::shared_ptr<Item> item) {
    AAudioStreamBuilder* builder = nullptr;
    AAudioStream* stream = nullptr;
    std::int32_t status = LUMA_E_ABSENT;
    Converter converter;
    do {
        if (!may_use(*item, "android.permission.RECORD_AUDIO")) {
            break;
        }
        if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) {
            break;
        }
        AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
        AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
        AAudioStreamBuilder_setChannelCount(builder, static_cast<std::int32_t>(item->channels));
        AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
        AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_NONE);
        if (AAudioStreamBuilder_openStream(builder, &stream) != AAUDIO_OK || stream == nullptr) {
            break;
        }
        const std::int32_t in_rate = AAudioStream_getSampleRate(stream);
        const std::int32_t in_channels = AAudioStream_getChannelCount(stream);
        if (in_rate <= 0 || in_channels <= 0 || AAudioStream_getFormat(stream) != AAUDIO_FORMAT_PCM_FLOAT ||
            AAudioStream_requestStart(stream) != AAUDIO_OK) {
            break;
        }
        status = LUMA_OK;
        // A fiftieth of a second a read; a read waits at most a tenth, so stop is seen soon.
        const std::int32_t frames_a_read = std::max(in_rate / 50, 64);
        std::vector<float> floats(static_cast<std::size_t>(frames_a_read) * static_cast<std::size_t>(in_channels));
        while (!item->stop.load(std::memory_order_acquire)) {
            const aaudio_result_t got = AAudioStream_read(stream, floats.data(), frames_a_read, 100000000);
            if (got < 0) {
                status = LUMA_E_IO;  // AAUDIO_ERROR_DISCONNECTED and the like
                break;
            }
            if (got > 0) {
                converter.push(*item, floats.data(), static_cast<std::size_t>(got), static_cast<std::uint32_t>(in_channels),
                               static_cast<std::uint32_t>(in_rate));
            }
        }
        (void)AAudioStream_requestStop(stream);
    } while (false);
    if (status != LUMA_OK) {
        const std::lock_guard<std::mutex> lock(g_lock);
        end_locked(item.get(), status);
    }
    if (stream != nullptr) {
        (void)AAudioStream_close(stream);
    }
    if (builder != nullptr) {
        (void)AAudioStreamBuilder_delete(builder);
    }
}

// What the camera's own callbacks tell its thread.
struct CameraState {
    std::atomic<bool> gone{false};
};

void camera_disconnected(void* context, ACameraDevice*) {
    static_cast<CameraState*>(context)->gone.store(true, std::memory_order_release);
}

void camera_error(void* context, ACameraDevice*, int) {
    static_cast<CameraState*>(context)->gone.store(true, std::memory_order_release);
}

void session_quiet(void*, ACameraCaptureSession*) {}

// One byte of colour from YUV (BT.601, full swing as the camera's JPEG-range output).
inline std::uint8_t to_byte(int v) noexcept {
    return static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// The first camera until stop, its YUV 4:2:0 pictures converted to RGBA rows top first: ABSENT when there is none or
// the app may not use it (CAMERA not granted), IO when it goes.
void camera_thread(std::shared_ptr<Item> item) {
    ACameraManager* manager = ACameraManager_create();
    ACameraIdList* ids = nullptr;
    ACameraMetadata* about = nullptr;
    ACameraDevice* device = nullptr;
    AImageReader* reader = nullptr;
    ACaptureRequest* request = nullptr;
    ACameraOutputTarget* target = nullptr;
    ACaptureSessionOutputContainer* outputs = nullptr;
    ACaptureSessionOutput* output = nullptr;
    ACameraCaptureSession* session = nullptr;
    CameraState state;
    std::int32_t status = LUMA_E_ABSENT;
    std::vector<std::uint8_t> picture;
    do {
        if (!may_use(*item, "android.permission.CAMERA")) {
            break;
        }
        if (manager == nullptr || ACameraManager_getCameraIdList(manager, &ids) != ACAMERA_OK || ids == nullptr ||
            ids->numCameras < 1) {
            break;
        }
        const char* const id = ids->cameraIds[0];
        // The size: the one asked when the camera gives it as YUV 4:2:0, else its first such size (its own).
        std::int32_t width = 0;
        std::int32_t height = 0;
        if (ACameraManager_getCameraCharacteristics(manager, id, &about) == ACAMERA_OK && about != nullptr) {
            ACameraMetadata_const_entry sizes{};
            if (ACameraMetadata_getConstEntry(about, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &sizes) == ACAMERA_OK) {
                for (std::uint32_t i = 0; i + 3u < sizes.count; i += 4u) {  // format, width, height, input
                    if (sizes.data.i32[i] != AIMAGE_FORMAT_YUV_420_888 || sizes.data.i32[i + 3u] != 0) {
                        continue;
                    }
                    const std::int32_t w = sizes.data.i32[i + 1u];
                    const std::int32_t h = sizes.data.i32[i + 2u];
                    if (width == 0) {
                        width = w;
                        height = h;
                    }
                    if (static_cast<std::uint32_t>(w) == item->width && static_cast<std::uint32_t>(h) == item->height) {
                        width = w;
                        height = h;
                        break;
                    }
                }
            }
        }
        if (width <= 0 || height <= 0) {
            break;
        }
        ACameraDevice_StateCallbacks device_calls{};
        device_calls.context = &state;
        device_calls.onDisconnected = camera_disconnected;
        device_calls.onError = camera_error;
        if (ACameraManager_openCamera(manager, id, &device_calls, &device) != ACAMERA_OK || device == nullptr) {
            break;  // ACAMERA_ERROR_PERMISSION_DENIED, or in use
        }
        if (AImageReader_new(width, height, AIMAGE_FORMAT_YUV_420_888, 2, &reader) != AMEDIA_OK || reader == nullptr) {
            break;
        }
        ANativeWindow* window = nullptr;
        if (AImageReader_getWindow(reader, &window) != AMEDIA_OK || window == nullptr) {
            break;
        }
        ACameraCaptureSession_stateCallbacks session_calls{};
        session_calls.context = &state;
        session_calls.onClosed = session_quiet;
        session_calls.onReady = session_quiet;
        session_calls.onActive = session_quiet;
        if (ACameraDevice_createCaptureRequest(device, TEMPLATE_PREVIEW, &request) != ACAMERA_OK ||
            ACameraOutputTarget_create(window, &target) != ACAMERA_OK ||
            ACaptureRequest_addTarget(request, target) != ACAMERA_OK ||
            ACaptureSessionOutputContainer_create(&outputs) != ACAMERA_OK ||
            ACaptureSessionOutput_create(window, &output) != ACAMERA_OK ||
            ACaptureSessionOutputContainer_add(outputs, output) != ACAMERA_OK ||
            ACameraDevice_createCaptureSession(device, outputs, &session_calls, &session) != ACAMERA_OK ||
            ACameraCaptureSession_setRepeatingRequest(session, nullptr, 1, &request, nullptr) != ACAMERA_OK) {
            break;
        }
        status = LUMA_OK;
        const auto w = static_cast<std::size_t>(width);
        const auto h = static_cast<std::size_t>(height);
        picture.resize(w * h * 4u);
        while (!item->stop.load(std::memory_order_acquire)) {
            if (state.gone.load(std::memory_order_acquire)) {
                status = LUMA_E_IO;
                break;
            }
            AImage* image = nullptr;
            if (AImageReader_acquireLatestImage(reader, &image) != AMEDIA_OK || image == nullptr) {
                std::this_thread::sleep_for(std::chrono::milliseconds(4));  // none yet
                continue;
            }
            std::uint8_t* y_plane = nullptr;
            std::uint8_t* u_plane = nullptr;
            std::uint8_t* v_plane = nullptr;
            int y_length = 0;
            int u_length = 0;
            int v_length = 0;
            std::int32_t y_row = 0;
            std::int32_t uv_row = 0;
            std::int32_t uv_step = 0;
            const bool planes = AImage_getPlaneData(image, 0, &y_plane, &y_length) == AMEDIA_OK &&
                                AImage_getPlaneData(image, 1, &u_plane, &u_length) == AMEDIA_OK &&
                                AImage_getPlaneData(image, 2, &v_plane, &v_length) == AMEDIA_OK &&
                                AImage_getPlaneRowStride(image, 0, &y_row) == AMEDIA_OK &&
                                AImage_getPlaneRowStride(image, 1, &uv_row) == AMEDIA_OK &&
                                AImage_getPlanePixelStride(image, 1, &uv_step) == AMEDIA_OK;
            // Every read below stays inside the planes' lengths.
            const bool whole = planes && y_row >= width && uv_row > 0 && uv_step > 0 &&
                               static_cast<std::size_t>(y_length) >= static_cast<std::size_t>(y_row) * (h - 1u) + w &&
                               static_cast<std::size_t>(u_length) >=
                                   static_cast<std::size_t>(uv_row) * ((h - 1u) / 2u) + ((w - 1u) / 2u) * static_cast<std::size_t>(uv_step) + 1u &&
                               static_cast<std::size_t>(v_length) >=
                                   static_cast<std::size_t>(uv_row) * ((h - 1u) / 2u) + ((w - 1u) / 2u) * static_cast<std::size_t>(uv_step) + 1u;
            if (whole) {
                for (std::size_t row = 0; row < h; ++row) {
                    const std::uint8_t* ys = y_plane + row * static_cast<std::size_t>(y_row);
                    const std::uint8_t* us = u_plane + (row / 2u) * static_cast<std::size_t>(uv_row);
                    const std::uint8_t* vs = v_plane + (row / 2u) * static_cast<std::size_t>(uv_row);
                    std::uint8_t* to = picture.data() + row * w * 4u;
                    for (std::size_t x = 0; x < w; ++x) {
                        const int luma = ys[x];
                        const int cb = us[(x / 2u) * static_cast<std::size_t>(uv_step)] - 128;
                        const int cr = vs[(x / 2u) * static_cast<std::size_t>(uv_step)] - 128;
                        to[x * 4u] = to_byte(luma + ((cr * 359) >> 8));
                        to[x * 4u + 1u] = to_byte(luma - ((cb * 88 + cr * 183) >> 8));
                        to[x * 4u + 2u] = to_byte(luma + ((cb * 454) >> 8));
                        to[x * 4u + 3u] = 255u;
                    }
                }
                const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                const std::lock_guard<std::mutex> lock(g_lock);
                if (!item->closed) {
                    item->skipped |= item->fresh;
                    item->ready.swap(picture);
                    item->fresh = true;
                    item->ready_width = static_cast<std::uint32_t>(width);
                    item->ready_height = static_cast<std::uint32_t>(height);
                    item->ready_time = now;
                    post_locked(item.get());
                }
                picture.resize(w * h * 4u);
            }
            AImage_delete(image);
        }
        (void)ACameraCaptureSession_stopRepeating(session);
    } while (false);
    if (status != LUMA_OK) {
        const std::lock_guard<std::mutex> lock(g_lock);
        end_locked(item.get(), status);
    }
    if (session != nullptr) {
        ACameraCaptureSession_close(session);
    }
    if (output != nullptr) {
        ACaptureSessionOutput_free(output);
    }
    if (outputs != nullptr) {
        ACaptureSessionOutputContainer_free(outputs);
    }
    if (target != nullptr) {
        ACameraOutputTarget_free(target);
    }
    if (request != nullptr) {
        ACaptureRequest_free(request);
    }
    if (device != nullptr) {
        (void)ACameraDevice_close(device);
    }
    if (reader != nullptr) {
        AImageReader_delete(reader);
    }
    if (about != nullptr) {
        ACameraMetadata_free(about);
    }
    if (ids != nullptr) {
        ACameraManager_deleteCameraIdList(ids);
    }
    if (manager != nullptr) {
        ACameraManager_delete(manager);
    }
}

constexpr bool kHasDevices = true;
#else
constexpr bool kHasDevices = false;
void microphone_thread(std::shared_ptr<Item>) {}
void camera_thread(std::shared_ptr<Item>) {}
#endif

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
