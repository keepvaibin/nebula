// SPDX-License-Identifier: GPL-3.0-only
// Records a window as Windows composes it (Windows.Graphics.Capture): every frame the window presents, generated
// frames included, with the system time of each. The hidden test runs show no frames to Windows; with
// GALAXY_TEST_OFFSCREEN_WINDOW=1 the test window sits off every monitor and this records what a player would see.
//
//   wgc_capture --exe <path substring> --out <file> [--wait <s>] [--delay <s>] [--seconds <s>] [--scale <n>]
//
// The output is one file of frames: per frame a u64 system-relative time (100 ns), u32 width, u32 height, then the
// pixels as RGB, every `scale`-th pixel of every `scale`-th row. tools/wgc_capture/frames.py reads it.
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <inspectable.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wdx = winrt::Windows::Graphics::DirectX;

namespace {

struct Options {
    std::wstring exe;
    std::wstring out;
    double wait = 60.0;
    double delay = 0.0;
    double seconds = 10.0;
    int scale = 1;
};

struct Search {
    const std::wstring* exe;
    HWND found;
};

BOOL CALLBACK find_window(HWND window, LPARAM param) {
    Search* search = reinterpret_cast<Search*>(param);
    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr) {
        return TRUE;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return TRUE;
    }
    wchar_t path[MAX_PATH * 2];
    DWORD size = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &size) != 0;
    CloseHandle(process);
    if (ok) {
        std::wstring lower(path);
        std::wstring want(*search->exe);
        for (wchar_t& c : lower) c = static_cast<wchar_t>(towlower(c));
        for (wchar_t& c : want) c = static_cast<wchar_t>(towlower(c));
        if (lower.find(want) != std::wstring::npos) {
            search->found = window;
            return FALSE;
        }
    }
    return TRUE;
}

bool parse(int argc, wchar_t** argv, Options& o) {
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::wstring key = argv[i];
        const wchar_t* value = argv[i + 1];
        if (key == L"--exe") o.exe = value;
        else if (key == L"--out") o.out = value;
        else if (key == L"--wait") o.wait = _wtof(value);
        else if (key == L"--delay") o.delay = _wtof(value);
        else if (key == L"--seconds") o.seconds = _wtof(value);
        else if (key == L"--scale") o.scale = (std::max)(1, _wtoi(value));
        else return false;
    }
    return !o.exe.empty() && !o.out.empty();
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    Options options;
    if (!parse(argc, argv, options)) {
        std::fprintf(stderr, "usage: wgc_capture --exe <path substring> --out <file> [--wait s] [--delay s] [--seconds s] [--scale n]\n");
        return 2;
    }
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    if (!wgc::GraphicsCaptureSession::IsSupported()) {
        std::fprintf(stderr, "capture: not supported here\n");
        return 3;
    }

    Search search{&options.exe, nullptr};
    const auto started = std::chrono::steady_clock::now();
    while (search.found == nullptr) {
        EnumWindows(find_window, reinterpret_cast<LPARAM>(&search));
        if (search.found != nullptr) break;
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() > options.wait) {
            std::fprintf(stderr, "capture: no window of %ls\n", options.exe.c_str());
            return 4;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::this_thread::sleep_for(std::chrono::duration<double>(options.delay));

    winrt::com_ptr<ID3D11Device> device;
    winrt::com_ptr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                 D3D11_SDK_VERSION, device.put(), nullptr, context.put()))) {
        std::fprintf(stderr, "capture: no D3D11 device\n");
        return 5;
    }
    winrt::com_ptr<IDXGIDevice> dxgi = device.as<IDXGIDevice>();
    winrt::com_ptr<::IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put()));
    const wdx::Direct3D11::IDirect3DDevice winrt_device = inspectable.as<wdx::Direct3D11::IDirect3DDevice>();

    auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    wgc::GraphicsCaptureItem item{nullptr};
    winrt::check_hresult(interop->CreateForWindow(search.found, winrt::guid_of<wgc::GraphicsCaptureItem>(),
                                                  winrt::put_abi(item)));
    const auto size = item.Size();
    auto pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
        winrt_device, wdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 4, size);
    auto session = pool.CreateCaptureSession(item);
    try {
        session.IsCursorCaptureEnabled(false);
        session.IsBorderRequired(false);
    } catch (...) {
    }
    // Every composed frame, not Windows' default of 60 a second (Windows 11 24H2 and later).
    try {
        session.MinUpdateInterval(winrt::Windows::Foundation::TimeSpan{std::chrono::milliseconds(1)});
    } catch (...) {
        std::fprintf(stderr, "capture: this Windows caps captures at its own rate\n");
    }

    std::FILE* file = nullptr;
    if (_wfopen_s(&file, options.out.c_str(), L"wb") != 0 || file == nullptr) {
        std::fprintf(stderr, "capture: cannot write %ls\n", options.out.c_str());
        return 6;
    }
    std::mutex lock;
    std::atomic<unsigned> frames{0};
    winrt::com_ptr<ID3D11Texture2D> staging;
    std::vector<unsigned char> row;
    pool.FrameArrived([&](const wgc::Direct3D11CaptureFramePool& sender, const winrt::Windows::Foundation::IInspectable&) {
        const wgc::Direct3D11CaptureFrame frame = sender.TryGetNextFrame();
        if (!frame) return;
        std::lock_guard<std::mutex> guard(lock);
        auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::com_ptr<ID3D11Texture2D> texture;
        winrt::check_hresult(access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), texture.put_void()));
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        D3D11_TEXTURE2D_DESC sdesc{};
        if (staging) staging->GetDesc(&sdesc);
        if (!staging || sdesc.Width != desc.Width || sdesc.Height != desc.Height) {
            staging = nullptr;
            D3D11_TEXTURE2D_DESC want = desc;
            want.Usage = D3D11_USAGE_STAGING;
            want.BindFlags = 0;
            want.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            want.MiscFlags = 0;
            winrt::check_hresult(device->CreateTexture2D(&want, nullptr, staging.put()));
        }
        context->CopyResource(staging.get(), texture.get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped))) return;
        const unsigned w = desc.Width / options.scale;
        const unsigned h = desc.Height / options.scale;
        const unsigned long long time = static_cast<unsigned long long>(frame.SystemRelativeTime().count());
        std::fwrite(&time, sizeof(time), 1, file);
        std::fwrite(&w, sizeof(w), 1, file);
        std::fwrite(&h, sizeof(h), 1, file);
        row.resize(static_cast<size_t>(w) * 3u);
        for (unsigned y = 0; y < h; ++y) {
            const unsigned char* src = static_cast<const unsigned char*>(mapped.pData) +
                                       static_cast<size_t>(y) * options.scale * mapped.RowPitch;
            for (unsigned x = 0; x < w; ++x) {
                const unsigned char* p = src + static_cast<size_t>(x) * options.scale * 4u;
                row[x * 3u + 0u] = p[2];
                row[x * 3u + 1u] = p[1];
                row[x * 3u + 2u] = p[0];
            }
            std::fwrite(row.data(), 1, row.size(), file);
        }
        context->Unmap(staging.get(), 0);
        ++frames;
    });
    session.StartCapture();
    std::this_thread::sleep_for(std::chrono::duration<double>(options.seconds));
    session.Close();
    pool.Close();
    std::lock_guard<std::mutex> guard(lock);
    std::fclose(file);
    std::printf("capture: %u frames of %dx%d in %.1f s\n", frames.load(), size.Width, size.Height, options.seconds);
    return 0;
}
