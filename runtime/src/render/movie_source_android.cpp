// SPDX-License-Identifier: GPL-3.0-only
// A replacement movie on Android (movie_source.h): the NDK's media extractor reads the file and MediaCodec (the
// device's decoders, in hardware where it has them) decodes the video onto an image reader's surface in
// YUV_420_888, flexible 4:2:0 that every decoder can give a surface: the decoder writes its own form (a vendor's
// semi-planar layout, Android's YV12) and the image names its planes, strides and crop, so no vendor colour format
// reaches this code. A decoder that refuses the stream gives way to Android's software one. One picture is in flight:
// rendered, then taken from the reader.

#include "movie_source.h"

#include <android/hardware_buffer.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <utility>

namespace galaxy::render {
namespace {

// MediaFormat's COLOR_STANDARD_BT709 and COLOR_RANGE_FULL.
constexpr std::int32_t kStandardBt709 = 1;
constexpr std::int32_t kRangeFull = 1;
// MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible: asked of a decoder that renders onto a CPU-read surface,
// it writes a 4:2:0 form the image's planes can describe, not its own opaque (tiled or compressed) one.
constexpr std::int32_t kYuv420Flexible = 0x7F420888;
constexpr std::int64_t kWaitUs = 10'000;
// A decoder that gives nothing for this many waits in a row (one second) has stopped.
constexpr int kIdleLimit = 100;
// The reader's images: the one held by the last read and room for the codec to render the next.
constexpr std::int32_t kImages = 3;
constexpr std::chrono::milliseconds kImageWait{1000};

class AndroidSource final : public MovieSource {
public:
    ~AndroidSource() override {
        release_image();
        if (codec_ != nullptr) {
            AMediaCodec_stop(codec_);
            AMediaCodec_delete(codec_);
        }
        if (reader_ != nullptr) AImageReader_delete(reader_);
        if (extractor_ != nullptr) AMediaExtractor_delete(extractor_);
        if (fd_ >= 0) ::close(fd_);
    }

    bool open(const std::filesystem::path& file) {
        fd_ = ::open(file.c_str(), O_RDONLY | O_CLOEXEC);
        struct stat info {};
        if (fd_ < 0 || fstat(fd_, &info) != 0) return fail("cannot open");
        extractor_ = AMediaExtractor_new();
        if (extractor_ == nullptr || AMediaExtractor_setDataSourceFd(extractor_, fd_, 0, info.st_size) != AMEDIA_OK) {
            return fail("cannot read its container");
        }
        const std::size_t tracks = AMediaExtractor_getTrackCount(extractor_);
        for (std::size_t track = 0; track < tracks && codec_ == nullptr; ++track) {
            AMediaFormat* const format = AMediaExtractor_getTrackFormat(extractor_, track);
            if (format == nullptr) continue;
            const char* mime = nullptr;
            std::int32_t width = 0;
            std::int32_t height = 0;
            if (AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime) && mime != nullptr &&
                std::strncmp(mime, "video/", 6) == 0 && AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &width) &&
                AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &height) && width >= 2 && height >= 2 &&
                start(format, mime, width, height)) {
                AMediaExtractor_selectTrack(extractor_, track);
                std::int32_t rate = 0;
                float rate_float = 0.0f;
                if (AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, &rate) && rate > 0) {
                    format_.rate = rate;
                } else if (AMediaFormat_getFloat(format, AMEDIAFORMAT_KEY_FRAME_RATE, &rate_float) && rate_float > 0.0f) {
                    format_.rate = rate_float;
                }
                format_.width = static_cast<std::uint32_t>(width);
                format_.height = static_cast<std::uint32_t>(height);
            }
            AMediaFormat_delete(format);
        }
        if (codec_ == nullptr) {
            if (error_.empty()) error_ = "no decoder for its video";
            return false;
        }
        return true;
    }

    const MovieSourceFormat& format() const override { return format_; }

    Read read(MovieSourcePicture& out) override {
        release_image();
        int idle = 0;
        while (!output_done_) {
            feed();
            AMediaCodecBufferInfo info{};
            const ssize_t index = AMediaCodec_dequeueOutputBuffer(codec_, &info, kWaitUs);
            if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                read_colours();
                continue;
            }
            if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER || index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
                if (++idle > kIdleLimit) return failed("the decoder stopped giving pictures");
                continue;
            }
            if (index < 0) return failed("decoding stopped");
            idle = 0;
            output_done_ = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0u;
            // An empty buffer (the end's) is not rendered.
            const bool picture = info.size > 0;
            if (AMediaCodec_releaseOutputBuffer(codec_, static_cast<std::size_t>(index), picture) != AMEDIA_OK) {
                return failed("the decoder did not render its picture");
            }
            if (!picture) continue;
            if (!take_image()) return failed("a decoded picture did not reach the image reader");
            return describe(out) ? Read::Picture : Read::Failed;
        }
        return Read::End;
    }

    bool seek(std::int64_t time) override {
        release_image();
        if (AMediaExtractor_seekTo(extractor_, time / 10, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC) != AMEDIA_OK ||
            AMediaCodec_flush(codec_) != AMEDIA_OK) {
            return false;
        }
        input_done_ = output_done_ = false;
        // Nothing rendered before the flush is waiting in the reader (each picture was taken), but a late one goes.
        AImage* stale = nullptr;
        while (AImageReader_acquireNextImage(reader_, &stale) == AMEDIA_OK) AImage_delete(stale);
        return true;
    }

private:
    bool fail(const char* what) {
        error_ = what;
        return false;
    }
    Read failed(const char* what) {
        error_ = what;
        return Read::Failed;
    }

    // A reader for the track's size and the codec rendering onto it.
    bool start(AMediaFormat* format, const char* mime, std::int32_t width, std::int32_t height) {
        AImageReader* reader = nullptr;
        if (AImageReader_newWithUsage(width, height, AIMAGE_FORMAT_YUV_420_888, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
                                      kImages, &reader) != AMEDIA_OK) {
            error_ = "no image reader for its size";
            return false;
        }
        AImageReader_ImageListener listener{this, [](void* context, AImageReader*) {
                                                auto* const self = static_cast<AndroidSource*>(context);
                                                {
                                                    const std::lock_guard<std::mutex> lock(self->mutex_);
                                                    ++self->available_;
                                                }
                                                self->arrived_.notify_one();
                                            }};
        ANativeWindow* window = nullptr;
        if (AImageReader_setImageListener(reader, &listener) != AMEDIA_OK ||
            AImageReader_getWindow(reader, &window) != AMEDIA_OK) {
            AImageReader_delete(reader);
            error_ = "no image reader surface";
            return false;
        }
        // The device's decoder for the type first; one that refuses this stream (a size or profile it does not take:
        // a hardware decoder's smallest picture, say) gives way to Android's own software decoder of the type.
        AMediaCodec* const chosen = AMediaCodec_createDecoderByType(mime);
        if (configured(chosen, format, window)) {
            reader_ = reader;
            return true;
        }
        if (const char* const software = software_decoder(mime)) {
            if (configured(AMediaCodec_createCodecByName(software), format, window)) {
                reader_ = reader;
                return true;
            }
        }
        AImageReader_delete(reader);
        return false;
    }

    // `codec` configured onto `window` and started: it becomes codec_. Else it is deleted.
    bool configured(AMediaCodec* codec, AMediaFormat* format, ANativeWindow* window) {
        if (codec == nullptr) return false;
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, kYuv420Flexible);
        if (AMediaCodec_configure(codec, format, window, nullptr, 0) == AMEDIA_OK && AMediaCodec_start(codec) == AMEDIA_OK) {
            codec_ = codec;
            return true;
        }
        AMediaCodec_delete(codec);
        return false;
    }

    // Android's own software decoders (Codec2's c2.android components, in every Android 10 and later).
    static const char* software_decoder(const char* mime) {
        static constexpr std::pair<const char*, const char*> kSoftware[] = {
            {"video/avc", "c2.android.avc.decoder"},           {"video/hevc", "c2.android.hevc.decoder"},
            {"video/x-vnd.on2.vp8", "c2.android.vp8.decoder"}, {"video/x-vnd.on2.vp9", "c2.android.vp9.decoder"},
            {"video/av01", "c2.android.av1.decoder"},          {"video/mp4v-es", "c2.android.mpeg4.decoder"},
            {"video/3gpp", "c2.android.h263.decoder"},
        };
        for (const auto& [type, name] : kSoftware) {
            if (std::strcmp(type, mime) == 0) return name;
        }
        return nullptr;
    }

    // The file's next samples into the codec while it has room.
    void feed() {
        while (!input_done_) {
            const ssize_t index = AMediaCodec_dequeueInputBuffer(codec_, 0);
            if (index < 0) return;
            std::size_t capacity = 0;
            std::uint8_t* const buffer = AMediaCodec_getInputBuffer(codec_, static_cast<std::size_t>(index), &capacity);
            const ssize_t size = buffer != nullptr ? AMediaExtractor_readSampleData(extractor_, buffer, capacity) : -1;
            if (size < 0) {
                AMediaCodec_queueInputBuffer(codec_, static_cast<std::size_t>(index), 0, 0, 0,
                                             AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                input_done_ = true;
                return;
            }
            AMediaCodec_queueInputBuffer(codec_, static_cast<std::size_t>(index), 0, static_cast<std::size_t>(size),
                                         static_cast<std::uint64_t>(AMediaExtractor_getSampleTime(extractor_)), 0);
            AMediaExtractor_advance(extractor_);
        }
    }

    // The rendered picture from the reader, waiting for it to arrive.
    bool take_image() {
        const auto until = std::chrono::steady_clock::now() + kImageWait;
        for (;;) {
            if (AImageReader_acquireNextImage(reader_, &image_) == AMEDIA_OK) return true;
            image_ = nullptr;
            std::unique_lock<std::mutex> lock(mutex_);
            if (!arrived_.wait_until(lock, until, [&] { return available_ > 0; })) return false;
            --available_;
        }
    }

    void release_image() {
        if (image_ != nullptr) {
            AImage_delete(image_);
            image_ = nullptr;
        }
    }

    // The codec's colours (its output format), else HD BT.709 / SD BT.601, limited range.
    void read_colours() {
        AMediaFormat* const format = AMediaCodec_getOutputFormat(codec_);
        if (format == nullptr) return;
        standard_ = 0;
        range_ = 0;
        AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_COLOR_STANDARD, &standard_);
        AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_COLOR_RANGE, &range_);
        AMediaFormat_delete(format);
    }

    // The held image's planes at its crop.
    bool describe(MovieSourcePicture& out) {
        std::int32_t kind = 0, width = 0, height = 0, planes = 0;
        std::int64_t timestamp = 0;
        AImageCropRect crop{};
        const media_status_t read = [&] {
            media_status_t status = AImage_getFormat(image_, &kind);
            if (status == AMEDIA_OK) status = AImage_getWidth(image_, &width);
            if (status == AMEDIA_OK) status = AImage_getHeight(image_, &height);
            if (status == AMEDIA_OK) status = AImage_getNumberOfPlanes(image_, &planes);
            if (status == AMEDIA_OK) status = AImage_getTimestamp(image_, &timestamp);
            return status;
        }();
        // YUV_420_888 is flexible: the decoder fills the reader's buffers in its own 4:2:0 form (Android's YV12, a
        // vendor's semi-planar layout) and the image's three planes describe whichever it is; the format is that form's.
        if (read != AMEDIA_OK || planes != 3) {
            error_ = "an image that is not YUV 4:2:0 (format " + std::to_string(kind) + ", " + std::to_string(planes) +
                     " planes, status " + std::to_string(static_cast<int>(read)) + ")";
            return false;
        }
        if (AImage_getCropRect(image_, &crop) != AMEDIA_OK || crop.right <= crop.left || crop.bottom <= crop.top) {
            crop = {0, 0, width, height};
        }
        crop.left = std::max(crop.left, 0);
        crop.top = std::max(crop.top, 0);
        crop.right = std::min(crop.right, width);
        crop.bottom = std::min(crop.bottom, height);
        std::array<std::uint8_t*, 3> data{};
        std::array<int, 3> length{};
        std::array<std::int32_t, 3> row{};
        std::array<std::int32_t, 3> pixel{};
        for (int p = 0; p < 3; ++p) {
            media_status_t status = AImage_getPlaneData(image_, p, &data[p], &length[p]);
            if (status == AMEDIA_OK) status = AImage_getPlaneRowStride(image_, p, &row[p]);
            if (status == AMEDIA_OK) status = AImage_getPlanePixelStride(image_, p, &pixel[p]);
            if (status != AMEDIA_OK || data[p] == nullptr) {
                error_ = "an image without its plane " + std::to_string(p) + " (format " + std::to_string(kind) +
                         ", status " + std::to_string(static_cast<int>(status)) + ")";
                return false;
            }
        }
        const std::int32_t shown_width = crop.right - crop.left;
        const std::int32_t shown_height = crop.bottom - crop.top;
        // Y a byte a sample; Cb and Cr alike, a byte (planar) or two (interleaved) apart.
        if (pixel[0] != 1 || pixel[1] != pixel[2] || (pixel[1] != 1 && pixel[1] != 2) || row[1] != row[2] ||
            shown_width < 2 || shown_height < 2) {
            error_ = "an image layout it cannot read";
            return false;
        }
        const std::int64_t luma_at = static_cast<std::int64_t>(crop.top) * row[0] + crop.left;
        const std::int64_t chroma_at = static_cast<std::int64_t>(crop.top / 2) * row[1] + (crop.left / 2) * pixel[1];
        const std::int64_t luma_end = luma_at + static_cast<std::int64_t>(shown_height - 1) * row[0] + shown_width;
        const std::int64_t chroma_end = chroma_at + static_cast<std::int64_t>((shown_height + 1) / 2 - 1) * row[1] +
                                        static_cast<std::int64_t>((shown_width + 1) / 2 - 1) * pixel[1] + 1;
        if (luma_end > length[0] || chroma_end > length[1] || chroma_end > length[2]) {
            error_ = "an image smaller than its crop";
            return false;
        }
        format_.width = static_cast<std::uint32_t>(shown_width);
        format_.height = static_cast<std::uint32_t>(shown_height);
        format_.pixels = MoviePixels::Yuv420;
        format_.bt709 = standard_ != 0 ? standard_ == kStandardBt709 : shown_height >= 720;
        format_.full_range = range_ == kRangeFull;
        out.time = timestamp / 100;  // the surface's time is the buffer's presentation time, in nanoseconds
        out.planes = {data[0] + luma_at, data[1] + chroma_at, data[2] + chroma_at};
        out.pitches = {row[0], row[1], row[2]};
        out.chroma_step = static_cast<std::uint32_t>(pixel[1]);
        return true;
    }

    int fd_ = -1;
    AMediaExtractor* extractor_ = nullptr;
    AMediaCodec* codec_ = nullptr;
    AImageReader* reader_ = nullptr;
    AImage* image_ = nullptr;  // the last read's picture
    std::mutex mutex_;
    std::condition_variable arrived_;
    int available_ = 0;  // images the reader announced and this source has not waited for (mutex_)
    std::int32_t standard_ = 0;
    std::int32_t range_ = 0;
    MovieSourceFormat format_;
    bool input_done_ = false;
    bool output_done_ = false;
};

}  // namespace

std::unique_ptr<MovieSource> open_movie_source(const std::filesystem::path& file, std::string& error) {
    auto source = std::make_unique<AndroidSource>();
    if (!source->open(file)) {
        error = source->error();
        return nullptr;
    }
    return source;
}

void prepare_movie_sources() {}

}  // namespace galaxy::render
