// SPDX-License-Identifier: GPL-3.0-only
// The system's sound output on Apple's systems (audio_device.h): Core Audio's default output unit, which follows the
// default device and converts to it; it pulls float samples on its own thread, as the device needs them.
#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#import <CoreAudio/CoreAudio.h>
#else
#import <AVFoundation/AVFoundation.h>
#endif

#include "apple_host.h"
#include "galaxy/audio_device.h"

#include <TargetConditionals.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

namespace galaxy::host {
namespace {

#if TARGET_OS_OSX
// One number of an output device's (its output scope); 0 when it does not say.
template <typename T>
T device_number(AudioObjectID object, AudioObjectPropertySelector selector, AudioObjectPropertyScope scope) {
    const AudioObjectPropertyAddress address{selector, scope, 0u};
    T value{};
    UInt32 size = sizeof(value);
    return AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, &value) == noErr ? value : T{};
}
#endif

// The output's latency at `rate`, in frames: what the system holds after the render callback before a sample is heard.
// macOS: the unit's own, the device's latency and safety offset, its IO buffer and its first output stream's latency
// (the device's frames are at its own rate). iOS: the session's output latency and IO buffer.
std::uint32_t output_latency_frames(AudioUnit unit, std::uint32_t rate) {
    if (unit == nullptr || rate == 0u) {
        return 0u;
    }
    Float64 unit_seconds = 0.0;
    UInt32 size = sizeof(unit_seconds);
    if (AudioUnitGetProperty(unit, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &unit_seconds, &size) != noErr) {
        unit_seconds = 0.0;
    }
    double seconds = unit_seconds;
#if TARGET_OS_OSX
    AudioDeviceID device = 0;
    size = sizeof(device);
    if (AudioUnitGetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &device, &size) == noErr &&
        device != 0) {
        const AudioObjectPropertyScope out = kAudioObjectPropertyScopeOutput;
        double frames = device_number<UInt32>(device, kAudioDevicePropertyLatency, out) +
                        device_number<UInt32>(device, kAudioDevicePropertySafetyOffset, out) +
                        device_number<UInt32>(device, kAudioDevicePropertyBufferFrameSize, out);
        const AudioObjectPropertyAddress streams{kAudioDevicePropertyStreams, out, 0u};
        UInt32 bytes = 0;
        if (AudioObjectGetPropertyDataSize(device, &streams, 0, nullptr, &bytes) == noErr && bytes >= sizeof(AudioStreamID)) {
            std::vector<AudioStreamID> ids(bytes / sizeof(AudioStreamID));
            if (AudioObjectGetPropertyData(device, &streams, 0, nullptr, &bytes, ids.data()) == noErr) {
                frames += device_number<UInt32>(ids[0], kAudioStreamPropertyLatency, kAudioObjectPropertyScopeGlobal);
            }
        }
        const Float64 device_rate = device_number<Float64>(device, kAudioDevicePropertyNominalSampleRate, out);
        seconds += frames / (device_rate >= 8000.0 ? device_rate : static_cast<double>(rate));
    }
#else
    AVAudioSession* const session = [AVAudioSession sharedInstance];
    seconds += session.outputLatency + session.IOBufferDuration;
#endif
    return static_cast<std::uint32_t>(seconds * rate + 0.5);
}

class AppleAudioDevice final : public AudioDevice {
public:
    AppleAudioDevice() {
        if (!galaxy::apple::audio_answers()) {
            return;  // the system's audio does not answer: start() falls back to no output
        }
        AudioComponentDescription wanted{};
        wanted.componentType = kAudioUnitType_Output;
#if TARGET_OS_OSX
        wanted.componentSubType = kAudioUnitSubType_DefaultOutput;
#else
        wanted.componentSubType = kAudioUnitSubType_RemoteIO;
#endif
        wanted.componentManufacturer = kAudioUnitManufacturer_Apple;
        const AudioComponent component = AudioComponentFindNext(nullptr, &wanted);
        if (component == nullptr || AudioComponentInstanceNew(component, &unit_) != noErr) {
            unit_ = nullptr;
            return;
        }
        // The device's own rate: the unit converts from what the game renders, which is that rate.
        AudioStreamBasicDescription device{};
        UInt32 size = sizeof(device);
        if (AudioUnitGetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &device, &size) == noErr &&
            device.mSampleRate >= 8000.0 && device.mSampleRate <= 192000.0) {
            rate_ = static_cast<std::uint32_t>(device.mSampleRate + 0.5);
        }
    }

    ~AppleAudioDevice() override {
        stop_watching();
        if (unit_ != nullptr) {
            AudioOutputUnitStop(unit_);
            AudioUnitUninitialize(unit_);
            AudioComponentInstanceDispose(unit_);
        }
    }

    [[nodiscard]] std::uint32_t rate() const override { return rate_; }
    // The output's latency (P-071): a movie's picture waits for its sound to be heard, not only rendered.
    [[nodiscard]] std::uint32_t queued_frames() const override { return latency_frames_.load(std::memory_order_relaxed); }

    void start(Render render) override {
        render_ = std::move(render);
        bool started = false;
        if (unit_ != nullptr) {
            AudioStreamBasicDescription format{};
            format.mSampleRate = rate_;
            format.mFormatID = kAudioFormatLinearPCM;
            format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
            format.mChannelsPerFrame = 2;
            format.mBitsPerChannel = 32;
            format.mBytesPerFrame = 8;
            format.mFramesPerPacket = 1;
            format.mBytesPerPacket = 8;
            AURenderCallbackStruct callback{};
            callback.inputProc = &AppleAudioDevice::pull;
            callback.inputProcRefCon = this;
            started = AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof(format)) == noErr &&
                      AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback,
                                           sizeof(callback)) == noErr &&
                      AudioUnitInitialize(unit_) == noErr && AudioOutputUnitStart(unit_) == noErr;
        }
        if (!started) {
            std::fprintf(stderr, "[audio] no output: the game's clock pulls its sound instead\n");
            fallback_ = open_null_audio_device(rate_);
            fallback_->start(render_);
        }
        running_ = started;
        if (started) {
            measure_latency();
            watch_output();
        }
    }

    void set_paused(bool paused) override {
        if (running_ && unit_ != nullptr) {
            (void)(paused ? AudioOutputUnitStop(unit_) : AudioOutputUnitStart(unit_));
        }
        if (fallback_ != nullptr) {
            fallback_->set_paused(paused);
        }
    }

private:
    // Read off the audio thread: at the start and whenever the output changes.
    void measure_latency() {
        latency_frames_.store(output_latency_frames(unit_, rate_), std::memory_order_relaxed);
    }

    // The default output (macOS) or the route (iOS) changing changes the latency.
    void watch_output() {
#if TARGET_OS_OSX
        const AudioObjectPropertyAddress address{kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, 0u};
        listener_ = ^(UInt32, const AudioObjectPropertyAddress*) { measure_latency(); };
        (void)AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &address, dispatch_get_main_queue(), listener_);
#else
        observer_ = [[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionRouteChangeNotification
                                                                      object:nil
                                                                       queue:[NSOperationQueue mainQueue]
                                                                  usingBlock:^(NSNotification*) { measure_latency(); }];
#endif
    }

    void stop_watching() {
#if TARGET_OS_OSX
        if (listener_ != nullptr) {
            const AudioObjectPropertyAddress address{kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, 0u};
            (void)AudioObjectRemovePropertyListenerBlock(kAudioObjectSystemObject, &address, dispatch_get_main_queue(), listener_);
            listener_ = nullptr;
        }
#else
        if (observer_ != nil) {
            [[NSNotificationCenter defaultCenter] removeObserver:observer_];
            observer_ = nil;
        }
#endif
    }

    static OSStatus pull(void* self, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames, AudioBufferList* out) {
        auto* const device = static_cast<AppleAudioDevice*>(self);
        if (out == nullptr || out->mNumberBuffers == 0u || out->mBuffers[0].mData == nullptr) {
            return noErr;
        }
        const UInt32 room = out->mBuffers[0].mDataByteSize / 8u;
        const UInt32 count = frames < room ? frames : room;
        device->render_(static_cast<float*>(out->mBuffers[0].mData), count, device->rate_);
        return noErr;
    }

    AudioUnit unit_ = nullptr;
    std::uint32_t rate_ = 48000u;
    bool running_ = false;
    std::atomic<std::uint32_t> latency_frames_{0u};
#if TARGET_OS_OSX
    AudioObjectPropertyListenerBlock listener_ = nullptr;
#else
    id observer_ = nil;
#endif
    std::unique_ptr<AudioDevice> fallback_;
    Render render_;
};

}  // namespace

std::unique_ptr<AudioDevice> open_audio_device() {
    return std::make_unique<AppleAudioDevice>();
}

void set_audio_thread_name() {}

}  // namespace galaxy::host
