// SPDX-License-Identifier: GPL-3.0-only
// LumaCore's capture services on Apple's systems (luma_capture_backend.h): the microphone through AVAudioEngine's
// input (the system's default input), the camera through an AVCaptureSession on the system's default camera, its
// frames as 32-bit BGRA turned to RGBA. The system asks the player the first time (the app's
// NSMicrophoneUsageDescription and NSCameraUsageDescription); refused or restricted: ABSENT.
#include "luma_capture_backend.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#include <TargetConditionals.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

using galaxy::luma_capture::backend::Item;

// A camera's frames, on the session's queue: each BGRA picture to RGBA, handed to the item.
@interface NebulaCaptureFrames : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
- (instancetype)initWithItem:(std::shared_ptr<Item>)item;
@end

@implementation NebulaCaptureFrames {
    std::shared_ptr<Item> _item;
    std::vector<std::uint8_t> _picture;
}

- (instancetype)initWithItem:(std::shared_ptr<Item>)item {
    if ((self = [super init])) {
        _item = std::move(item);
    }
    return self;
}

- (void)captureOutput:(AVCaptureOutput*)output
    didOutputSampleBuffer:(CMSampleBufferRef)sample
           fromConnection:(AVCaptureConnection*)connection {
    (void)output;
    (void)connection;
    if (_item->stop.load(std::memory_order_acquire)) {
        return;
    }
    CVImageBufferRef image = CMSampleBufferGetImageBuffer(sample);
    if (image == nullptr || CVPixelBufferGetPixelFormatType(image) != kCVPixelFormatType_32BGRA ||
        CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
        return;
    }
    const std::size_t width = CVPixelBufferGetWidth(image);
    const std::size_t height = CVPixelBufferGetHeight(image);
    const std::size_t row = CVPixelBufferGetBytesPerRow(image);
    const auto* const base = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddress(image));
    if (base != nullptr && width > 0u && height > 0u && row >= width * 4u) {
        _picture.resize(width * height * 4u);
        for (std::size_t y = 0; y < height; ++y) {
            const std::uint8_t* from = base + y * row;
            std::uint8_t* to = _picture.data() + y * width * 4u;
            for (std::size_t x = 0; x < width; ++x) {  // BGRA to RGBA, alpha opaque
                to[x * 4u] = from[x * 4u + 2u];
                to[x * 4u + 1u] = from[x * 4u + 1u];
                to[x * 4u + 2u] = from[x * 4u];
                to[x * 4u + 3u] = 255u;
            }
        }
        // When it was taken: its age on the host clock (the session's), taken from the steady clock's now.
        std::uint64_t time = galaxy::luma_capture::backend::steady_ns();
        const CMTime taken = CMSampleBufferGetPresentationTimeStamp(sample);
        const CMTime now = CMClockGetTime(CMClockGetHostTimeClock());
        if (CMTIME_IS_NUMERIC(taken) && CMTIME_IS_NUMERIC(now)) {
            const double age = CMTimeGetSeconds(CMTimeSubtract(now, taken));
            if (age > 0.0 && age < 10.0) {
                time -= static_cast<std::uint64_t>(std::llround(age * 1e9));
            }
        }
        galaxy::luma_capture::backend::hand_picture(*_item, _picture, static_cast<std::uint32_t>(width),
                                                    static_cast<std::uint32_t>(height), time);
    }
    CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
}

@end

namespace galaxy::luma_capture::backend {
namespace {

void end(Item& item, std::int32_t status) {
    const std::lock_guard<std::mutex> lock(g_lock);
    end_locked(&item, status);
}

// Whether the app may use `media`: the system asks the player the first time; false when refused or restricted, or
// when the item is closed before the answer.
bool may_use(const Item& item, AVMediaType media) {
    const AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:media];
    if (status != AVAuthorizationStatusNotDetermined) {
        return status == AVAuthorizationStatusAuthorized;
    }
    // The answer's own reference: the asking thread may have gone on (stop) before the player answered.
    const auto answer = std::make_shared<std::atomic<int>>(-1);
    [AVCaptureDevice requestAccessForMediaType:media
                             completionHandler:^(BOOL granted) {
                               answer->store(granted ? 1 : 0, std::memory_order_release);
                             }];
    while (answer->load(std::memory_order_acquire) < 0) {
        if (item.stop.load(std::memory_order_acquire)) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return answer->load(std::memory_order_acquire) == 1;
}

#if TARGET_OS_IPHONE
// iOS records only in a recording category: the app's session takes PlayAndRecord (the game still plays through the
// speaker, beside other apps' sound) while a microphone is open, and its own category again after the last.
std::mutex g_session_lock;
unsigned g_recording = 0;  // g_session_lock
NSString* g_category = nil;
AVAudioSessionCategoryOptions g_options = 0;

bool begin_recording() {
    const std::lock_guard<std::mutex> lock(g_session_lock);
    AVAudioSession* const session = [AVAudioSession sharedInstance];
    if (g_recording == 0u) {
        g_category = session.category;
        g_options = session.categoryOptions;
        NSError* error = nil;
        if (![session setCategory:AVAudioSessionCategoryPlayAndRecord
                             mode:AVAudioSessionModeDefault
                          options:AVAudioSessionCategoryOptionDefaultToSpeaker |
                                  AVAudioSessionCategoryOptionMixWithOthers |
                                  AVAudioSessionCategoryOptionAllowBluetoothA2DP
                            error:&error] ||
            ![session setActive:YES error:&error]) {
            return false;
        }
    }
    ++g_recording;
    return true;
}

void end_recording() {
    const std::lock_guard<std::mutex> lock(g_session_lock);
    if (g_recording == 0u || --g_recording != 0u) {
        return;
    }
    if (g_category != nil) {
        [[AVAudioSession sharedInstance] setCategory:g_category withOptions:g_options error:nil];
    }
}
#else
bool begin_recording() {
    return true;
}
void end_recording() {}
#endif

// The input's tap at its own format (Core Audio's), its first two channels interleaved for the converter.
void tap(AVAudioInputNode* input, const std::shared_ptr<Item>& item, const std::shared_ptr<Converter>& converter) {
    AVAudioFormat* const format = [input outputFormatForBus:0];
    const auto in_channels = static_cast<std::uint32_t>(std::min<AVAudioChannelCount>(format.channelCount, 2u));
    const auto in_rate = static_cast<std::uint32_t>(std::llround(format.sampleRate));
    auto interleaved = std::make_shared<std::vector<float>>();
    std::shared_ptr<Item> held = item;
    std::shared_ptr<Converter> convert = converter;
    [input installTapOnBus:0
                bufferSize:1024
                    format:format
                     block:^(AVAudioPCMBuffer* buffer, AVAudioTime* when) {
                       (void)when;
                       float* const* const channels = buffer.floatChannelData;
                       const AVAudioFrameCount frames = buffer.frameLength;
                       if (held->stop.load(std::memory_order_acquire) || channels == nullptr || frames == 0u ||
                           in_channels == 0u) {
                           return;
                       }
                       const NSUInteger stride = buffer.stride;
                       const bool split = !buffer.format.interleaved;
                       interleaved->resize(static_cast<std::size_t>(frames) * in_channels);
                       for (AVAudioFrameCount f = 0; f < frames; ++f) {
                           for (std::uint32_t c = 0; c < in_channels; ++c) {
                               (*interleaved)[f * in_channels + c] =
                                   split ? channels[c][f * stride] : channels[0][f * stride + c];
                           }
                       }
                       convert->push(*held, interleaved->data(), frames, in_channels, in_rate);
                     }];
}

}  // namespace

// The default input until stop: ABSENT when there is none or the app may not record, IO when it goes and does not
// come back (an engine stopped by a change of devices starts again on the new default).
void microphone_thread(std::shared_ptr<Item> item) {
    @autoreleasepool {
        if (!may_use(*item, AVMediaTypeAudio) || !begin_recording()) {
            end(*item, LUMA_E_ABSENT);
            return;
        }
        AVAudioEngine* const engine = [[AVAudioEngine alloc] init];
        AVAudioInputNode* const input = engine.inputNode;
        const auto converter = std::make_shared<Converter>();
        const auto usable = [&] {
            AVAudioFormat* const format = [input outputFormatForBus:0];
            return format != nil && format.channelCount > 0u && format.sampleRate > 0.0 &&
                   format.commonFormat == AVAudioPCMFormatFloat32;
        };
        NSError* error = nil;
        bool running = false;
        if (usable()) {
            tap(input, item, converter);
            [engine prepare];
            running = [engine startAndReturnError:&error];
            if (!running) {
                [input removeTapOnBus:0];
            }
        }
        if (!running) {
            end(*item, LUMA_E_ABSENT);
        }
        while (running && !item->stop.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (engine.isRunning) {
                continue;
            }
            // Stopped by a change of devices (AVAudioEngineConfigurationChangeNotification): the tap again at the
            // input's format now.
            [input removeTapOnBus:0];
            if (!usable()) {
                running = false;
                end(*item, LUMA_E_IO);
                break;
            }
            tap(input, item, converter);
            [engine prepare];
            running = [engine startAndReturnError:&error];
            if (!running) {
                [input removeTapOnBus:0];
                end(*item, LUMA_E_IO);
            }
        }
        if (running) {
            [input removeTapOnBus:0];
            [engine stop];
        }
        end_recording();
    }
}

// The system's default camera until stop: ABSENT when there is none or the app may not use it, IO when it goes.
void camera_thread(std::shared_ptr<Item> item) {
    @autoreleasepool {
        if (!may_use(*item, AVMediaTypeVideo)) {
            end(*item, LUMA_E_ABSENT);
            return;
        }
        AVCaptureDevice* const device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
        NSError* error = nil;
        AVCaptureDeviceInput* const input =
            device != nil ? [AVCaptureDeviceInput deviceInputWithDevice:device error:&error] : nil;
        AVCaptureSession* const session = [[AVCaptureSession alloc] init];
        AVCaptureVideoDataOutput* const output = [[AVCaptureVideoDataOutput alloc] init];
        output.videoSettings = @{(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)};
        output.alwaysDiscardsLateVideoFrames = YES;
        dispatch_queue_t const queue = dispatch_queue_create("dev.nebula.capture.camera", DISPATCH_QUEUE_SERIAL);
        NebulaCaptureFrames* const frames = [[NebulaCaptureFrames alloc] initWithItem:item];
        [output setSampleBufferDelegate:frames queue:queue];
        [session beginConfiguration];
        const bool joined = input != nil && [session canAddInput:input] && [session canAddOutput:output];
        if (joined) {
            [session addInput:input];
            [session addOutput:output];
            // The size asked where the camera has it (its own otherwise), at the rate asked where that size has it.
            AVCaptureDeviceFormat* chosen = nil;
            if (item->width != 0u) {
                for (AVCaptureDeviceFormat* format in device.formats) {
                    const CMVideoDimensions size = CMVideoFormatDescriptionGetDimensions(format.formatDescription);
                    if (static_cast<std::uint32_t>(size.width) != item->width ||
                        static_cast<std::uint32_t>(size.height) != item->height) {
                        continue;
                    }
                    bool rated = item->fps == 0u;
                    for (AVFrameRateRange* range in format.videoSupportedFrameRateRanges) {
                        rated = rated || (range.minFrameRate <= item->fps && item->fps <= range.maxFrameRate);
                    }
                    if (chosen == nil || rated) {
                        chosen = format;
                    }
                    if (rated) {
                        break;
                    }
                }
            }
            if ((chosen != nil || item->fps != 0u) && [device lockForConfiguration:&error]) {
                if (chosen != nil) {
                    device.activeFormat = chosen;
                }
                if (item->fps != 0u) {
                    for (AVFrameRateRange* range in device.activeFormat.videoSupportedFrameRateRanges) {
                        if (range.minFrameRate <= item->fps && item->fps <= range.maxFrameRate) {
                            device.activeVideoMinFrameDuration = CMTimeMake(1, static_cast<int32_t>(item->fps));
                            device.activeVideoMaxFrameDuration = CMTimeMake(1, static_cast<int32_t>(item->fps));
                            break;
                        }
                    }
                }
                [device unlockForConfiguration];
            }
        }
        [session commitConfiguration];
        if (joined) {
            [session startRunning];
        }
        if (!joined || !session.isRunning) {
            end(*item, LUMA_E_ABSENT);  // none, or another app holds it
        }
        while (session.isRunning && !item->stop.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (session.isRunning) {
                continue;
            }
#if TARGET_OS_IPHONE
            // Taken for a while (another app, a call): it comes back by itself.
            while (session.isInterrupted && !item->stop.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (session.isRunning || item->stop.load(std::memory_order_acquire)) {
                continue;
            }
#endif
            end(*item, LUMA_E_IO);
        }
        if (session.isRunning) {
            [session stopRunning];
        }
        [output setSampleBufferDelegate:nil queue:nil];
        dispatch_sync(queue, ^{
                      });  // the last frame's call has returned
    }
}

}  // namespace galaxy::luma_capture::backend
