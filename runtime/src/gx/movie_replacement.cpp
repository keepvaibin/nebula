// SPDX-License-Identifier: GPL-3.0-only
// HD movie replacements (movie_replacement.h): Media Foundation's source reader on Windows decodes the file (its
// colour conversion to RGB follows the video's own matrix and range); the planes are JPEG's YCbCr of that picture.

#include "galaxy/gx/movie_replacement.h"

#include "galaxy/crash_report.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>
#include <wrl/client.h>
#endif

namespace galaxy::gx {

namespace {

constexpr std::size_t kAhead = 4;         // decoded frames kept ahead of the game
constexpr std::size_t kPoolLimit = 12;    // upload buffers a movie may hold (ahead, shown, in flight)
constexpr const char* kExtensions[] = {".mp4", ".m4v", ".mov", ".mkv", ".webm", ".avi", ".wmv"};
std::atomic<bool> s_showing{false};

std::string ascii_lower(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

}  // namespace

std::uint64_t movie_plane_layout(std::uint32_t width, std::uint32_t height, std::array<std::uint64_t, 3>& offsets,
                                 std::array<std::uint32_t, 3>& pitches) noexcept {
    std::uint64_t size = 0;
    for (unsigned p = 0; p < 3u; ++p) {
        const std::uint32_t w = p == 0u ? width : (width + 1u) / 2u;
        const std::uint32_t h = p == 0u ? height : (height + 1u) / 2u;
        size = (size + TextureBackend::kPlacementAlignment - 1u) &
               ~static_cast<std::uint64_t>(TextureBackend::kPlacementAlignment - 1u);
        offsets[p] = size;
        pitches[p] = (w * 4u + TextureBackend::kPitchAlignment - 1u) & ~(TextureBackend::kPitchAlignment - 1u);
        size += static_cast<std::uint64_t>(pitches[p]) * h;
    }
    return size;
}

void convert_bgra_to_movie_planes(const std::uint8_t* bgra, std::uint32_t width, std::uint32_t height,
                                  std::int32_t stride, std::uint8_t* out, const std::array<std::uint64_t, 3>& offsets,
                                  const std::array<std::uint32_t, 3>& pitches) noexcept {
    // JFIF: Y = .299R + .587G + .114B; Cb = 128 - .168736R - .331264G + .5B; Cr = 128 + .5R - .418688G - .081312B
    // (16-bit fixed point), chroma from each 2x2 block's mean.
    const auto replicate = [](int value) {
        const auto v = static_cast<std::uint32_t>(std::clamp(value, 0, 255));
        return v * 0x01010101u;
    };
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* row = bgra + static_cast<std::ptrdiff_t>(y) * stride;
        auto* luma = reinterpret_cast<std::uint32_t*>(out + offsets[0] + static_cast<std::uint64_t>(y) * pitches[0]);
        for (std::uint32_t x = 0; x < width; ++x) {
            const int b = row[x * 4u];
            const int g = row[x * 4u + 1u];
            const int r = row[x * 4u + 2u];
            luma[x] = replicate((19595 * r + 38470 * g + 7471 * b + 32768) >> 16);
        }
    }
    const std::uint32_t cw = (width + 1u) / 2u;
    const std::uint32_t ch = (height + 1u) / 2u;
    for (std::uint32_t y = 0; y < ch; ++y) {
        const std::uint32_t y0 = y * 2u;
        const std::uint32_t y1 = std::min(y0 + 1u, height - 1u);
        const std::uint8_t* row0 = bgra + static_cast<std::ptrdiff_t>(y0) * stride;
        const std::uint8_t* row1 = bgra + static_cast<std::ptrdiff_t>(y1) * stride;
        auto* cb = reinterpret_cast<std::uint32_t*>(out + offsets[1] + static_cast<std::uint64_t>(y) * pitches[1]);
        auto* cr = reinterpret_cast<std::uint32_t*>(out + offsets[2] + static_cast<std::uint64_t>(y) * pitches[2]);
        for (std::uint32_t x = 0; x < cw; ++x) {
            const std::uint32_t x0 = x * 2u * 4u;
            const std::uint32_t x1 = std::min(x * 2u + 1u, width - 1u) * 4u;
            const int b = row0[x0] + row0[x1] + row1[x0] + row1[x1];
            const int g = row0[x0 + 1u] + row0[x1 + 1u] + row1[x0 + 1u] + row1[x1 + 1u];
            const int r = row0[x0 + 2u] + row0[x1 + 2u] + row1[x0 + 2u] + row1[x1 + 2u];
            // Sums of four: the coefficients carry a quarter.
            cb[x] = replicate(128 + ((-11059 * r - 21709 * g + 32768 * b + 131072) >> 18));
            cr[x] = replicate(128 + ((32768 * r - 27439 * g - 5329 * b + 131072) >> 18));
        }
    }
}

MovieYuvTransform movie_yuv_transform(bool bt709, bool full_range) noexcept {
    const double kr = bt709 ? 0.2126 : 0.299;
    const double kb = bt709 ? 0.0722 : 0.114;
    const double kg = 1.0 - kr - kb;
    const double luma_range = full_range ? 255.0 : 219.0;
    const double chroma_range = full_range ? 255.0 : 224.0;
    // The source's RGB per unit chroma: R = Y + rr Cr, G = Y + gb Cb + gr Cr, B = Y + bb Cb.
    const double rr = 2.0 * (1.0 - kr);
    const double bb = 2.0 * (1.0 - kb);
    const double gb = -2.0 * kb * (1.0 - kb) / kg;
    const double gr = -2.0 * kr * (1.0 - kr) / kg;
    // JPEG's rows (BT.601, full range).
    const double y_r = 0.299, y_g = 0.587, y_b = 0.114;
    const double cb_r = -0.168736, cb_g = -0.331264, cb_b = 0.5;
    const double cr_r = 0.5, cr_g = -0.418688, cr_b = -0.081312;
    const auto fixed = [](double value) { return static_cast<std::int32_t>(std::lround(value * 65536.0)); };
    const double scale = 255.0 / chroma_range;
    MovieYuvTransform t{};
    t.y0 = full_range ? 0 : 16;
    t.ky = fixed(255.0 / luma_range);
    t.kyb = fixed((y_g * gb + y_b * bb) * scale);
    t.kyr = fixed((y_r * rr + y_g * gr) * scale);
    t.kbb = fixed((cb_g * gb + cb_b * bb) * scale);
    t.kbr = fixed((cb_r * rr + cb_g * gr) * scale);
    t.krb = fixed((cr_g * gb + cr_b * bb) * scale);
    t.krr = fixed((cr_r * rr + cr_g * gr) * scale);
    return t;
}

void convert_nv12_to_movie_planes(const std::uint8_t* y, std::int32_t y_pitch, const std::uint8_t* uv,
                                  std::int32_t uv_pitch, std::uint32_t width, std::uint32_t height,
                                  const MovieYuvTransform& t, std::uint8_t* out,
                                  const std::array<std::uint64_t, 3>& offsets,
                                  const std::array<std::uint32_t, 3>& pitches) noexcept {
    const auto replicate = [](std::int32_t value) {
        return static_cast<std::uint32_t>(std::clamp(value, 0, 255)) * 0x01010101u;
    };
    const std::uint32_t cw = (width + 1u) / 2u;
    const std::uint32_t ch = (height + 1u) / 2u;
    const std::int32_t luma_bias = 32768 - t.ky * t.y0;
    for (std::uint32_t j = 0; j < ch; ++j) {
        const std::uint8_t* chroma = uv + static_cast<std::ptrdiff_t>(j) * uv_pitch;
        auto* cb_out = reinterpret_cast<std::uint32_t*>(out + offsets[1] + static_cast<std::uint64_t>(j) * pitches[1]);
        auto* cr_out = reinterpret_cast<std::uint32_t*>(out + offsets[2] + static_cast<std::uint64_t>(j) * pitches[2]);
        const std::uint32_t rows = std::min(2u, height - j * 2u);
        const std::uint8_t* luma_rows[2] = {y + static_cast<std::ptrdiff_t>(j * 2u) * y_pitch,
                                            y + static_cast<std::ptrdiff_t>(j * 2u + rows - 1u) * y_pitch};
        std::uint32_t* luma_out[2] = {
            reinterpret_cast<std::uint32_t*>(out + offsets[0] + static_cast<std::uint64_t>(j * 2u) * pitches[0]),
            reinterpret_cast<std::uint32_t*>(out + offsets[0] + static_cast<std::uint64_t>(j * 2u + rows - 1u) * pitches[0])};
        for (std::uint32_t i = 0; i < cw; ++i) {
            const std::int32_t cb = static_cast<std::int32_t>(chroma[i * 2u]) - 128;
            const std::int32_t cr = static_cast<std::int32_t>(chroma[i * 2u + 1u]) - 128;
            cb_out[i] = replicate(128 + ((t.kbb * cb + t.kbr * cr + 32768) >> 16));
            cr_out[i] = replicate(128 + ((t.krb * cb + t.krr * cr + 32768) >> 16));
            const std::int32_t bias = luma_bias + t.kyb * cb + t.kyr * cr;
            const std::uint32_t x0 = i * 2u;
            const std::uint32_t x1 = std::min(x0 + 1u, width - 1u);
            for (std::uint32_t r = 0; r < rows; ++r) {
                luma_out[r][x0] = replicate((t.ky * luma_rows[r][x0] + bias) >> 16);
                luma_out[r][x1] = replicate((t.ky * luma_rows[r][x1] + bias) >> 16);
            }
        }
    }
}

// Decodes one replacement file on its own thread, up to kAhead frames ahead of the game.
class MovieReplacements::Decoder {
public:
    Decoder(std::filesystem::path file, double rate, std::function<TextureBackend::UploadChunk(std::uint64_t)> stager)
        : file_(std::move(file)), rate_(rate > 0.0 ? rate : 30.0), stager_(std::move(stager)) {
        thread_ = std::thread([this] { run(); });
    }
    ~Decoder() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    // Render thread: the newest decoded frame at or before `frame`; false when there is none (yet).
    bool take(std::int32_t frame, std::int32_t shown, MovieReplacementFrame& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        wanted_ = frame;
        // The game went back (a loop or a restart) or far ahead of what is decoded: start again there.
        const bool behind = shown >= 0 && frame + 2 < shown;
        const bool ahead = !ready_.empty() && ready_.back().frame + static_cast<std::int32_t>(kAhead) * 4 < frame;
        if ((behind || ahead) && seek_to_ < 0) {
            seek_to_ = frame;
            ready_.clear();
            ++seeks_;
            wake_.notify_all();
            return false;
        }
        auto best = ready_.end();
        for (auto it = ready_.begin(); it != ready_.end() && it->frame <= frame; ++it) best = it;
        if (best == ready_.end()) {
            wake_.notify_all();
            return false;
        }
        out = std::move(*best);
        ready_.erase(ready_.begin(), best + 1);
        wake_.notify_all();
        return true;
    }

    [[nodiscard]] bool failed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return failed_;
    }
    void add_stats(Stats& stats) const {
        std::lock_guard<std::mutex> lock(mutex_);
        stats.decoded += decoded_;
        stats.seeks += seeks_;
        stats.convert_ms += convert_ms_;
    }

private:
    std::shared_ptr<TextureBackend::UploadChunk> buffer(std::uint64_t size) {
        // A pooled buffer only this pool still holds (the cache drops its hold when the GPU has copied it).
        for (const auto& chunk : pool_) {
            if (chunk.use_count() == 1 && chunk->size >= size) return chunk;
        }
        if (pool_.size() >= kPoolLimit) {
            // Full: an idle buffer too small for this size gives its place, else every one is in use (wait).
            const auto idle = std::find_if(pool_.begin(), pool_.end(), [](const auto& c) { return c.use_count() == 1; });
            if (idle == pool_.end()) return nullptr;
            pool_.erase(idle);
        }
        auto chunk = std::make_shared<TextureBackend::UploadChunk>(stager_(size));
        if (chunk->cpu_base == nullptr) return nullptr;
        pool_.push_back(chunk);
        return chunk;
    }

#if defined(_WIN32)
    void run() {
        using Microsoft::WRL::ComPtr;
        // A crash while this movie plays names it (NEB-V01).
        const galaxy::crash::ScopedActivity activity(galaxy::crash::Activity::Movie, file_);
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool mf = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
        decode(mf);
        if (mf) MFShutdown();
        if (SUCCEEDED(com)) CoUninitialize();
    }

    void decode(bool mf) {
        using Microsoft::WRL::ComPtr;
        const auto fail = [&](const char* what, HRESULT hr) {
            std::cerr << "[movie] " << file_.filename().string() << ": " << what << " (0x" << std::hex
                      << static_cast<std::uint32_t>(hr) << std::dec << ")\n";
            std::lock_guard<std::mutex> lock(mutex_);
            failed_ = true;
        };
        if (!mf) {
            fail("Media Foundation is unavailable", E_FAIL);
            return;
        }
        ComPtr<IMFAttributes> attributes;
        ComPtr<IMFSourceReader> reader;
        HRESULT hr = MFCreateAttributes(&attributes, 2);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromURL(file_.c_str(), attributes.Get(), &reader);
        if (FAILED(hr)) {
            fail("cannot open", hr);
            return;
        }
        constexpr DWORD kVideo = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
        reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
        reader->SetStreamSelection(kVideo, TRUE);
        // NV12, the decoders' own output: re-encoded as the game's YCbCr with no RGB step. RGB32 (Media Foundation's
        // colour conversion) where a decoder gives no NV12.
        bool nv12 = false;
        for (const GUID& subtype : {MFVideoFormat_NV12, MFVideoFormat_RGB32}) {
            ComPtr<IMFMediaType> type;
            hr = MFCreateMediaType(&type);
            if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_SUBTYPE, subtype);
            if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(kVideo, nullptr, type.Get());
            if (SUCCEEDED(hr)) {
                nv12 = subtype == MFVideoFormat_NV12;
                break;
            }
        }
        if (FAILED(hr)) {
            fail("no decoder for its video", hr);
            return;
        }
        UINT32 coded_width = 0;
        UINT32 coded_height = 0;
        UINT32 width = 0;   // the picture shown (the display aperture)
        UINT32 height = 0;
        UINT32 left = 0;
        UINT32 top = 0;
        LONG stride = 0;
        MovieYuvTransform transform{};
        const auto read_format = [&]() {
            ComPtr<IMFMediaType> current;
            if (FAILED(reader->GetCurrentMediaType(kVideo, &current))) return false;
            if (FAILED(MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &coded_width, &coded_height)) ||
                coded_width == 0u || coded_height == 0u) {
                return false;
            }
            width = coded_width;
            height = coded_height;
            left = top = 0;
            MFVideoArea area{};
            if (SUCCEEDED(current->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof(area),
                                           nullptr)) &&
                area.Area.cx > 0 && area.Area.cy > 0) {
                left = static_cast<UINT32>(std::max<SHORT>(0, area.OffsetX.value));
                top = static_cast<UINT32>(std::max<SHORT>(0, area.OffsetY.value));
                width = std::min<UINT32>(static_cast<UINT32>(area.Area.cx), coded_width - std::min(left, coded_width));
                height = std::min<UINT32>(static_cast<UINT32>(area.Area.cy), coded_height - std::min(top, coded_height));
            }
            UINT32 default_stride = 0;
            stride = SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &default_stride))
                         ? static_cast<LONG>(static_cast<INT32>(default_stride))
                         : static_cast<LONG>(nv12 ? coded_width : coded_width * 4u);
            // The source's colours: its matrix and range, else HD BT.709 / SD BT.601, limited range.
            UINT32 matrix = MFVideoTransferMatrix_Unknown;
            UINT32 range = MFNominalRange_Unknown;
            current->GetUINT32(MF_MT_YUV_MATRIX, &matrix);
            current->GetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, &range);
            const bool bt709 = matrix == MFVideoTransferMatrix_BT709 ||
                               (matrix != MFVideoTransferMatrix_BT601 && coded_height >= 720u);
            transform = movie_yuv_transform(bt709, range == MFNominalRange_0_255);
            return width >= 2u && height >= 2u;
        };
        if (!read_format()) {
            fail("no picture size", E_FAIL);
            return;
        }
        std::cerr << "[movie] " << file_.filename().string() << " " << width << "x" << height
                  << (nv12 ? " nv12" : " rgb32") << "\n";
        std::array<std::uint64_t, 3> offsets{};
        std::array<std::uint32_t, 3> pitches{};
        bool first = true;
        for (;;) {
            std::int32_t seek = -1;
            std::int32_t wanted = 0;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [&] { return stop_ || seek_to_ >= 0 || (!eof_ && ready_.size() < kAhead); });
                if (stop_) return;
                seek = std::exchange(seek_to_, -1);
                wanted = wanted_;
                if (seek >= 0) eof_ = false;
            }
            if (seek >= 0) {
                PROPVARIANT position;
                PropVariantInit(&position);
                position.vt = VT_I8;
                position.hVal.QuadPart = static_cast<LONGLONG>(static_cast<double>(seek) / rate_ * 1.0e7);
                reader->SetCurrentPosition(GUID_NULL, position);
                PropVariantClear(&position);
                first = true;
            }
            DWORD stream = 0;
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;
            hr = reader->ReadSample(kVideo, 0, &stream, &flags, &timestamp, &sample);
            if (FAILED(hr)) {
                fail("decoding stopped", hr);
                return;
            }
            if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0u) {
                std::lock_guard<std::mutex> lock(mutex_);
                eof_ = true;
                continue;
            }
            if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0u && !read_format()) {
                fail("its picture size changed to none", E_FAIL);
                return;
            }
            if (!sample) continue;
            const auto frame = static_cast<std::int32_t>(std::llround(static_cast<double>(timestamp) * 1.0e-7 * rate_));
            // Catching up after a seek or a slow start: frames the game has passed are not converted.
            if (!first && frame + 1 < wanted) continue;
            first = false;
            const std::uint64_t size = movie_plane_layout(width, height, offsets, pitches);
            std::shared_ptr<TextureBackend::UploadChunk> chunk;
            try {
                while (!(chunk = buffer(size))) {
                    std::unique_lock<std::mutex> lock(mutex_);
                    if (stop_) return;
                    wake_.wait_for(lock, std::chrono::milliseconds(4));
                }
            } catch (const std::exception& error) {
                std::cerr << "[movie] upload buffer: " << error.what() << "\n";
                std::lock_guard<std::mutex> lock(mutex_);
                failed_ = true;
                return;
            }
            ComPtr<IMFMediaBuffer> media;
            if (FAILED(sample->ConvertToContiguousBuffer(&media))) continue;
            const auto start = std::chrono::steady_clock::now();
            ComPtr<IMF2DBuffer> planar;
            BYTE* scan0 = nullptr;
            LONG pitch = 0;
            bool locked2d = false;
            BYTE* data = nullptr;
            DWORD length = 0;
            if (SUCCEEDED(media.As(&planar)) && SUCCEEDED(planar->Lock2D(&scan0, &pitch))) {
                locked2d = true;
            } else if (SUCCEEDED(media->Lock(&data, nullptr, &length))) {
                pitch = stride;
                scan0 = stride < 0 ? data + static_cast<std::ptrdiff_t>(coded_height - 1u) * -stride : data;
                const std::uint64_t needed = static_cast<std::uint64_t>(std::abs(stride)) * coded_height *
                                             (nv12 ? 3u : 2u) / 2u;
                if (needed > length) {
                    media->Unlock();
                    continue;
                }
            } else {
                continue;
            }
            if (nv12) {
                // The interleaved chroma follows the coded rows.
                const BYTE* luma = scan0 + static_cast<std::ptrdiff_t>(top) * pitch + left;
                const BYTE* chroma = scan0 + static_cast<std::ptrdiff_t>(coded_height) * pitch +
                                     static_cast<std::ptrdiff_t>(top / 2u) * pitch + (left & ~1u);
                convert_nv12_to_movie_planes(luma, pitch, chroma, pitch, width, height, transform, chunk->cpu_base,
                                             offsets, pitches);
            } else {
                convert_bgra_to_movie_planes(scan0 + static_cast<std::ptrdiff_t>(top) * pitch + left * 4u, width,
                                             height, pitch, chunk->cpu_base, offsets, pitches);
            }
            if (locked2d) planar->Unlock2D(); else media->Unlock();
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            MovieReplacementFrame decoded{};
            decoded.frame = frame;
            decoded.width = width;
            decoded.height = height;
            decoded.staging = std::move(chunk);
            decoded.offsets = offsets;
            decoded.pitches = pitches;
            std::lock_guard<std::mutex> lock(mutex_);
            if (seek_to_ >= 0) continue;  // a seek came in meanwhile: this frame is from before it
            ready_.push_back(std::move(decoded));
            ++decoded_;
            convert_ms_ += ms;
        }
    }
#else
    void run() {
        std::lock_guard<std::mutex> lock(mutex_);
        failed_ = true;
    }
#endif

    std::filesystem::path file_;
    double rate_;
    std::function<TextureBackend::UploadChunk(std::uint64_t)> stager_;
    std::vector<std::shared_ptr<TextureBackend::UploadChunk>> pool_;  // decoder thread only
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<MovieReplacementFrame> ready_;
    std::int32_t wanted_ = 0;
    std::int32_t seek_to_ = -1;
    bool stop_ = false;
    bool eof_ = false;
    bool failed_ = false;
    std::uint64_t decoded_ = 0;
    std::uint64_t seeks_ = 0;
    double convert_ms_ = 0.0;
    std::thread thread_;
};

MovieReplacements::MovieReplacements() = default;

MovieReplacements::~MovieReplacements() {
    shutdown();
}

void MovieReplacements::configure(std::vector<std::filesystem::path> folders,
                                  std::function<TextureBackend::UploadChunk(std::uint64_t)> stager) {
    if (folders != folders_) {
        close();
        folders_ = std::move(folders);
    }
    stager_ = std::move(stager);
}

void MovieReplacements::shutdown() noexcept {
    close();
}

bool MovieReplacements::showing() noexcept {
    return s_showing.load(std::memory_order_relaxed);
}

void MovieReplacements::close() noexcept {
    s_showing.store(false, std::memory_order_relaxed);
    decoder_.reset();
    shown_ = {};
    current_name_.clear();
    current_missing_ = false;
}

std::filesystem::path MovieReplacements::find(const char* disc_path) const {
#if defined(_WIN32)
    if (disc_path == nullptr) return {};
    const std::string path(disc_path);
    const std::size_t slash = path.find_last_of('/');
    std::string stem = ascii_lower(path.substr(slash == std::string::npos ? 0 : slash + 1u));
    if (const std::size_t dot = stem.find_last_of('.'); dot != std::string::npos) stem.resize(dot);
    if (stem.empty()) return {};
    for (auto folder = folders_.rbegin(); folder != folders_.rend(); ++folder) {
        std::error_code error;
        for (std::filesystem::directory_iterator it(*folder, error), end; !error && it != end; it.increment(error)) {
            if (!it->is_regular_file(error)) continue;
            const std::string name = ascii_lower(it->path().stem().string());
            const std::string extension = ascii_lower(it->path().extension().string());
            if (name != stem) continue;
            for (const char* known : kExtensions) {
                if (extension == known) return it->path();
            }
        }
    }
#else
    (void)disc_path;
#endif
    return {};
}

const MovieReplacementFrame* MovieReplacements::frame(const MovieFrameMark& mark) {
    if (mark.frame == MovieFrameMark::kNone) {
        if (!current_name_.empty()) close();
        return nullptr;
    }
    const std::string name(mark.name.data(), strnlen(mark.name.data(), mark.name.size()));
    if (name != current_name_) {
        close();
        current_name_ = name;
        const std::filesystem::path file = find(name.c_str());
        current_missing_ = file.empty() || !stager_;
        if (!current_missing_) {
            decoder_ = std::make_unique<Decoder>(file, mark.frame_rate, stager_);
            ++opened_;
            std::cerr << "[movie] " << name << " -> " << file.string() << "\n";
        }
    }
    if (current_missing_ || decoder_ == nullptr || mark.frame < 0) {
        return nullptr;
    }
    if (decoder_->failed()) {
        s_showing.store(false, std::memory_order_relaxed);
        return nullptr;
    }
    MovieReplacementFrame next{};
    if (decoder_->take(mark.frame, shown_.frame, next)) {
        shown_ = std::move(next);
    } else if (shown_.frame < 0 || shown_.frame + 2 < mark.frame) {
        ++late_;
    }
    if (shown_.frame < 0 || !shown_.staging) return nullptr;
    // Within a few frames of the game's: the THP's decode may stop (it resumes if the decoder falls behind).
    s_showing.store(shown_.frame + 8 >= mark.frame, std::memory_order_relaxed);
    ++shown_count_;
    return &shown_;
}

MovieReplacements::Stats MovieReplacements::stats() const {
    Stats stats{};
    stats.opened = opened_;
    stats.shown = shown_count_;
    stats.late = late_;
    if (decoder_) decoder_->add_stats(stats);
    return stats;
}

}  // namespace galaxy::gx
