// SPDX-License-Identifier: GPL-3.0-only
// Writes the movie source test's fixture (tests/data/movie/h264_64x64.mp4, runtime/src/render/movie_source_tests.cpp):
// 64x64 H.264 (Baseline) in MP4, 30 frames at 30 fps, frames 0-14 red and 15-29 blue, a key frame every 15, with Media
// Foundation's H.264 encoder. Built by hand on Windows (cl /EHsc /std:c++20 make_movie_fixture.cpp); usage:
// make_movie_fixture <out.mp4> [width height] (another even size: the same movie for a device run, see the test).
#include <windows.h>
#include <codecapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

#define CHECK(x)                                                                         \
    do {                                                                                 \
        const HRESULT hr_ = (x);                                                         \
        if (FAILED(hr_)) {                                                               \
            std::fprintf(stderr, "%s failed: 0x%08lx\n", #x, static_cast<unsigned long>(hr_)); \
            return 1;                                                                    \
        }                                                                                \
    } while (0)

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 && argc != 4) return 2;
    const UINT32 kWidth = argc == 4 ? static_cast<UINT32>(std::wcstoul(argv[2], nullptr, 10)) : 64u;
    const UINT32 kHeight = argc == 4 ? static_cast<UINT32>(std::wcstoul(argv[3], nullptr, 10)) : 64u;
    if (kWidth < 2u || kHeight < 2u || kWidth % 2u != 0u || kHeight % 2u != 0u) return 2;
    constexpr UINT32 kRate = 30, kFrames = 30;
    CHECK(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    CHECK(MFStartup(MF_VERSION));
    ComPtr<IMFAttributes> attributes;
    CHECK(MFCreateAttributes(&attributes, 1));
    CHECK(attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4));
    ComPtr<IMFSinkWriter> writer;
    CHECK(MFCreateSinkWriterFromURL(argv[1], nullptr, attributes.Get(), &writer));
    ComPtr<IMFMediaType> out;
    CHECK(MFCreateMediaType(&out));
    CHECK(out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    CHECK(out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
    CHECK(out->SetUINT32(MF_MT_AVG_BITRATE, 200000));
    CHECK(out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    CHECK(out->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Base));
    CHECK(MFSetAttributeSize(out.Get(), MF_MT_FRAME_SIZE, kWidth, kHeight));
    CHECK(MFSetAttributeRatio(out.Get(), MF_MT_FRAME_RATE, kRate, 1));
    CHECK(MFSetAttributeRatio(out.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
    DWORD stream = 0;
    CHECK(writer->AddStream(out.Get(), &stream));
    ComPtr<IMFMediaType> in;
    CHECK(MFCreateMediaType(&in));
    CHECK(in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    CHECK(in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12));
    CHECK(in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    CHECK(MFSetAttributeSize(in.Get(), MF_MT_FRAME_SIZE, kWidth, kHeight));
    CHECK(MFSetAttributeRatio(in.Get(), MF_MT_FRAME_RATE, kRate, 1));
    CHECK(MFSetAttributeRatio(in.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
    // A key frame every 15: a seek to frame 15 lands on one. No B-frames (Baseline): each picture shows in the order
    // it is stored, inside the movie's duration (with them the writer shifts every picture a frame later and the last
    // one past the duration it writes, which GStreamer honours, dropping that picture).
    ComPtr<IMFAttributes> encoder;
    CHECK(MFCreateAttributes(&encoder, 1));
    CHECK(encoder->SetUINT32(CODECAPI_AVEncMPVGOPSize, 15));
    CHECK(encoder->SetUINT32(CODECAPI_AVEncMPVDefaultBPictureCount, 0));
    CHECK(writer->SetInputMediaType(stream, in.Get(), encoder.Get()));
    CHECK(writer->BeginWriting());
    // BT.601 limited range: red Y 81 Cb 90 Cr 240; blue Y 41 Cb 240 Cr 110.
    for (UINT32 f = 0; f < kFrames; ++f) {
        const bool red = f < 15u;
        const BYTE y = red ? 81 : 41, cb = red ? 90 : 240, cr = red ? 240 : 110;
        const DWORD size = kWidth * kHeight * 3u / 2u;
        ComPtr<IMFMediaBuffer> buffer;
        CHECK(MFCreateMemoryBuffer(size, &buffer));
        BYTE* data = nullptr;
        CHECK(buffer->Lock(&data, nullptr, nullptr));
        std::memset(data, y, kWidth * kHeight);
        for (UINT32 i = 0; i < kWidth * kHeight / 2u; i += 2u) {
            data[kWidth * kHeight + i] = cb;
            data[kWidth * kHeight + i + 1u] = cr;
        }
        CHECK(buffer->Unlock());
        CHECK(buffer->SetCurrentLength(size));
        ComPtr<IMFSample> sample;
        CHECK(MFCreateSample(&sample));
        CHECK(sample->AddBuffer(buffer.Get()));
        CHECK(sample->SetSampleTime(static_cast<LONGLONG>(f) * 10'000'000 / kRate));
        CHECK(sample->SetSampleDuration(10'000'000 / kRate));
        CHECK(writer->WriteSample(stream, sample.Get()));
    }
    CHECK(writer->Finalize());
    writer.Reset();
    MFShutdown();
    CoUninitialize();
    std::printf("written\n");
    return 0;
}
