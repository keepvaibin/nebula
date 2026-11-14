// SPDX-License-Identifier: GPL-3.0-only
// LumaCore's system services on Windows (docs/modding/services/system.md): the facts, the display snapshot from the
// game window's procedure, the clipboard through one worker thread that owns a message-only window, and links through
// the shell on the same thread. The worker starts at the first clipboard or link request (no cost with no mod).

#include "galaxy/luma_system_platform.h"

#include "galaxy/windows_api.h"

#include <objbase.h>
#include <shellapi.h>

#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <deque>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace galaxy::luma_system {
namespace {

constexpr std::uint64_t kTextMost = 16ull << 20u;       // services/system.md 1 item 5
constexpr std::uint32_t kSideMost = 8192u;              // item 4
constexpr std::uint64_t kPixelsMost = 64ull << 20u;
constexpr std::uint64_t kEncodedMost = 80ull << 20u;    // a PNG of a picture within the limits, with room for its headers
constexpr int kOpenTries = 25;                          // OpenClipboard for 250 ms, 10 ms apart (section 5)

// ---- Facts (section 3), read once ------------------------------------------------------------------------------

std::once_flag g_facts_once;
LumaSystemFacts g_facts{};

void read_facts() noexcept {
    g_facts.platform = LUMA_SYSTEM_WINDOWS;
#if defined(_M_ARM64) || defined(__aarch64__)
    g_facts.arch = LUMA_SYSTEM_ARM64;
#else
    g_facts.arch = LUMA_SYSTEM_X64;
#endif
    g_facts.renderer = LUMA_SYSTEM_D3D12;  // the D3D12 build's; the Vulkan build's renderer says so (set_renderer)
    g_facts.clipboard_images = 1u;
    wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
    if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) > 0) {
        // BCP 47 already ("en-US", "zh-Hant-TW"), ASCII.
        const int written = WideCharToMultiByte(CP_UTF8, 0, locale, -1, g_facts.locale, sizeof(g_facts.locale), nullptr, nullptr);
        if (written <= 0) {
            g_facts.locale[0] = 0;
        }
    }
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        const auto get_version = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
        OSVERSIONINFOW version{};
        version.dwOSVersionInfoSize = sizeof(version);
        if (get_version != nullptr && get_version(&version) == 0) {
            if (version.dwMajorVersion == 10u) {
                std::snprintf(g_facts.os, sizeof(g_facts.os), "Windows %s (%lu)", version.dwBuildNumber >= 22000u ? "11" : "10",
                              static_cast<unsigned long>(version.dwBuildNumber));
            } else {
                std::snprintf(g_facts.os, sizeof(g_facts.os), "Windows %lu.%lu (%lu)", static_cast<unsigned long>(version.dwMajorVersion),
                              static_cast<unsigned long>(version.dwMinorVersion), static_cast<unsigned long>(version.dwBuildNumber));
            }
        }
    }
}

void facts(LumaSystemFacts& out) noexcept {
    try {
        std::call_once(g_facts_once, read_facts);
    } catch (...) {
        return;
    }
    out.platform = g_facts.platform;
    out.arch = g_facts.arch;
    out.renderer = g_facts.renderer;
    out.clipboard_images = g_facts.clipboard_images;
    std::memcpy(out.locale, g_facts.locale, sizeof(out.locale));
    std::memcpy(out.os, g_facts.os, sizeof(out.os));
}

// ---- The worker ----------------------------------------------------------------------------------------------------

std::mutex g_jobs_lock;
std::deque<std::function<void()>> g_jobs;
HANDLE g_wake = nullptr;  // g_jobs_lock until the thread starts, then fixed
bool g_started = false;   // g_jobs_lock
HWND g_owner = nullptr;   // the worker's: the message-only window that owns what the game puts on the clipboard

void worker_main() {
    // The shell's links need COM on the calling thread (ShellExecuteW).
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    (void)com;
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"NebulaLumaClipboard";
    RegisterClassExW(&window_class);
    g_owner = CreateWindowExW(0, window_class.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, window_class.hInstance,
                              nullptr);
    for (;;) {
        // Messages are pumped while waiting: another program that empties the clipboard sends the owner
        // WM_DESTROYCLIPBOARD and waits for the answer.
        (void)MsgWaitForMultipleObjectsEx(1, &g_wake, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        std::deque<std::function<void()>> now;
        {
            const std::lock_guard<std::mutex> lock(g_jobs_lock);
            now.swap(g_jobs);
        }
        for (std::function<void()>& job : now) {
            job();
        }
    }
}

// Queues `job` for the worker, started at the first. False: no thread or no memory (the caller answers).
bool post_job(std::function<void()> job) {
    try {
        const std::lock_guard<std::mutex> lock(g_jobs_lock);
        if (!g_started) {
            g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (g_wake == nullptr) {
                return false;
            }
            std::thread(worker_main).detach();
            g_started = true;
        }
        g_jobs.push_back(std::move(job));
        SetEvent(g_wake);
        return true;
    } catch (...) {
        return false;
    }
}

bool open_clipboard() {
    for (int i = 0; i < kOpenTries; i++) {
        if (OpenClipboard(g_owner)) {
            return true;
        }
        Sleep(10);
    }
    return false;
}

// Closes the clipboard when it goes out of scope.
struct OpenedClipboard {
    bool opened = open_clipboard();
    ~OpenedClipboard() {
        if (opened) {
            CloseClipboard();
        }
    }
};

// A locked global memory block's bytes.
struct Locked {
    HANDLE handle;
    const std::uint8_t* bytes;
    SIZE_T size;
    explicit Locked(HANDLE h) : handle(h), bytes(static_cast<const std::uint8_t*>(GlobalLock(h))), size(GlobalSize(h)) {}
    ~Locked() {
        if (bytes != nullptr) {
            GlobalUnlock(handle);
        }
    }
};

// A global memory block holding `size` bytes from `fill`, or null.
template <typename Fill>
HGLOBAL global_block(std::size_t size, Fill fill) {
    HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, size);
    if (block == nullptr) {
        return nullptr;
    }
    void* bytes = GlobalLock(block);
    if (bytes == nullptr) {
        GlobalFree(block);
        return nullptr;
    }
    fill(static_cast<std::uint8_t*>(bytes));
    GlobalUnlock(block);
    return block;
}

// ---- Text ----------------------------------------------------------------------------------------------------------

void read_text(std::function<void(std::int32_t, std::string)> done) {
    auto job = [done]() mutable {
        try {
            OpenedClipboard clipboard;
            if (!clipboard.opened) {
                done(LUMA_E_IO, {});
                return;
            }
            HANDLE data = GetClipboardData(CF_UNICODETEXT);
            if (data == nullptr) {
                done(LUMA_E_NOT_FOUND, {});
                return;
            }
            const Locked locked(data);
            if (locked.bytes == nullptr) {
                done(LUMA_E_IO, {});
                return;
            }
            const auto* wide = reinterpret_cast<const wchar_t*>(locked.bytes);
            const std::size_t length = wcsnlen(wide, locked.size / sizeof(wchar_t));
            if (length == 0u) {
                done(LUMA_E_NOT_FOUND, {});
                return;
            }
            if (length > static_cast<std::size_t>(INT_MAX)) {
                done(LUMA_E_TOO_LARGE, {});
                return;
            }
            // Unpaired surrogates become U+FFFD: the text is always UTF-8.
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
            if (bytes <= 0) {
                done(LUMA_E_IO, {});
                return;
            }
            if (static_cast<std::uint64_t>(bytes) > kTextMost) {
                done(LUMA_E_TOO_LARGE, {});
                return;
            }
            std::string text(static_cast<std::size_t>(bytes), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(length), text.data(), bytes, nullptr, nullptr);
            done(LUMA_OK, std::move(text));
        } catch (...) {
            done(LUMA_E_MEMORY, {});
        }
    };
    if (!post_job(job)) {
        done(LUMA_E_IO, {});
    }
}

void write_text(std::string text, std::function<void(std::int32_t)> done) {
    auto job = [text = std::move(text), done]() {
        try {
            int wide_length = 0;
            if (!text.empty()) {
                wide_length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
                if (wide_length <= 0) {
                    done(LUMA_E_IO);
                    return;
                }
            }
            const std::size_t bytes = (static_cast<std::size_t>(wide_length) + 1u) * sizeof(wchar_t);
            HGLOBAL block = global_block(bytes, [&](std::uint8_t* out) {
                auto* wide = reinterpret_cast<wchar_t*>(out);
                if (wide_length > 0) {
                    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide, wide_length);
                }
                wide[wide_length] = L'\0';
            });
            if (block == nullptr) {
                done(LUMA_E_MEMORY);
                return;
            }
            OpenedClipboard clipboard;
            if (!clipboard.opened || !EmptyClipboard() || SetClipboardData(CF_UNICODETEXT, block) == nullptr) {
                GlobalFree(block);
                done(LUMA_E_IO);
                return;
            }
            done(LUMA_OK);  // the clipboard owns the block now
        } catch (...) {
            done(LUMA_E_MEMORY);
        }
    };
    if (!post_job(std::move(job))) {
        done(LUMA_E_IO);
    }
}

// ---- Images --------------------------------------------------------------------------------------------------------

// The shift and width of a channel's mask. False: not 8 contiguous bits (or, for alpha, not 0 or 8).
bool mask_shift(DWORD mask, bool may_be_empty, unsigned& shift) {
    if (mask == 0u) {
        shift = 0;
        return may_be_empty;
    }
    shift = 0;
    while ((mask & 1u) == 0u) {
        mask >>= 1u;
        shift++;
    }
    return mask == 0xFFu;
}

// A device-independent bitmap (CF_DIBV5 or CF_DIB) as RGBA: 24 or 32 bits, BI_RGB or BI_BITFIELDS, either row order.
std::int32_t read_dib(const std::uint8_t* bytes, std::size_t size, Image& out) {
    BITMAPINFOHEADER header{};
    if (size < sizeof(header)) {
        return LUMA_E_FORMAT;
    }
    std::memcpy(&header, bytes, sizeof(header));
    if (header.biSize < sizeof(BITMAPINFOHEADER) || header.biSize > size || header.biWidth <= 0 || header.biHeight == 0 ||
        header.biHeight == LONG_MIN || (header.biBitCount != 24u && header.biBitCount != 32u)) {
        return LUMA_E_FORMAT;
    }
    const bool top_down = header.biHeight < 0;
    const std::uint32_t width = static_cast<std::uint32_t>(header.biWidth);
    const std::uint32_t height = static_cast<std::uint32_t>(top_down ? -header.biHeight : header.biHeight);
    if (width > kSideMost || height > kSideMost || std::uint64_t{width} * height * 4u > kPixelsMost) {
        return LUMA_E_TOO_LARGE;
    }
    std::size_t offset = header.biSize;
    DWORD red = 0x00FF0000u;
    DWORD green = 0x0000FF00u;
    DWORD blue = 0x000000FFu;
    DWORD alpha = 0u;
    const bool v4 = header.biSize >= sizeof(BITMAPV4HEADER);
    if (header.biCompression == BI_BITFIELDS) {
        if (header.biBitCount != 32u) {
            return LUMA_E_FORMAT;
        }
        DWORD masks[3]{};
        if (v4) {
            std::memcpy(masks, bytes + offsetof(BITMAPV4HEADER, bV4RedMask), sizeof(masks));
        } else {
            if (size < offset + sizeof(masks)) {
                return LUMA_E_FORMAT;
            }
            std::memcpy(masks, bytes + offset, sizeof(masks));
            offset += sizeof(masks);
        }
        red = masks[0];
        green = masks[1];
        blue = masks[2];
    } else if (header.biCompression != BI_RGB) {
        return LUMA_E_FORMAT;
    }
    if (v4 && header.biBitCount == 32u) {
        // The alpha mask of a V4 or V5 header (CF_DIBV5): alpha when it names one, else opaque.
        std::memcpy(&alpha, bytes + offsetof(BITMAPV4HEADER, bV4AlphaMask), sizeof(alpha));
    }
    offset += std::size_t{header.biClrUsed} * 4u;  // a colour table a writer may add even above 8 bits
    unsigned red_shift = 0;
    unsigned green_shift = 0;
    unsigned blue_shift = 0;
    unsigned alpha_shift = 0;
    if (!mask_shift(red, false, red_shift) || !mask_shift(green, false, green_shift) || !mask_shift(blue, false, blue_shift) ||
        !mask_shift(alpha, true, alpha_shift)) {
        return LUMA_E_FORMAT;
    }
    const std::size_t stride = ((std::size_t{width} * header.biBitCount + 31u) / 32u) * 4u;
    if (offset > size || stride * height > size - offset) {
        return LUMA_E_FORMAT;
    }
    out.width = width;
    out.height = height;
    out.rgba.resize(std::size_t{width} * height * 4u);
    bool any_alpha = false;
    for (std::uint32_t y = 0; y < height; y++) {
        const std::uint8_t* row = bytes + offset + stride * (top_down ? y : height - 1u - y);
        std::uint8_t* to = out.rgba.data() + std::size_t{y} * width * 4u;
        for (std::uint32_t x = 0; x < width; x++, to += 4) {
            if (header.biBitCount == 24u) {
                const std::uint8_t* pixel = row + std::size_t{x} * 3u;
                to[0] = pixel[2];
                to[1] = pixel[1];
                to[2] = pixel[0];
                to[3] = 255u;
                continue;
            }
            DWORD pixel = 0;
            std::memcpy(&pixel, row + std::size_t{x} * 4u, sizeof(pixel));
            to[0] = static_cast<std::uint8_t>((pixel & red) >> red_shift);
            to[1] = static_cast<std::uint8_t>((pixel & green) >> green_shift);
            to[2] = static_cast<std::uint8_t>((pixel & blue) >> blue_shift);
            to[3] = alpha != 0u ? static_cast<std::uint8_t>((pixel & alpha) >> alpha_shift) : 255u;
            any_alpha = any_alpha || to[3] != 0u;
        }
    }
    if (alpha != 0u && !any_alpha) {
        // An alpha mask over an alpha channel left at 0 (programs that copy opaque pictures this way): opaque.
        for (std::size_t i = 3; i < out.rgba.size(); i += 4) {
            out.rgba[i] = 255u;
        }
    }
    return LUMA_OK;
}

UINT png_format() {
    return RegisterClipboardFormatW(L"PNG");
}

void read_image(std::function<void(std::int32_t, Image)> done) {
    auto job = [done]() mutable {
        try {
            OpenedClipboard clipboard;
            if (!clipboard.opened) {
                done(LUMA_E_IO, {});
                return;
            }
            Image image;
            const UINT png = png_format();
            if (png != 0u && IsClipboardFormatAvailable(png)) {
                HANDLE data = GetClipboardData(png);
                const Locked locked(data);
                if (data == nullptr || locked.bytes == nullptr) {
                    done(LUMA_E_IO, {});
                    return;
                }
                if (locked.size > kEncodedMost) {
                    done(LUMA_E_TOO_LARGE, {});
                    return;
                }
                image.encoded.assign(locked.bytes, locked.bytes + locked.size);
                done(LUMA_OK, std::move(image));  // decoded by the service, on this thread
                return;
            }
            // Windows gives CF_DIBV5 for a CF_DIB or a CF_BITMAP too.
            const UINT format = IsClipboardFormatAvailable(CF_DIBV5) ? CF_DIBV5 : IsClipboardFormatAvailable(CF_DIB) ? CF_DIB : 0u;
            if (format == 0u) {
                done(LUMA_E_NOT_FOUND, {});
                return;
            }
            HANDLE data = GetClipboardData(format);
            const Locked locked(data);
            if (data == nullptr || locked.bytes == nullptr) {
                done(LUMA_E_IO, {});
                return;
            }
            const std::int32_t status = read_dib(locked.bytes, locked.size, image);
            done(status, status == LUMA_OK ? std::move(image) : Image{});
        } catch (...) {
            done(LUMA_E_MEMORY, {});
        }
    };
    if (!post_job(job)) {
        done(LUMA_E_IO, {});
    }
}

void write_image(Image image, std::function<void(std::int32_t)> done) {
    auto job = [image = std::move(image), done]() mutable {
        try {
            if (!encode_png(image)) {
                done(LUMA_E_MEMORY);
                return;
            }
            HGLOBAL png = global_block(image.encoded.size(),
                                       [&](std::uint8_t* out) { std::memcpy(out, image.encoded.data(), image.encoded.size()); });
            // A bottom-up 32-bit BGRA DIBV5 with an alpha mask: what programs that read no PNG paste.
            const std::size_t pixels = std::size_t{image.width} * image.height * 4u;
            HGLOBAL dib = global_block(sizeof(BITMAPV5HEADER) + pixels, [&](std::uint8_t* out) {
                BITMAPV5HEADER header{};
                header.bV5Size = sizeof(header);
                header.bV5Width = static_cast<LONG>(image.width);
                header.bV5Height = static_cast<LONG>(image.height);
                header.bV5Planes = 1;
                header.bV5BitCount = 32;
                header.bV5Compression = BI_BITFIELDS;
                header.bV5SizeImage = static_cast<DWORD>(pixels);
                header.bV5RedMask = 0x00FF0000u;
                header.bV5GreenMask = 0x0000FF00u;
                header.bV5BlueMask = 0x000000FFu;
                header.bV5AlphaMask = 0xFF000000u;
                header.bV5CSType = LCS_sRGB;
                header.bV5Intent = LCS_GM_IMAGES;
                std::memcpy(out, &header, sizeof(header));
                std::uint8_t* to = out + sizeof(header);
                for (std::uint32_t y = 0; y < image.height; y++) {
                    const std::uint8_t* from = image.rgba.data() + std::size_t{image.height - 1u - y} * image.width * 4u;
                    for (std::uint32_t x = 0; x < image.width; x++, from += 4, to += 4) {
                        to[0] = from[2];
                        to[1] = from[1];
                        to[2] = from[0];
                        to[3] = from[3];
                    }
                }
            });
            if (png == nullptr || dib == nullptr) {
                if (png != nullptr) {
                    GlobalFree(png);
                }
                if (dib != nullptr) {
                    GlobalFree(dib);
                }
                done(LUMA_E_MEMORY);
                return;
            }
            OpenedClipboard clipboard;
            if (!clipboard.opened || !EmptyClipboard()) {
                GlobalFree(png);
                GlobalFree(dib);
                done(LUMA_E_IO);
                return;
            }
            const UINT format = png_format();
            const bool png_set = format != 0u && SetClipboardData(format, png) != nullptr;
            if (!png_set) {
                GlobalFree(png);
            }
            const bool dib_set = SetClipboardData(CF_DIBV5, dib) != nullptr;
            if (!dib_set) {
                GlobalFree(dib);
            }
            done(png_set && dib_set ? LUMA_OK : LUMA_E_IO);
        } catch (...) {
            done(LUMA_E_MEMORY);
        }
    };
    if (!post_job(std::move(job))) {
        done(LUMA_E_IO);
    }
}

// ---- Links ---------------------------------------------------------------------------------------------------------

void open_link(std::string url, std::function<void(std::int32_t)> done) {
    auto job = [url = std::move(url), done]() {
        try {
            const int length = MultiByteToWideChar(CP_UTF8, 0, url.data(), static_cast<int>(url.size()), nullptr, 0);
            if (length <= 0) {
                done(LUMA_E_IO);
                return;
            }
            std::wstring wide(static_cast<std::size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, url.data(), static_cast<int>(url.size()), wide.data(), length);
            const HINSTANCE result = ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            done(reinterpret_cast<INT_PTR>(result) > 32 ? LUMA_OK : LUMA_E_IO);
        } catch (...) {
            done(LUMA_E_MEMORY);
        }
    };
    if (!post_job(std::move(job))) {
        done(LUMA_E_IO);
    }
}

const Platform kPlatform = {facts, read_text, read_image, write_text, write_image, open_link};

// ---- The display (section 4) ---------------------------------------------------------------------------------------

// The window thread's: the refresh rate is read again only when the monitor or its settings change.
HMONITOR g_rate_monitor = nullptr;
std::uint32_t g_rate_mhz = 0;

}  // namespace

const Platform* win32_platform() noexcept {
    return &kPlatform;
}

void win32_window_changed(void* window, int active, bool settings_changed) noexcept {
    const HWND hwnd = static_cast<HWND>(window);
    LumaSystemDisplay display{};
    display.size = sizeof(display);
    RECT client{};
    if (GetClientRect(hwnd, &client)) {
        display.width = static_cast<std::uint32_t>(client.right - client.left);
        display.height = static_cast<std::uint32_t>(client.bottom - client.top);
    }
    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (monitor != nullptr && GetMonitorInfoW(monitor, &info)) {
        display.display_width = static_cast<std::uint32_t>(info.rcMonitor.right - info.rcMonitor.left);
        display.display_height = static_cast<std::uint32_t>(info.rcMonitor.bottom - info.rcMonitor.top);
        if (monitor != g_rate_monitor || settings_changed) {
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            g_rate_monitor = monitor;
            g_rate_mhz = EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1u
                             ? mode.dmDisplayFrequency * 1000u
                             : 0u;  // 0 and 1 are the hardware's default: unknown
        }
        display.refresh_mhz = g_rate_mhz;
        POINT origin{0, 0};
        if (ClientToScreen(hwnd, &origin)) {
            display.fullscreen = origin.x == info.rcMonitor.left && origin.y == info.rcMonitor.top &&
                                         static_cast<LONG>(display.width) == info.rcMonitor.right - info.rcMonitor.left &&
                                         static_cast<LONG>(display.height) == info.rcMonitor.bottom - info.rcMonitor.top
                                     ? 1u
                                     : 0u;
        }
    }
    const UINT dpi = GetDpiForWindow(hwnd);
    display.scale = dpi != 0u ? dpi * 1000u / 96u : 1000u;
    const bool in_front = active >= 0 ? active != 0 : GetForegroundWindow() == hwnd;
    display.focused = in_front && !IsIconic(hwnd) ? 1u : 0u;
    display_changed(display);
}

}  // namespace galaxy::luma_system
