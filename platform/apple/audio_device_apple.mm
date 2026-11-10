// SPDX-License-Identifier: GPL-3.0-only
// The system's sound output on Apple's systems (audio_device.h): Core Audio's default output unit, which follows the
// default device and converts to it; it pulls float samples on its own thread, as the device needs them.
#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>

#include "apple_host.h"
#include "galaxy/audio_device.h"

#include <TargetConditionals.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace galaxy::host {
namespace {

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
        if (unit_ != nullptr) {
            AudioOutputUnitStop(unit_);
            AudioUnitUninitialize(unit_);
            AudioComponentInstanceDispose(unit_);
        }
    }

    [[nodiscard]] std::uint32_t rate() const override { return rate_; }

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
    std::unique_ptr<AudioDevice> fallback_;
    Render render_;
};

}  // namespace

std::unique_ptr<AudioDevice> open_audio_device() {
    return std::make_unique<AppleAudioDevice>();
}

void set_audio_thread_name() {}

}  // namespace galaxy::host
