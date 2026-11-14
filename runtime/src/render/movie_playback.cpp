// SPDX-License-Identifier: GPL-3.0-only
// The game's movies (movie_playback.h). The cooked movie decodes with galaxy/movie_video.h on its own thread. An HD
// replacement decodes with Media Foundation's source reader on Windows, on the GPU's video decoder where it has one for
// the format (through a D3D11 device), else in software; its planes are JPEG's YCbCr of that picture.

#include "galaxy/render/movie_playback.h"
#include "galaxy/cpu_count.h"
#include "galaxy/movie_video.h"
#include "galaxy/path_text.h"

#include "galaxy/crash_report.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#include <d3d11.h>
#include <d3d10.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>
#include <wrl/client.h>
#endif

namespace galaxy::render {

namespace {

// Decoded frames kept ahead of the game: 133 ms at 60 fps, room for a slow frame of the decoder or the copy back.
constexpr std::size_t kAhead = 8;
constexpr std::size_t kPoolLimit = 16;    // upload buffers a movie may hold (ahead, shown, in flight)
constexpr const char* kExtensions[] = {".mp4", ".m4v", ".mov", ".mkv", ".webm", ".avi", ".wmv"};

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
        pitches[p] = (w + TextureBackend::kPitchAlignment - 1u) & ~(TextureBackend::kPitchAlignment - 1u);
        size += static_cast<std::uint64_t>(pitches[p]) * h;
    }
    return size;
}

namespace {

std::uint8_t clamp_byte(std::int32_t value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

// Rows of a picture converted in bands on a few worker threads and the calling one: a 4K frame's planes in a few
// milliseconds on a four-core CPU, where one thread would take most of a frame's time.
class RowBands {
public:
    RowBands() {
        // Half the logical processors help the calling thread; a picture uses as many bands as it has rows for (run).
        const unsigned cores = logical_cores();
        const unsigned helpers = cores > 2u ? cores / 2u : 0u;
        for (unsigned i = 0; i < helpers; ++i) {
            workers_.emplace_back([this, i] { work(i); });
        }
    }
    ~RowBands() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        start_.notify_all();
        for (std::thread& worker : workers_) worker.join();
    }

    // fn(first, end) over [0, rows), one band per thread; returns when every band is done. One caller at a time.
    void run(std::uint32_t rows, const std::function<void(std::uint32_t, std::uint32_t)>& fn) {
        // A band is at least 64 rows: a small picture does not wake helpers it cannot use.
        const auto bands = static_cast<std::uint32_t>(std::min<std::size_t>(workers_.size() + 1u, std::max(1u, rows / 64u)));
        if (bands == 1u || rows < 64u) {
            fn(0u, rows);
            return;
        }
        const std::lock_guard<std::mutex> caller(caller_);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            fn_ = &fn;
            rows_ = rows;
            bands_ = bands;
            pending_ = bands - 1u;
            ++generation_;
        }
        start_.notify_all();
        fn(rows * (bands - 1u) / bands, rows);
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [&] { return pending_ == 0u; });
        fn_ = nullptr;
    }

private:
    void work(unsigned band) {
        std::uint64_t seen = 0;
        for (;;) {
            const std::function<void(std::uint32_t, std::uint32_t)>* fn = nullptr;
            std::uint32_t begin = 0;
            std::uint32_t end = 0;
            bool idle = false;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                start_.wait(lock, [&] { return stop_ || generation_ != seen; });
                if (stop_) return;
                seen = generation_;
                fn = fn_;
                begin = rows_ * band / bands_;
                end = rows_ * (band + 1u) / bands_;
                idle = band + 1u >= bands_;  // the caller takes the last band; helpers past the bands in use sit this one out
            }
            if (idle) {
                continue;
            }
            (*fn)(begin, end);
            std::lock_guard<std::mutex> lock(mutex_);
            if (--pending_ == 0u) done_.notify_one();
        }
    }

    std::vector<std::thread> workers_;
    std::mutex caller_;
    std::mutex mutex_;
    std::condition_variable start_;
    std::condition_variable done_;
    const std::function<void(std::uint32_t, std::uint32_t)>* fn_ = nullptr;
    std::uint32_t rows_ = 0;
    std::uint32_t bands_ = 1;
    std::uint32_t pending_ = 0;
    std::uint64_t generation_ = 0;
    bool stop_ = false;
};

RowBands& row_bands() {
    static RowBands bands;
    return bands;
}

}  // namespace

void convert_bgra_to_movie_planes(const std::uint8_t* bgra, std::uint32_t width, std::uint32_t height,
                                  std::int32_t stride, std::uint8_t* out, const std::array<std::uint64_t, 3>& offsets,
                                  const std::array<std::uint32_t, 3>& pitches) noexcept {
    // JFIF: Y = .299R + .587G + .114B; Cb = 128 - .168736R - .331264G + .5B; Cr = 128 + .5R - .418688G - .081312B
    // (16-bit fixed point), chroma from each 2x2 block's mean.
    const std::uint32_t cw = (width + 1u) / 2u;
    const std::uint32_t ch = (height + 1u) / 2u;
    row_bands().run(ch, [&](std::uint32_t first, std::uint32_t end) {
        for (std::uint32_t j = first; j < end; ++j) {
            const std::uint32_t y0 = j * 2u;
            const std::uint32_t y1 = std::min(y0 + 1u, height - 1u);
            for (std::uint32_t y = y0; y <= y1; ++y) {
                const std::uint8_t* row = bgra + static_cast<std::ptrdiff_t>(y) * stride;
                std::uint8_t* luma = out + offsets[0] + static_cast<std::uint64_t>(y) * pitches[0];
                for (std::uint32_t x = 0; x < width; ++x) {
                    const int b = row[x * 4u];
                    const int g = row[x * 4u + 1u];
                    const int r = row[x * 4u + 2u];
                    luma[x] = clamp_byte((19595 * r + 38470 * g + 7471 * b + 32768) >> 16);
                }
            }
            const std::uint8_t* row0 = bgra + static_cast<std::ptrdiff_t>(y0) * stride;
            const std::uint8_t* row1 = bgra + static_cast<std::ptrdiff_t>(y1) * stride;
            std::uint8_t* cb = out + offsets[1] + static_cast<std::uint64_t>(j) * pitches[1];
            std::uint8_t* cr = out + offsets[2] + static_cast<std::uint64_t>(j) * pitches[2];
            for (std::uint32_t x = 0; x < cw; ++x) {
                const std::uint32_t x0 = x * 2u * 4u;
                const std::uint32_t x1 = std::min(x * 2u + 1u, width - 1u) * 4u;
                const int b = row0[x0] + row0[x1] + row1[x0] + row1[x1];
                const int g = row0[x0 + 1u] + row0[x1 + 1u] + row1[x0 + 1u] + row1[x1 + 1u];
                const int r = row0[x0 + 2u] + row0[x1 + 2u] + row1[x0 + 2u] + row1[x1 + 2u];
                // Sums of four: the coefficients carry a quarter.
                cb[x] = clamp_byte(128 + ((-11059 * r - 21709 * g + 32768 * b + 131072) >> 18));
                cr[x] = clamp_byte(128 + ((32768 * r - 27439 * g - 5329 * b + 131072) >> 18));
            }
        }
    });
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
    const std::uint32_t cw = (width + 1u) / 2u;
    const std::uint32_t ch = (height + 1u) / 2u;
    // A source already in JPEG's YCbCr (full-range BT.601): the planes are its own bytes.
    const bool identity = t.y0 == 0 && t.ky == 65536 && t.kyb == 0 && t.kyr == 0 && t.kbb == 65536 && t.kbr == 0 &&
                          t.krb == 0 && t.krr == 65536;
    const std::int32_t luma_bias = 32768 - t.ky * t.y0;
    row_bands().run(ch, [&](std::uint32_t first, std::uint32_t end) {
        for (std::uint32_t j = first; j < end; ++j) {
            const std::uint8_t* chroma = uv + static_cast<std::ptrdiff_t>(j) * uv_pitch;
            std::uint8_t* cb_out = out + offsets[1] + static_cast<std::uint64_t>(j) * pitches[1];
            std::uint8_t* cr_out = out + offsets[2] + static_cast<std::uint64_t>(j) * pitches[2];
            const std::uint32_t rows = std::min(2u, height - j * 2u);
            for (std::uint32_t r = 0; r < rows; ++r) {
                const std::uint8_t* luma = y + static_cast<std::ptrdiff_t>(j * 2u + r) * y_pitch;
                std::uint8_t* luma_out = out + offsets[0] + static_cast<std::uint64_t>(j * 2u + r) * pitches[0];
                if (identity) {
                    std::memcpy(luma_out, luma, width);
                    continue;
                }
                for (std::uint32_t i = 0; i < cw; ++i) {
                    const std::int32_t cb = static_cast<std::int32_t>(chroma[i * 2u]) - 128;
                    const std::int32_t cr = static_cast<std::int32_t>(chroma[i * 2u + 1u]) - 128;
                    const std::int32_t bias = luma_bias + t.kyb * cb + t.kyr * cr;
                    const std::uint32_t x0 = i * 2u;
                    const std::uint32_t x1 = std::min(x0 + 1u, width - 1u);
                    luma_out[x0] = clamp_byte((t.ky * luma[x0] + bias) >> 16);
                    luma_out[x1] = clamp_byte((t.ky * luma[x1] + bias) >> 16);
                }
            }
            for (std::uint32_t i = 0; i < cw; ++i) {
                if (identity) {
                    cb_out[i] = chroma[i * 2u];
                    cr_out[i] = chroma[i * 2u + 1u];
                    continue;
                }
                const std::int32_t cb = static_cast<std::int32_t>(chroma[i * 2u]) - 128;
                const std::int32_t cr = static_cast<std::int32_t>(chroma[i * 2u + 1u]) - 128;
                cb_out[i] = clamp_byte(128 + ((t.kbb * cb + t.kbr * cr + 32768) >> 16));
                cr_out[i] = clamp_byte(128 + ((t.krb * cb + t.krr * cr + 32768) >> 16));
            }
        }
    });
}

#if defined(_WIN32)
namespace {

// The GPU's video decoder for every movie: Media Foundation uses it only through a D3D device manager. Made once, ahead
// of the first movie (MoviePlayback::configure), so a movie's first frames are not held up by it; null where the
// GPU has no D3D11 video device (the movies decode in software).
Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> video_device_manager() {
    static std::once_flag once;
    static Microsoft::WRL::ComPtr<ID3D11Device> device;
    static Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> manager;
    std::call_once(once, [] {
        UINT reset_token = 0;
        Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> made;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                     D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                     D3D11_SDK_VERSION, &device, nullptr, nullptr)) ||
            FAILED(MFCreateDXGIDeviceManager(&reset_token, &made)) ||
            FAILED(made->ResetDevice(device.Get(), reset_token))) {
            device.Reset();
            return;
        }
        Microsoft::WRL::ComPtr<ID3D10Multithread> multithread;
        if (SUCCEEDED(device.As(&multithread))) multithread->SetMultithreadProtected(TRUE);
        manager = made;
    });
    return manager;
}

}  // namespace
#endif

// Decodes one replacement file on its own thread, up to kAhead frames ahead of the game.
class MoviePlayback::Decoder {
public:
    // `rate`: the THP's frame rate, or 0 while it is not known (the game opens the movie before it reads the
    // header; set_rate gives it with the first frame).
    Decoder(std::filesystem::path file, double rate, std::function<TextureBackend::UploadChunk(std::uint64_t)> stager)
        : file_(std::move(file)), rate_(rate > 0.0 ? rate : 0.0), stager_(std::move(stager)) {
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
    bool take(std::int32_t frame, std::int32_t shown, MovieFrame& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        wanted_ = frame;
        if (!(rate_ > 0.0)) return false;
        number_ready();
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

    // How many of the game's frames one of the file's lasts.
    [[nodiscard]] std::int32_t frame_step() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return file_rate_ > 0.0 && rate_ > file_rate_ ? std::max<std::int32_t>(1, static_cast<std::int32_t>(std::llround(rate_ / file_rate_))) : 1;
    }

    void set_rate(double rate) {
        if (!(rate > 0.0)) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (rate_ > 0.0) return;
        rate_ = rate;
        number_ready();
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
    // The rate frames are numbered by (mutex_ held, rate known). A file at the THP's nominal rate (60 for 59.94) is the
    // THP frame for frame and is numbered by its own rate, which keeps it in step to the end (the THP's rate would
    // drift a frame every 17 s); other rates are placed by time.
    double index_rate() const {
        return file_rate_ > 0.0 && std::abs(file_rate_ - rate_) < 0.5 ? file_rate_ : rate_;
    }
    // A frame's number from its presentation time, counted from the file's first frame: an H.264 file with B-frames
    // starts a few frames after 0, which would otherwise show every frame that many early.
    std::int32_t frame_at(std::int64_t time) const {
        return static_cast<std::int32_t>(
            std::llround(static_cast<double>(time - start_time_) * 1.0e-7 * index_rate()));
    }
    // Frames decoded before the rate was known get their numbers (mutex_ held).
    void number_ready() {
        if (!(rate_ > 0.0)) return;
        for (MovieFrame& ready : ready_) {
            if (ready.frame < 0) ready.frame = frame_at(ready.time);
        }
    }

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
        // Media Foundation is delay-loaded: on a Windows edition without it the first call would raise the loader's
        // exception instead of failing, so its two libraries are asked for first (once; they stay loaded).
        static const bool present = LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) != nullptr &&
                                    LoadLibraryExW(L"mfreadwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) != nullptr;
        const bool mf = present && SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
        decode(mf);
        if (mf) MFShutdown();
        if (SUCCEEDED(com)) CoUninitialize();
    }

    void decode(bool mf) {
        using Microsoft::WRL::ComPtr;
        const auto fail = [&](const char* what, HRESULT hr) {
            std::cerr << "[movie] " << galaxy::path_text(file_.filename()) << ": " << what << " (0x" << std::hex
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
        HRESULT hr = MFCreateAttributes(&attributes, 3);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        // Decoded on the GPU and only copied back where it can (video_device_manager), else in software.
        if (const ComPtr<IMFDXGIDeviceManager> device_manager = video_device_manager(); SUCCEEDED(hr) && device_manager) {
            attributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, device_manager.Get());
        }
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
        double file_rate = 0.0;
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
            UINT32 numerator = 0;
            UINT32 denominator = 0;
            file_rate = SUCCEEDED(MFGetAttributeRatio(current.Get(), MF_MT_FRAME_RATE, &numerator, &denominator)) &&
                                numerator != 0u && denominator != 0u
                            ? static_cast<double>(numerator) / static_cast<double>(denominator)
                            : 0.0;
            return width >= 2u && height >= 2u;
        };
        if (!read_format()) {
            fail("no picture size", E_FAIL);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            file_rate_ = file_rate;
        }
        std::cerr << "[movie] " << galaxy::path_text(file_.filename()) << " " << width << "x" << height
                  << (nv12 ? " nv12" : " rgb32") << " at " << file_rate << " fps\n";
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
                double rate = 0.0;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    rate = index_rate();
                }
                position.hVal.QuadPart =
                    static_cast<LONGLONG>(static_cast<double>(seek) / rate * 1.0e7) + start_time_;
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
                fail("no picture size after a format change", E_FAIL);
                return;
            }
            if (!sample) continue;
            // Numbered now if the game has given the THP's rate; else when it does (the first frames, decoded
            // while the game reads the movie's header).
            std::int32_t frame = -1;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (start_time_ < 0) start_time_ = timestamp;
                if (rate_ > 0.0) frame = frame_at(timestamp);
            }
            // Catching up after a seek or a slow start: frames the game has passed are not converted.
            if (!first && frame >= 0 && frame + 1 < wanted) continue;
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
            MovieFrame decoded{};
            decoded.frame = frame;
            decoded.time = timestamp;
            decoded.width = width;
            decoded.height = height;
            decoded.staging = std::move(chunk);
            decoded.offsets = offsets;
            decoded.pitches = pitches;
            std::lock_guard<std::mutex> lock(mutex_);
            if (seek_to_ >= 0) continue;  // a seek came in meanwhile: this frame is from before it
            if (decoded.frame < 0 && rate_ > 0.0) decoded.frame = frame_at(decoded.time);
            if (decoded_ == 0u) {
                std::cerr << "[movie] first frame after "
                          << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                                   opened_)
                                 .count()
                          << " ms\n";
            }
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
    double rate_;  // the THP's frame rate; 0 until the game's first frame gives it (mutex_)
    double file_rate_ = 0.0;  // the file's own frame rate, 0 when it has none (mutex_)
    std::int64_t start_time_ = -1;  // the first frame's presentation time, 100 ns units (mutex_)
    std::chrono::steady_clock::time_point opened_ = std::chrono::steady_clock::now();
    std::function<TextureBackend::UploadChunk(std::uint64_t)> stager_;
    std::vector<std::shared_ptr<TextureBackend::UploadChunk>> pool_;  // decoder thread only
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<MovieFrame> ready_;
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

CookedMovieIndex index_cooked_movie(const std::uint8_t* data, std::uint64_t size) {
    // The header (0x40 bytes: "NMOV", frames, rate, largest, first size, width, height, tracks) and each frame's
    // (0x10: next size, video size, audio size, 0), host order.
    constexpr std::uint64_t kHeader = 0x40;
    constexpr std::uint64_t kFrameHeader = 0x10;
    CookedMovieIndex index;
    if (data == nullptr || size < kHeader || std::memcmp(data, "NMOV", 4) != 0) {
        return index;
    }
    const auto word = [data](std::uint64_t at) {
        std::uint32_t value = 0;
        std::memcpy(&value, data + at, sizeof(value));
        return value;
    };
    const std::uint32_t frames = word(4);
    const std::uint32_t width = word(20);
    const std::uint32_t height = word(24);
    // Each frame takes at least its header, so a count the file cannot hold is refused before anything is reserved.
    if (frames == 0u || width == 0u || height == 0u || width > 4096u || height > 4096u ||
        frames > (size - kHeader) / kFrameHeader) {
        return index;
    }
    index.video_at.reserve(frames);
    index.video_size.reserve(frames);
    std::uint64_t at = kHeader;
    std::uint32_t frame_size = word(16);
    for (std::uint32_t f = 0; f < frames; ++f) {
        if (at > size || size - at < kFrameHeader) {
            return {};
        }
        const std::uint32_t video = word(at + 4);
        if (video > size - at - kFrameHeader) {
            return {};
        }
        index.video_at.push_back(at + kFrameHeader);
        index.video_size.push_back(video);
        const std::uint32_t next = word(at);
        at += frame_size;
        frame_size = next;
    }
    index.width = width;
    index.height = height;
    return index;
}

namespace {

// Cooked frames decoded past the one the render thread took last: the next one and one more. A frame takes about half a
// millisecond, so this covers a slow decode or a frame the game passes over.
constexpr std::size_t kCookedAhead = 2;
// A frame up to this many past the decoder's place is waited for; farther, or behind, the decoder starts again there.
constexpr std::int32_t kCookedReach = 4;
// The longest the render thread waits for one frame before the last one shows again.
constexpr std::chrono::milliseconds kCookedWaitLimit{250};

}  // namespace

// The game's cooked movie on its own thread, in order from where the render thread takes it, wrapping at the end (a
// looping movie goes on; one that ends costs a frame or two). Every frame stands alone, so a jump is only a new place to
// start.
class MoviePlayback::CookedDecoder {
public:
    CookedDecoder(std::string name, CookedMovieIndex index, const std::uint8_t* data,
                  std::function<TextureBackend::UploadChunk(std::uint64_t)> stager)
        : name_(std::move(name)), index_(std::move(index)), data_(data), stager_(std::move(stager)) {
        thread_ = std::thread([this] { run(); });
    }
    ~CookedDecoder() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        ready_signal_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    // `frame` (0 or more) within the movie's frames.
    [[nodiscard]] std::int32_t wrap(std::int32_t frame) const noexcept {
        return frame % static_cast<std::int32_t>(index_.video_at.size());
    }

    // Render thread: frame `frame` (wrapped), waiting while the decoder is behind; false when it did not decode or did
    // not come within kCookedWaitLimit.
    bool take(std::int32_t frame, MovieFrame& out) {
        const auto started = std::chrono::steady_clock::now();
        std::unique_lock<std::mutex> lock(mutex_);
        bool waited = false;
        for (;;) {
            // Frames before this one are passed over.
            while (!ready_.empty() && ready_.front().frame != frame &&
                   distance(ready_.front().frame, frame) <= kCookedReach) {
                ready_.pop_front();
                wake_.notify_all();
            }
            if (!ready_.empty() && ready_.front().frame == frame) {
                break;
            }
            if (stop_ || broken_) {
                return false;
            }
            // Queued frames after this one, or this one far from the decoder's place: start again at it.
            if (!ready_.empty() || distance(place(), frame) > kCookedReach) {
                ready_.clear();
                next_ = frame;
                ++generation_;
                ++seeks_;
                wake_.notify_all();
            }
            waited = true;
            if (ready_signal_.wait_until(lock, started + kCookedWaitLimit) == std::cv_status::timeout) {
                ++timeouts_;
                return false;
            }
        }
        Entry entry = std::move(ready_.front());
        ready_.pop_front();
        wake_.notify_all();
        if (waited) {
            ++waits_;
            wait_ms_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        }
        if (!entry.picture.staging) {
            return false;  // its data did not decode
        }
        out = std::move(entry.picture);
        ++shown_;
        return true;
    }

    void add_stats(Stats& stats) const {
        std::lock_guard<std::mutex> lock(mutex_);
        stats.cooked_decoded += decoded_;
        stats.cooked_shown += shown_;
        stats.cooked_waits += waits_ + timeouts_;
        stats.cooked_seeks += seeks_;
        stats.cooked_failed += failed_ + timeouts_;
        stats.cooked_decode_ms += decode_ms_;
        stats.cooked_wait_ms += wait_ms_;
    }

private:
    struct Entry {
        std::int32_t frame = -1;
        MovieFrame picture;  // no staging: the frame's data did not decode
    };

    // Frames from `from` forward to `to`, wrapping.
    [[nodiscard]] std::int32_t distance(std::int32_t from, std::int32_t to) const noexcept {
        const auto count = static_cast<std::int32_t>(index_.video_at.size());
        return (to - from + count) % count;
    }
    // The first frame the decoder will hand over next (mutex_ held, ready_ empty).
    [[nodiscard]] std::int32_t place() const noexcept {
        return decoding_ >= 0 && decoding_generation_ == generation_ ? decoding_ : next_;
    }

    std::shared_ptr<TextureBackend::UploadChunk> buffer(std::uint64_t size) {
        // A pooled buffer only this pool still holds (the texture store drops its hold when the GPU has copied it).
        for (const auto& chunk : pool_) {
            if (chunk.use_count() == 1 && chunk->size >= size) return chunk;
        }
        if (pool_.size() >= kPoolLimit) {
            const auto idle = std::find_if(pool_.begin(), pool_.end(), [](const auto& c) { return c.use_count() == 1; });
            if (idle == pool_.end()) return nullptr;
            pool_.erase(idle);
        }
        auto chunk = std::make_shared<TextureBackend::UploadChunk>(stager_(size));
        if (chunk->cpu_base == nullptr) return nullptr;
        pool_.push_back(chunk);
        return chunk;
    }

    void run() {
        // A crash while this movie decodes names it.
        const galaxy::crash::ScopedActivity activity(galaxy::crash::Activity::Movie, std::filesystem::path(name_));
        const auto count = static_cast<std::int32_t>(index_.video_at.size());
        const std::uint32_t width = index_.width;
        const std::uint32_t height = index_.height;
        std::array<std::uint64_t, 3> offsets{};
        std::array<std::uint32_t, 3> pitches{};
        const std::uint64_t bytes = movie_plane_layout(width, height, offsets, pitches);
        const std::uint32_t chroma_rows = (height + 1u) / 2u;
        for (;;) {
            std::int32_t frame = 0;
            std::uint64_t generation = 0;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [&] { return stop_ || ready_.size() < kCookedAhead; });
                if (stop_) return;
                frame = next_;
                next_ = (next_ + 1) % count;
                generation = generation_;
                decoding_ = frame;
                decoding_generation_ = generation;
            }
            std::shared_ptr<TextureBackend::UploadChunk> chunk;
            try {
                while (!(chunk = buffer(bytes))) {
                    std::unique_lock<std::mutex> lock(mutex_);
                    if (stop_) return;
                    wake_.wait_for(lock, std::chrono::milliseconds(4));
                }
            } catch (const std::exception& error) {
                std::cerr << "[movie] " << name_ << ": upload buffer: " << error.what() << "\n";
                std::lock_guard<std::mutex> lock(mutex_);
                broken_ = true;
                ready_signal_.notify_all();
                return;
            }
            const auto start = std::chrono::steady_clock::now();
            std::uint8_t* const base = chunk->cpu_base;
            const std::int32_t result = galaxy::movie::decode_video(
                {data_ + index_.video_at[static_cast<std::size_t>(frame)], index_.video_size[static_cast<std::size_t>(frame)]},
                static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height),
                {base + offsets[0], static_cast<std::size_t>(pitches[0]) * height},
                {base + offsets[1], static_cast<std::size_t>(pitches[1]) * chroma_rows},
                {base + offsets[2], static_cast<std::size_t>(pitches[2]) * chroma_rows}, galaxy::movie::VideoKernel::Automatic,
                pitches);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            Entry entry{};
            entry.frame = frame;
            if (result == 0) {
                entry.picture.frame = frame;
                entry.picture.width = width;
                entry.picture.height = height;
                entry.picture.staging = std::move(chunk);
                entry.picture.offsets = offsets;
                entry.picture.pitches = pitches;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                decoding_ = -1;
                if (generation != generation_) continue;  // a jump came meanwhile: this frame is from before it
                if (result == 0) {
                    ++decoded_;
                    decode_ms_ += ms;
                } else {
                    if (failed_ == 0u) {
                        std::cerr << "[movie] " << name_ << " frame " << frame << ": the decoder refused it (" << result << ")\n";
                    }
                    ++failed_;
                }
                ready_.push_back(std::move(entry));
            }
            ready_signal_.notify_all();
        }
    }

    std::string name_;
    CookedMovieIndex index_;
    const std::uint8_t* data_;
    std::function<TextureBackend::UploadChunk(std::uint64_t)> stager_;
    std::vector<std::shared_ptr<TextureBackend::UploadChunk>> pool_;  // decoder thread only
    mutable std::mutex mutex_;
    std::condition_variable wake_;          // the decoder: room in ready_, a jump, or stop
    std::condition_variable ready_signal_;  // the render thread: a frame came
    std::deque<Entry> ready_;               // consecutive frames from the next one the render thread takes
    std::int32_t next_ = 0;                 // the frame the decoder takes next
    std::int32_t decoding_ = -1;            // the frame being decoded, of decoding_generation_
    std::uint64_t decoding_generation_ = 0;
    std::uint64_t generation_ = 0;          // counts jumps; a frame decoded before the last is dropped
    bool stop_ = false;
    bool broken_ = false;  // the decoder thread has ended
    std::uint64_t decoded_ = 0;
    std::uint64_t shown_ = 0;
    std::uint64_t waits_ = 0;
    std::uint64_t timeouts_ = 0;
    std::uint64_t seeks_ = 0;
    std::uint64_t failed_ = 0;
    double decode_ms_ = 0.0;
    double wait_ms_ = 0.0;
    std::thread thread_;
};

MoviePlayback::MoviePlayback() = default;

MoviePlayback::~MoviePlayback() {
    shutdown();
}

void MoviePlayback::configure(std::vector<std::filesystem::path> folders,
                              std::function<TextureBackend::UploadChunk(std::uint64_t)> stager) {
    if (folders != folders_) {
        close();
        folders_ = std::move(folders);
#if defined(_WIN32)
        // The video decoder's device, ready before the first movie opens.
        if (!folders_.empty()) std::thread([] { (void)video_device_manager(); }).detach();
#endif
    }
    stager_ = std::move(stager);
}

void MoviePlayback::shutdown() noexcept {
    close();
}

void MoviePlayback::close() noexcept {
    if (decoder_) decoder_->add_stats(retired_);
    if (cooked_) cooked_->add_stats(retired_);
    decoder_.reset();
    cooked_.reset();
    cooked_data_ = nullptr;
    shown_ = {};
    cooked_shown_ = {};
    current_name_.clear();
    current_missing_ = false;
}

std::filesystem::path MoviePlayback::find(const char* disc_path) const {
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
            const std::string name = ascii_lower(galaxy::path_text(it->path().stem()));
            const std::string extension = ascii_lower(galaxy::path_text(it->path().extension()));
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

const MovieFrame* MoviePlayback::frame(const MovieFrameMark& mark) {
    if (mark.frame == MovieFrameMark::kNone) {
        if (!current_name_.empty() || cooked_) close();
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
            std::cerr << "[movie] " << name << " -> " << galaxy::path_text(file) << "\n";
        }
    }
    if (mark.data != nullptr && mark.data != cooked_data_ && stager_) {
        if (cooked_) cooked_->add_stats(retired_);
        cooked_.reset();
        cooked_shown_ = {};
        cooked_data_ = mark.data;
        CookedMovieIndex index = index_cooked_movie(mark.data, mark.size);
        if (index.video_at.empty()) {
            std::cerr << "[movie] " << name << ": not a cooked movie\n";
        } else {
            cooked_ = std::make_unique<CookedDecoder>(name, std::move(index), mark.data, stager_);
        }
    }
    if (mark.frame < 0) {
        return nullptr;
    }
    // A replacement's frame once it has one.
    if (!current_missing_ && decoder_ != nullptr && !decoder_->failed()) {
        decoder_->set_rate(mark.frame_rate);
        MovieFrame next{};
        if (decoder_->take(mark.frame, shown_.frame, next)) {
            shown_ = std::move(next);
        }
        if (shown_.frame >= 0 && shown_.staging) {
            if (shown_.frame + decoder_->frame_step() <= mark.frame) {
                ++late_;  // the game's frame is not decoded yet: an older one shows
            }
            ++shown_count_;
            return &shown_;
        }
    }
    if (cooked_ == nullptr) {
        return nullptr;
    }
    const std::int32_t frame = cooked_->wrap(mark.frame);
    if (!cooked_shown_.staging || cooked_shown_.frame != frame) {
        MovieFrame next{};
        if (cooked_->take(frame, next)) {
            cooked_shown_ = std::move(next);
        }
    }
    return cooked_shown_.staging ? &cooked_shown_ : nullptr;
}

MoviePlayback::Stats MoviePlayback::stats() const {
    Stats stats = retired_;
    stats.opened += opened_;
    stats.shown += shown_count_;
    stats.late += late_;
    if (decoder_) decoder_->add_stats(stats);
    if (cooked_) cooked_->add_stats(stats);
    return stats;
}

}  // namespace galaxy::render
