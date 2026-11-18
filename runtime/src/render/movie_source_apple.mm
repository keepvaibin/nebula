// SPDX-License-Identifier: GPL-3.0-only
// A replacement movie on macOS and iOS (movie_source.h): AVFoundation's asset reader decodes the video track
// (VideoToolbox, in hardware where the system has it) into bi-planar 4:2:0 pixel buffers, NV12's layout, in the
// decoders' own video range. The reader cannot seek: a seek starts a new reader at that time. Built with ARC.

#include "movie_source.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <algorithm>

#if !__has_feature(objc_arc)
#error "movie_source_apple.mm is built with -fobjc-arc"
#endif

namespace galaxy::render {
namespace {

std::string text_of(NSError* error, const char* otherwise) {
    const char* const text = error != nil ? error.localizedDescription.UTF8String : nullptr;
    return text != nullptr ? text : otherwise;
}

class AppleSource final : public MovieSource {
public:
    ~AppleSource() override {
        unlock();
        [reader_ cancelReading];
    }

    bool open(const std::filesystem::path& file) {
        @autoreleasepool {
            NSURL* const url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:file.c_str()]];
            asset_ = [AVURLAsset URLAssetWithURL:url options:@{AVURLAssetPreferPreciseDurationAndTimingKey : @YES}];
            NSArray<AVAssetTrack*>* const tracks = [asset_ tracksWithMediaType:AVMediaTypeVideo];
            if (tracks.count == 0u) {
                error_ = "no video in it";
                return false;
            }
            track_ = tracks.firstObject;
            format_.rate = track_.nominalFrameRate > 0.0f ? track_.nominalFrameRate : 0.0;
            const CGSize size = track_.naturalSize;
            format_.width = size.width >= 2.0 ? static_cast<std::uint32_t>(size.width) : 0u;
            format_.height = size.height >= 2.0 ? static_cast<std::uint32_t>(size.height) : 0u;
            return start(kCMTimeZero);
        }
    }

    const MovieSourceFormat& format() const override { return format_; }

    Read read(MovieSourcePicture& out) override {
        unlock();
        @autoreleasepool {
            const CMSampleBufferRef sample = [output_ copyNextSampleBuffer];
            if (sample == nullptr) {
                if (reader_.status == AVAssetReaderStatusFailed) {
                    error_ = text_of(reader_.error, "decoding stopped");
                    return Read::Failed;
                }
                return Read::End;
            }
            const CVImageBufferRef image = CMSampleBufferGetImageBuffer(sample);
            if (image == nullptr || CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
                CFRelease(sample);
                error_ = "a sample without a picture";
                return Read::Failed;
            }
            locked_ = static_cast<CVPixelBufferRef>(const_cast<void*>(CFRetain(image)));
            const CMTime time = CMSampleBufferGetPresentationTimeStamp(sample);
            CFRelease(sample);
            // The shown area (the clean aperture), else the whole buffer.
            const CGRect clean = CVImageBufferGetCleanRect(locked_);
            const std::size_t buffer_width = CVPixelBufferGetWidthOfPlane(locked_, 0);
            const std::size_t buffer_height = CVPixelBufferGetHeightOfPlane(locked_, 0);
            std::size_t left = 0, top = 0, width = buffer_width, height = buffer_height;
            if (clean.size.width >= 2.0 && clean.size.height >= 2.0) {
                left = std::min(static_cast<std::size_t>(std::max(0.0, static_cast<double>(clean.origin.x))), buffer_width);
                top = std::min(static_cast<std::size_t>(std::max(0.0, static_cast<double>(clean.origin.y))), buffer_height);
                width = std::min(static_cast<std::size_t>(clean.size.width), buffer_width - left);
                height = std::min(static_cast<std::size_t>(clean.size.height), buffer_height - top);
            }
            const auto* const luma = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(locked_, 0));
            const auto* const chroma = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(locked_, 1));
            if (CVPixelBufferGetPlaneCount(locked_) != 2u || luma == nullptr || chroma == nullptr || width < 2u ||
                height < 2u) {
                error_ = "a picture without its two planes";
                return Read::Failed;
            }
            format_.width = static_cast<std::uint32_t>(width);
            format_.height = static_cast<std::uint32_t>(height);
            format_.pixels = MoviePixels::Yuv420;
            // Its matrix as the file tags it, else HD BT.709 / SD BT.601; the range is the buffer format asked for.
            const CFTypeRef matrix = CVBufferCopyAttachment(locked_, kCVImageBufferYCbCrMatrixKey, nullptr);
            format_.bt709 = matrix != nullptr ? CFEqual(matrix, kCVImageBufferYCbCrMatrix_ITU_R_709_2) : height >= 720u;
            if (matrix != nullptr) CFRelease(matrix);
            format_.full_range =
                CVPixelBufferGetPixelFormatType(locked_) == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
            const auto luma_pitch = static_cast<std::int32_t>(CVPixelBufferGetBytesPerRowOfPlane(locked_, 0));
            const auto chroma_pitch = static_cast<std::int32_t>(CVPixelBufferGetBytesPerRowOfPlane(locked_, 1));
            const std::uint8_t* const cb =
                chroma + (top / 2u) * static_cast<std::size_t>(chroma_pitch) + (left & ~std::size_t{1});
            out.time = CMTIME_IS_NUMERIC(time) ? static_cast<std::int64_t>(CMTimeGetSeconds(time) * 1.0e7 + 0.5) : 0;
            out.planes = {luma + top * static_cast<std::size_t>(luma_pitch) + left, cb, cb + 1};
            out.pitches = {luma_pitch, chroma_pitch, chroma_pitch};
            out.chroma_step = 2u;
            return Read::Picture;
        }
    }

    bool seek(std::int64_t time) override {
        unlock();
        @autoreleasepool {
            [reader_ cancelReading];
            return start(CMTimeMake(time, 10'000'000));
        }
    }

private:
    // A reader from `from` to the end, in the decoders' video-range bi-planar form: nothing converts it first.
    bool start(CMTime from) {
        NSError* error = nil;
        reader_ = [AVAssetReader assetReaderWithAsset:asset_ error:&error];
        if (reader_ == nil) {
            error_ = text_of(error, "cannot open");
            return false;
        }
        reader_.timeRange = CMTimeRangeMake(from, kCMTimePositiveInfinity);
        NSDictionary* const settings = @{
            (__bridge NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
        };
        output_ = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track_ outputSettings:settings];
        output_.alwaysCopiesSampleData = NO;
        if (![reader_ canAddOutput:output_]) {
            error_ = "no decoder for its video";
            return false;
        }
        [reader_ addOutput:output_];
        if (![reader_ startReading]) {
            error_ = text_of(reader_.error, "no decoder for its video");
            return false;
        }
        return true;
    }

    void unlock() {
        if (locked_ != nullptr) {
            CVPixelBufferUnlockBaseAddress(locked_, kCVPixelBufferLock_ReadOnly);
            CFRelease(locked_);
            locked_ = nullptr;
        }
    }

    AVURLAsset* asset_ = nil;
    AVAssetTrack* track_ = nil;
    AVAssetReader* reader_ = nil;
    AVAssetReaderTrackOutput* output_ = nil;
    CVPixelBufferRef locked_ = nullptr;  // the last read's picture, locked for reading
    MovieSourceFormat format_;
};

}  // namespace

std::unique_ptr<MovieSource> open_movie_source(const std::filesystem::path& file, std::string& error) {
    auto source = std::make_unique<AppleSource>();
    if (!source->open(file)) {
        error = source->error();
        return nullptr;
    }
    return source;
}

void prepare_movie_sources() {}

}  // namespace galaxy::render
