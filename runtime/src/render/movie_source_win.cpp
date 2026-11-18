// SPDX-License-Identifier: GPL-3.0-only
// A replacement movie on Windows (movie_source.h): Media Foundation's source reader, on the GPU's video decoder where
// it has one for the format (through a D3D11 device), else in software. NV12, the decoders' own output, where a
// decoder gives it; RGB32 (Media Foundation's colour conversion) where none does.

#include "movie_source.h"

#include "galaxy/windows_api.h"

// d3d11.h first: it includes d3d10_1.h, which must come before d3d10.h.
#include <d3d11.h>
#include <d3d10.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace galaxy::render {
namespace {

using Microsoft::WRL::ComPtr;

// Media Foundation is delay-loaded: on a Windows edition without it the first call into it would raise the loader's
// exception instead of failing, so everything that calls it asks here first (its two libraries, loaded once).
bool media_foundation_present() {
    static const bool present = LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) != nullptr &&
                                LoadLibraryExW(L"mfreadwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) != nullptr;
    return present;
}

// The GPU's video decoder for every movie: Media Foundation uses it only through a D3D device manager. Made once
// (prepare_movie_sources makes it ahead of the first movie); null where the GPU has no D3D11 video device (the movies
// decode in software).
ComPtr<IMFDXGIDeviceManager> video_device_manager() {
    static std::once_flag once;
    static ComPtr<ID3D11Device> device;
    static ComPtr<IMFDXGIDeviceManager> manager;
    std::call_once(once, [] {
        if (!media_foundation_present()) return;
        UINT reset_token = 0;
        ComPtr<IMFDXGIDeviceManager> made;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                     D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                     D3D11_SDK_VERSION, &device, nullptr, nullptr)) ||
            FAILED(MFCreateDXGIDeviceManager(&reset_token, &made)) || FAILED(made->ResetDevice(device.Get(), reset_token))) {
            device.Reset();
            return;
        }
        ComPtr<ID3D10Multithread> multithread;
        if (SUCCEEDED(device.As(&multithread))) multithread->SetMultithreadProtected(TRUE);
        manager = made;
    });
    return manager;
}

std::string hresult_text(const char* what, HRESULT hr) {
    char text[128];
    std::snprintf(text, sizeof(text), "%s (0x%08x)", what, static_cast<unsigned>(hr));
    return text;
}

class WindowsSource final : public MovieSource {
public:
    ~WindowsSource() override {
        release();
        reader_.Reset();
        if (mf_) MFShutdown();
        if (SUCCEEDED(com_)) CoUninitialize();
    }

    // On the thread that reads it (COM is joined per thread).
    bool open(const std::filesystem::path& file) {
        com_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (!media_foundation_present() || FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
            error_ = "Media Foundation is unavailable";
            return false;
        }
        mf_ = true;
        ComPtr<IMFAttributes> attributes;
        HRESULT hr = MFCreateAttributes(&attributes, 3);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        // Decoded on the GPU and only copied back where it can, else in software.
        if (const ComPtr<IMFDXGIDeviceManager> manager = video_device_manager(); SUCCEEDED(hr) && manager) {
            attributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, manager.Get());
        }
        if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromURL(file.c_str(), attributes.Get(), &reader_);
        if (FAILED(hr)) return fail("cannot open", hr);
        reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
        reader_->SetStreamSelection(kVideo, TRUE);
        for (const GUID& subtype : {MFVideoFormat_NV12, MFVideoFormat_RGB32}) {
            ComPtr<IMFMediaType> type;
            hr = MFCreateMediaType(&type);
            if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_SUBTYPE, subtype);
            if (SUCCEEDED(hr)) hr = reader_->SetCurrentMediaType(kVideo, nullptr, type.Get());
            if (SUCCEEDED(hr)) {
                format_.pixels = subtype == MFVideoFormat_NV12 ? MoviePixels::Yuv420 : MoviePixels::Bgra;
                break;
            }
        }
        if (FAILED(hr)) return fail("no decoder for its video", hr);
        if (!read_format()) {
            error_ = "no picture size";
            return false;
        }
        return true;
    }

    const MovieSourceFormat& format() const override { return format_; }

    Read read(MovieSourcePicture& out) override {
        release();
        for (;;) {
            DWORD stream = 0;
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;
            const HRESULT hr = reader_->ReadSample(kVideo, 0, &stream, &flags, &timestamp, &sample);
            if (FAILED(hr)) {
                error_ = hresult_text("decoding stopped", hr);
                return Read::Failed;
            }
            if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0u) return Read::End;
            if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0u && !read_format()) {
                error_ = "no picture size after a format change";
                return Read::Failed;
            }
            if (!sample || FAILED(sample->ConvertToContiguousBuffer(&media_))) continue;
            BYTE* scan0 = nullptr;
            LONG pitch = 0;
            if (SUCCEEDED(media_.As(&planar_)) && SUCCEEDED(planar_->Lock2D(&scan0, &pitch))) {
                locked2d_ = true;
            } else {
                planar_.Reset();
                BYTE* data = nullptr;
                DWORD length = 0;
                if (FAILED(media_->Lock(&data, nullptr, &length))) {
                    media_.Reset();
                    continue;
                }
                locked_ = true;
                pitch = stride_;
                scan0 = stride_ < 0 ? data + static_cast<std::ptrdiff_t>(coded_height_ - 1u) * -stride_ : data;
                const bool yuv = format_.pixels == MoviePixels::Yuv420;
                const std::uint64_t needed =
                    static_cast<std::uint64_t>(std::abs(stride_)) * coded_height_ * (yuv ? 3u : 2u) / 2u;
                if (needed > length) {
                    release();
                    continue;
                }
            }
            out.time = timestamp;
            if (format_.pixels == MoviePixels::Yuv420) {
                // NV12: the interleaved chroma follows the coded rows.
                const BYTE* const chroma = scan0 + static_cast<std::ptrdiff_t>(coded_height_) * pitch +
                                           static_cast<std::ptrdiff_t>(top_ / 2u) * pitch + (left_ & ~1u);
                out.planes = {scan0 + static_cast<std::ptrdiff_t>(top_) * pitch + left_, chroma, chroma + 1};
                out.pitches = {pitch, pitch, pitch};
                out.chroma_step = 2u;
            } else {
                out.planes = {scan0 + static_cast<std::ptrdiff_t>(top_) * pitch + left_ * 4u, nullptr, nullptr};
                out.pitches = {pitch, 0, 0};
                out.chroma_step = 1u;
            }
            return Read::Picture;
        }
    }

    bool seek(std::int64_t time) override {
        release();
        PROPVARIANT position;
        PropVariantInit(&position);
        position.vt = VT_I8;
        position.hVal.QuadPart = time;
        const HRESULT hr = reader_->SetCurrentPosition(GUID_NULL, position);
        PropVariantClear(&position);
        return SUCCEEDED(hr);
    }

private:
    static constexpr DWORD kVideo = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

    bool fail(const char* what, HRESULT hr) {
        error_ = hresult_text(what, hr);
        return false;
    }

    void release() {
        if (locked2d_) planar_->Unlock2D();
        if (locked_) media_->Unlock();
        locked2d_ = locked_ = false;
        planar_.Reset();
        media_.Reset();
    }

    // The current media type: the coded size, the picture shown (the display aperture), the stride, the colours and
    // the frame rate.
    bool read_format() {
        ComPtr<IMFMediaType> current;
        if (FAILED(reader_->GetCurrentMediaType(kVideo, &current))) return false;
        UINT32 coded_width = 0;
        if (FAILED(MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &coded_width, &coded_height_)) ||
            coded_width == 0u || coded_height_ == 0u) {
            return false;
        }
        UINT32 width = coded_width;
        UINT32 height = coded_height_;
        left_ = top_ = 0;
        MFVideoArea area{};
        if (SUCCEEDED(current->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof(area),
                                       nullptr)) &&
            area.Area.cx > 0 && area.Area.cy > 0) {
            left_ = static_cast<UINT32>(std::max<SHORT>(0, area.OffsetX.value));
            top_ = static_cast<UINT32>(std::max<SHORT>(0, area.OffsetY.value));
            width = std::min<UINT32>(static_cast<UINT32>(area.Area.cx), coded_width - std::min(left_, coded_width));
            height = std::min<UINT32>(static_cast<UINT32>(area.Area.cy), coded_height_ - std::min(top_, coded_height_));
        }
        const bool yuv = format_.pixels == MoviePixels::Yuv420;
        UINT32 default_stride = 0;
        stride_ = SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &default_stride))
                      ? static_cast<LONG>(static_cast<INT32>(default_stride))
                      : static_cast<LONG>(yuv ? coded_width : coded_width * 4u);
        // The source's colours: its matrix and range, else HD BT.709 / SD BT.601, limited range.
        UINT32 matrix = MFVideoTransferMatrix_Unknown;
        UINT32 range = MFNominalRange_Unknown;
        current->GetUINT32(MF_MT_YUV_MATRIX, &matrix);
        current->GetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, &range);
        format_.bt709 =
            matrix == MFVideoTransferMatrix_BT709 || (matrix != MFVideoTransferMatrix_BT601 && coded_height_ >= 720u);
        format_.full_range = range == MFNominalRange_0_255;
        UINT32 numerator = 0;
        UINT32 denominator = 0;
        format_.rate = SUCCEEDED(MFGetAttributeRatio(current.Get(), MF_MT_FRAME_RATE, &numerator, &denominator)) &&
                               numerator != 0u && denominator != 0u
                           ? static_cast<double>(numerator) / static_cast<double>(denominator)
                           : 0.0;
        format_.width = width;
        format_.height = height;
        return width >= 2u && height >= 2u;
    }

    HRESULT com_ = E_FAIL;
    bool mf_ = false;
    ComPtr<IMFSourceReader> reader_;
    ComPtr<IMFMediaBuffer> media_;  // the last read's picture, locked
    ComPtr<IMF2DBuffer> planar_;
    bool locked2d_ = false;
    bool locked_ = false;
    UINT32 coded_height_ = 0;
    UINT32 left_ = 0;
    UINT32 top_ = 0;
    LONG stride_ = 0;
    MovieSourceFormat format_;
};

}  // namespace

std::unique_ptr<MovieSource> open_movie_source(const std::filesystem::path& file, std::string& error) {
    auto source = std::make_unique<WindowsSource>();
    if (!source->open(file)) {
        error = source->error();
        return nullptr;
    }
    return source;
}

void prepare_movie_sources() {
    (void)video_device_manager();
}

}  // namespace galaxy::render
