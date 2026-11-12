// SPDX-License-Identifier: GPL-3.0-only
// LumaCore's system services on SDL (contract services-system 3 to 6): Linux and SteamOS, Android, and the SDL build
// on macOS. The clipboard and links through SDL on its main thread (SDL_RunOnMainThread), answered from one worker
// thread that also decodes and encodes; Android's pictures through the activity's ClipboardManager
// (ClipboardBridge.java). The display snapshot from the event loop (host_window_sdl.cpp). The worker starts at the
// first clipboard or link request (no cost with no mod).

#include "galaxy/luma_system_platform.h"

#include <SDL3/SDL.h>

#if defined(__ANDROID__)
#include <jni.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#include <sys/utsname.h>
#else
#include <sys/utsname.h>
#endif

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace galaxy::luma_system {
namespace {

constexpr std::uint64_t kTextMost = 16ull << 20u;     // services-system 1 item 5
constexpr std::uint64_t kEncodedMost = 80ull << 20u;  // a PNG of a picture within 64 MiB of pixels, with its headers

// ---- Facts (section 3), read once ------------------------------------------------------------------------------

std::once_flag g_facts_once;
LumaSystemFacts g_facts{};

#if !defined(__ANDROID__)
// /etc/os-release's PRETTY_NAME, without its quotes; "" when there is none.
std::string pretty_name() {
    std::ifstream file("/etc/os-release");
    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            std::string value = line.substr(12);
            if (value.size() >= 2u && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
                value = value.substr(1, value.size() - 2u);
            }
            return value;
        }
    }
    return {};
}
#endif

void read_facts() noexcept {
    try {
#if defined(__ANDROID__)
        g_facts.platform = LUMA_SYSTEM_ANDROID;
#elif defined(__APPLE__) && TARGET_OS_IOS
        g_facts.platform = LUMA_SYSTEM_IOS;
#elif defined(__APPLE__)
        g_facts.platform = LUMA_SYSTEM_MACOS;
#else
        g_facts.platform = LUMA_SYSTEM_LINUX;
#endif
#if defined(__aarch64__)
        g_facts.arch = LUMA_SYSTEM_ARM64;
#else
        g_facts.arch = LUMA_SYSTEM_X64;
#endif
#if defined(NEBULA_GPU_METAL)
        g_facts.renderer = LUMA_SYSTEM_METAL;
#else
        g_facts.renderer = LUMA_SYSTEM_VULKAN;
#endif
#if defined(__ANDROID__)
        g_facts.clipboard_images = 1u;  // through the activity's ClipboardManager
#else
        // SDL's MIME clipboard needs a real video driver (X11, Wayland, Cocoa); none on the offscreen and dummy ones.
        const char* driver = SDL_GetCurrentVideoDriver();
        g_facts.clipboard_images =
            driver != nullptr && (std::strcmp(driver, "x11") == 0 || std::strcmp(driver, "wayland") == 0 ||
                                  std::strcmp(driver, "cocoa") == 0)
                ? 1u
                : 0u;
#endif
        int count = 0;
        if (SDL_Locale** locales = SDL_GetPreferredLocales(&count)) {
            if (count > 0 && locales[0] != nullptr && locales[0]->language != nullptr) {
                std::string tag = locales[0]->language;
                if (locales[0]->country != nullptr && locales[0]->country[0] != 0) {
                    tag += '-';
                    tag += locales[0]->country;
                }
                std::snprintf(g_facts.locale, sizeof(g_facts.locale), "%s", tag.c_str());
            }
            SDL_free(locales);
        }
#if defined(__ANDROID__)
        std::snprintf(g_facts.os, sizeof(g_facts.os), "Android (API %d)", SDL_GetAndroidSDKVersion());
#else
        utsname name{};
        if (uname(&name) == 0) {
            const std::string pretty = pretty_name();
            if (!pretty.empty()) {
                std::snprintf(g_facts.os, sizeof(g_facts.os), "%s (%s %s)", pretty.c_str(), name.sysname, name.release);
            } else {
                std::snprintf(g_facts.os, sizeof(g_facts.os), "%s %s", name.sysname, name.release);
            }
        }
#endif
    } catch (...) {
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

// ---- The worker and SDL's main thread --------------------------------------------------------------------------

std::mutex g_jobs_lock;
std::condition_variable g_jobs_ready;
std::deque<std::function<void()>> g_jobs;
bool g_started = false;  // g_jobs_lock

void worker_main() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(g_jobs_lock);
            g_jobs_ready.wait(lock, [] { return !g_jobs.empty(); });
            job = std::move(g_jobs.front());
            g_jobs.pop_front();
        }
        job();
    }
}

// Queues `job` for the worker, started at the first. False: no thread or no memory.
bool post_job(std::function<void()> job) {
    try {
        {
            const std::lock_guard<std::mutex> lock(g_jobs_lock);
            if (!g_started) {
                std::thread(worker_main).detach();
                g_started = true;
            }
            g_jobs.push_back(std::move(job));
        }
        g_jobs_ready.notify_one();
        return true;
    } catch (...) {
        return false;
    }
}

// `job` on SDL's main thread at its next event pump (SDL's clipboard and its links belong to that thread). False: SDL
// did not take it (the caller answers).
bool on_main(std::function<void()> job) {
    auto* held = new (std::nothrow) std::function<void()>(std::move(job));
    if (held == nullptr) {
        return false;
    }
    const auto run = [](void* user) {
        auto* call = static_cast<std::function<void()>*>(user);
        (*call)();
        delete call;
    };
    if (!SDL_RunOnMainThread(run, held, false)) {
        delete held;
        return false;
    }
    return true;
}

// The answer, from the worker (never from the main thread: a done may decode a picture).
void answer_text(std::function<void(std::int32_t, std::string)> done, std::int32_t status, std::string text) {
    auto job = [done, status, text = std::move(text)]() mutable { done(status, std::move(text)); };
    if (!post_job(std::move(job))) {
        done(LUMA_E_MEMORY, {});
    }
}
#if !defined(__ANDROID__)
void answer_image(std::function<void(std::int32_t, Image)> done, std::int32_t status, Image image) {
    auto job = [done, status, image = std::move(image)]() mutable { done(status, std::move(image)); };
    if (!post_job(std::move(job))) {
        done(LUMA_E_MEMORY, {});
    }
}
#endif
void answer_status(std::function<void(std::int32_t)> done, std::int32_t status) {
    if (!post_job([done, status]() { done(status); })) {
        done(LUMA_E_MEMORY);
    }
}

// ---- Android's pictures (ClipboardBridge.java) -----------------------------------------------------------------

#if defined(__ANDROID__)
struct Bridge {
    jclass type = nullptr;
    jmethodID has_image = nullptr;
    jmethodID read_image = nullptr;
    jmethodID write_image = nullptr;
};
std::once_flag g_bridge_once;
Bridge g_bridge;

// The bridge's class through the activity's class loader (a thread SDL attached has only the system's).
void find_bridge() {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (env == nullptr || activity == nullptr) {
        return;
    }
    jclass activity_type = env->GetObjectClass(activity);
    jmethodID get_loader = env->GetMethodID(activity_type, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject loader = get_loader != nullptr ? env->CallObjectMethod(activity, get_loader) : nullptr;
    jclass loader_type = loader != nullptr ? env->GetObjectClass(loader) : nullptr;
    jmethodID load_class =
        loader_type != nullptr ? env->GetMethodID(loader_type, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;") : nullptr;
    jstring name = env->NewStringUTF("dev.nebula.galaxy.ClipboardBridge");
    jobject type = load_class != nullptr && name != nullptr ? env->CallObjectMethod(loader, load_class, name) : nullptr;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        type = nullptr;
    }
    if (type != nullptr) {
        auto* bridge_type = static_cast<jclass>(type);
        g_bridge.has_image = env->GetStaticMethodID(bridge_type, "hasImage", "(Landroid/app/Activity;)Z");
        g_bridge.read_image = env->GetStaticMethodID(bridge_type, "readImage", "(Landroid/app/Activity;I)[B");
        g_bridge.write_image = env->GetStaticMethodID(bridge_type, "writeImage", "(Landroid/app/Activity;[B)Z");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        } else if (g_bridge.has_image != nullptr && g_bridge.read_image != nullptr && g_bridge.write_image != nullptr) {
            g_bridge.type = static_cast<jclass>(env->NewGlobalRef(bridge_type));
        }
        env->DeleteLocalRef(type);
    }
    for (jobject local : {static_cast<jobject>(name), static_cast<jobject>(loader_type), loader, static_cast<jobject>(activity_type),
                          activity}) {
        if (local != nullptr) {
            env->DeleteLocalRef(local);
        }
    }
}

// The worker's: a picture from the clipboard, through the bridge.
std::int32_t android_read_image(Image& image) {
    std::call_once(g_bridge_once, find_bridge);
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (g_bridge.type == nullptr || env == nullptr || activity == nullptr) {
        if (env != nullptr && activity != nullptr) {
            env->DeleteLocalRef(activity);
        }
        return LUMA_E_IO;
    }
    std::int32_t status = LUMA_E_NOT_FOUND;
    if (env->CallStaticBooleanMethod(g_bridge.type, g_bridge.has_image, activity) == JNI_TRUE && !env->ExceptionCheck()) {
        auto bytes = static_cast<jbyteArray>(
            env->CallStaticObjectMethod(g_bridge.type, g_bridge.read_image, activity, static_cast<jint>(kEncodedMost)));
        if (env->ExceptionCheck() || bytes == nullptr) {
            status = LUMA_E_IO;
        } else {
            const jsize length = env->GetArrayLength(bytes);
            if (length == 0) {
                status = LUMA_E_TOO_LARGE;  // the bridge's "over the limit"
            } else {
                image.encoded.resize(static_cast<std::size_t>(length));
                env->GetByteArrayRegion(bytes, 0, length, reinterpret_cast<jbyte*>(image.encoded.data()));
                status = LUMA_OK;
            }
        }
        if (bytes != nullptr) {
            env->DeleteLocalRef(bytes);
        }
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        status = LUMA_E_IO;
    }
    env->DeleteLocalRef(activity);
    return status;
}

// The worker's: the PNG on the clipboard, through the bridge (the app's cache file, its FileProvider address).
std::int32_t android_write_image(const Image& image) {
    std::call_once(g_bridge_once, find_bridge);
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (g_bridge.type == nullptr || env == nullptr || activity == nullptr) {
        if (env != nullptr && activity != nullptr) {
            env->DeleteLocalRef(activity);
        }
        return LUMA_E_IO;
    }
    std::int32_t status = LUMA_E_MEMORY;
    if (jbyteArray bytes = env->NewByteArray(static_cast<jsize>(image.encoded.size()))) {
        env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(image.encoded.size()), reinterpret_cast<const jbyte*>(image.encoded.data()));
        const jboolean set = env->CallStaticBooleanMethod(g_bridge.type, g_bridge.write_image, activity, bytes);
        status = !env->ExceptionCheck() && set == JNI_TRUE ? LUMA_OK : LUMA_E_IO;
        env->DeleteLocalRef(bytes);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        status = LUMA_E_IO;
    }
    env->DeleteLocalRef(activity);
    return status;
}
#endif

// ---- The clipboard and links -----------------------------------------------------------------------------------

void read_text(std::function<void(std::int32_t, std::string)> done) {
    const bool taken = on_main([done]() {
        if (!SDL_HasClipboardText()) {
            answer_text(done, LUMA_E_NOT_FOUND, {});
            return;
        }
        char* text = SDL_GetClipboardText();
        if (text == nullptr) {
            answer_text(done, LUMA_E_IO, {});
            return;
        }
        const std::size_t length = std::strlen(text);
        if (length == 0u) {
            SDL_free(text);
            answer_text(done, LUMA_E_NOT_FOUND, {});
            return;
        }
        if (length > kTextMost) {
            SDL_free(text);
            answer_text(done, LUMA_E_TOO_LARGE, {});
            return;
        }
        std::string copy;
        try {
            copy.assign(text, length);
        } catch (...) {
            SDL_free(text);
            answer_text(done, LUMA_E_MEMORY, {});
            return;
        }
        SDL_free(text);
        answer_text(done, LUMA_OK, std::move(copy));
    });
    if (!taken) {
        done(LUMA_E_IO, {});
    }
}

void write_text(std::string text, std::function<void(std::int32_t)> done) {
    const bool taken = on_main([text = std::move(text), done]() { answer_status(done, SDL_SetClipboardText(text.c_str()) ? LUMA_OK : LUMA_E_IO); });
    if (!taken) {
        done(LUMA_E_IO);
    }
}

void read_image(std::function<void(std::int32_t, Image)> done) {
#if defined(__ANDROID__)
    if (!post_job([done]() {
            Image image;
            const std::int32_t status = android_read_image(image);
            done(status, std::move(image));  // decoded by the service, on this thread
        })) {
        done(LUMA_E_MEMORY, {});
    }
#else
    const bool taken = on_main([done]() {
        // The first picture type the clipboard offers that the service decodes.
        for (const char* type : {"image/png", "image/bmp", "image/jpeg"}) {
            if (!SDL_HasClipboardData(type)) {
                continue;
            }
            std::size_t size = 0;
            void* data = SDL_GetClipboardData(type, &size);
            if (data == nullptr) {
                answer_image(done, LUMA_E_IO, {});
                return;
            }
            if (size > kEncodedMost) {
                SDL_free(data);
                answer_image(done, LUMA_E_TOO_LARGE, {});
                return;
            }
            Image image;
            try {
                const auto* bytes = static_cast<const std::uint8_t*>(data);
                image.encoded.assign(bytes, bytes + size);
            } catch (...) {
                SDL_free(data);
                answer_image(done, LUMA_E_MEMORY, {});
                return;
            }
            SDL_free(data);
            answer_image(done, LUMA_OK, std::move(image));
            return;
        }
        answer_image(done, LUMA_E_NOT_FOUND, {});
    });
    if (!taken) {
        done(LUMA_E_IO, {});
    }
#endif
}

#if !defined(__ANDROID__)
// The PNG SDL hands out until it says it is done with it (SDL_SetClipboardData's cleanup).
struct HeldPng {
    std::vector<std::uint8_t> bytes;
};
const char* const kPngTypes[] = {"image/png"};
#endif

void write_image(Image image, std::function<void(std::int32_t)> done) {
    auto job = [image = std::move(image), done]() mutable {
        if (!encode_png(image)) {
            done(LUMA_E_MEMORY);
            return;
        }
#if defined(__ANDROID__)
        done(android_write_image(image));
#else
        auto* held = new (std::nothrow) HeldPng();
        if (held == nullptr) {
            done(LUMA_E_MEMORY);
            return;
        }
        held->bytes = std::move(image.encoded);
        const bool taken = on_main([held, done]() {
            const auto give = [](void* user, const char* type, std::size_t* size) -> const void* {
                auto* png = static_cast<HeldPng*>(user);
                if (type == nullptr || std::strcmp(type, "image/png") != 0) {
                    *size = 0;
                    return nullptr;
                }
                *size = png->bytes.size();
                return png->bytes.data();
            };
            const auto cleanup = [](void* user) { delete static_cast<HeldPng*>(user); };
            // SDL owns `held` from here, failed or not (it keeps the cleanup and calls it when the clipboard next
            // changes); it refuses earlier only without a video device, which has no picture clipboard (facts).
            const bool set = SDL_SetClipboardData(give, cleanup, held, kPngTypes, 1);
            answer_status(done, set ? LUMA_OK : LUMA_E_IO);
        });
        if (!taken) {
            delete held;
            done(LUMA_E_IO);
        }
#endif
    };
    if (!post_job(std::move(job))) {
        done(LUMA_E_MEMORY);
    }
}

void open_link(std::string url, std::function<void(std::int32_t)> done) {
    const bool taken = on_main([url = std::move(url), done]() { answer_status(done, SDL_OpenURL(url.c_str()) ? LUMA_OK : LUMA_E_IO); });
    if (!taken) {
        done(LUMA_E_IO);
    }
}

const Platform kPlatform = {facts, read_text, read_image, write_text, write_image, open_link};

std::uint32_t at_least_zero(float value) {
    return value > 0.0f ? static_cast<std::uint32_t>(value + 0.5f) : 0u;
}

}  // namespace

const Platform* sdl_platform() noexcept {
    return &kPlatform;
}

bool sdl_display_event(std::uint32_t type) noexcept {
    switch (type) {
    case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
    case SDL_EVENT_DISPLAY_ORIENTATION:
    case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_WINDOW_HIDDEN:
        return true;
    default:
        return false;
    }
}

void sdl_window_changed(void* window) noexcept {
    auto* const sdl_window = static_cast<SDL_Window*>(window);
    if (sdl_window == nullptr) {
        return;
    }
    LumaSystemDisplay display{};
    display.size = sizeof(display);
    int width = 0;
    int height = 0;
    if (SDL_GetWindowSizeInPixels(sdl_window, &width, &height)) {
        display.width = static_cast<std::uint32_t>(std::max(width, 0));
        display.height = static_cast<std::uint32_t>(std::max(height, 0));
    }
    if (const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(sdl_window))) {
        // A mode's size is in points; its pixel density makes it pixels.
        const float density = mode->pixel_density > 0.0f ? mode->pixel_density : 1.0f;
        display.display_width = at_least_zero(static_cast<float>(mode->w) * density);
        display.display_height = at_least_zero(static_cast<float>(mode->h) * density);
        if (mode->refresh_rate_numerator > 0 && mode->refresh_rate_denominator > 0) {
            display.refresh_mhz = static_cast<std::uint32_t>(static_cast<std::uint64_t>(mode->refresh_rate_numerator) * 1000u /
                                                             static_cast<std::uint64_t>(mode->refresh_rate_denominator));
        } else {
            display.refresh_mhz = at_least_zero(mode->refresh_rate * 1000.0f);
        }
    }
    const float scale = SDL_GetWindowDisplayScale(sdl_window);
    display.scale = scale > 0.0f ? at_least_zero(scale * 1000.0f) : 1000u;
    SDL_Rect safe{};
    int points_width = 0;
    int points_height = 0;
    if (SDL_GetWindowSafeArea(sdl_window, &safe) && SDL_GetWindowSize(sdl_window, &points_width, &points_height)) {
        const float density = std::max(SDL_GetWindowPixelDensity(sdl_window), 0.0f);
        display.safe_left = at_least_zero(static_cast<float>(safe.x) * density);
        display.safe_top = at_least_zero(static_cast<float>(safe.y) * density);
        display.safe_right = at_least_zero(static_cast<float>(points_width - safe.x - safe.w) * density);
        display.safe_bottom = at_least_zero(static_cast<float>(points_height - safe.y - safe.h) * density);
    }
    const SDL_WindowFlags flags = SDL_GetWindowFlags(sdl_window);
    display.fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0u ? 1u : 0u;
    display.focused = (flags & SDL_WINDOW_INPUT_FOCUS) != 0u && (flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) == 0u ? 1u : 0u;
    display_changed(display);
}

}  // namespace galaxy::luma_system
