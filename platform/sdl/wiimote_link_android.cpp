// SPDX-License-Identifier: GPL-3.0-only
// The real Wii Remote on Android (wiimote_link.h): a remote on a DolphinBar, through Android's USB host
// (dev.nebula.galaxy.WiimoteUsb). Its Java side reads the open slot's reports on its own thread and hands them here;
// the worker waits for them here, so a wake() reaches it at once whatever the device does.
#include "galaxy/wiimote_link.h"

#include <SDL3/SDL_system.h>
#include <jni.h>
#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace galaxy::host::wiimote_link {

namespace {

constexpr std::size_t kReportBytes = 64;  // a remote's input reports are 22 at most; the bar's packets 64 at most
constexpr std::size_t kQueued = 64;       // reports kept while the worker is busy; the oldest go first

struct Report {
    std::array<std::uint8_t, kReportBytes> bytes{};
    int size = 0;
};

struct Java {
    jclass cls = nullptr;
    jmethodID slots = nullptr;
    jmethodID open = nullptr;
    jmethodID write = nullptr;
    jmethodID close = nullptr;
};

struct State {
    std::mutex lock;
    std::condition_variable changed;
    Java java;
    bool arrived = false;
    bool woken = false;
    int slot = -1;  // the open slot's key
    bool gone = false;
    std::deque<Report> reports;
};

// Never destroyed: the Java side may call in while the process exits.
State& state() {
    static State* instance = new State;
    return *instance;
}

// A Java call's exception is cleared (it is the call failing, as a false answer is).
bool cleared(JNIEnv* env) {
    if (!env->ExceptionCheck()) return false;
    env->ExceptionDescribe();
    env->ExceptionClear();
    return true;
}

class Slot final : public Link {
public:
    explicit Slot(int key) : key_(key) {}

    bool send(std::span<const std::uint8_t> report) override {
        State& s = state();
        JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        if (env == nullptr || s.java.cls == nullptr || report.empty()) return false;
        jbyteArray bytes = env->NewByteArray(static_cast<jsize>(report.size()));
        if (bytes == nullptr) {
            cleared(env);
            return false;
        }
        env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(report.size()), reinterpret_cast<const jbyte*>(report.data()));
        const bool sent = env->CallStaticBooleanMethod(s.java.cls, s.java.write, key_, bytes,
                                                      static_cast<jint>(report.size())) == JNI_TRUE;
        env->DeleteLocalRef(bytes);
        return !cleared(env) && sent;
    }

    int receive(std::uint32_t timeout_ms, bool wakeable) override {
        State& s = state();
        std::unique_lock<std::mutex> hold(s.lock);
        s.changed.wait_for(hold, std::chrono::milliseconds(timeout_ms),
                           [&] { return !s.reports.empty() || s.gone || (wakeable && s.woken); });
        if (!s.reports.empty()) {
            in_ = s.reports.front();
            s.reports.pop_front();
            return in_.size;
        }
        if (s.gone) return -1;
        if (wakeable) s.woken = false;
        return 0;
    }

    [[nodiscard]] std::span<const std::uint8_t> data(int size) const override {
        return {in_.bytes.data(), static_cast<std::size_t>(std::clamp(size, 0, in_.size))};
    }

private:
    int key_;
    Report in_;
};

}  // namespace

void start() {}

void each_remote(const std::function<bool(Link&)>& use) {
    State& s = state();
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (env == nullptr || s.java.cls == nullptr) return;
    auto* found = static_cast<jintArray>(env->CallStaticObjectMethod(s.java.cls, s.java.slots));
    if (cleared(env) || found == nullptr) return;
    std::vector<jint> keys(static_cast<std::size_t>(env->GetArrayLength(found)));
    env->GetIntArrayRegion(found, 0, static_cast<jsize>(keys.size()), keys.data());
    env->DeleteLocalRef(found);
    for (const jint key : keys) {
        {
            const std::lock_guard<std::mutex> hold(s.lock);
            s.slot = key;
            s.gone = false;
            s.reports.clear();
        }
        if (env->CallStaticBooleanMethod(s.java.cls, s.java.open, key) != JNI_TRUE || cleared(env)) continue;
        Slot slot(key);
        const bool answered = use(slot);
        env->CallStaticVoidMethod(s.java.cls, s.java.close, key);
        cleared(env);
        {
            const std::lock_guard<std::mutex> hold(s.lock);
            s.slot = -1;
            s.reports.clear();
        }
        if (answered) return;
    }
}

bool wait_arrival(std::uint32_t timeout_ms) {
    State& s = state();
    std::unique_lock<std::mutex> hold(s.lock);
    s.changed.wait_for(hold, std::chrono::milliseconds(timeout_ms), [&] { return s.arrived; });
    const bool arrived = s.arrived;
    s.arrived = false;
    return arrived;
}

void wake() {
    State& s = state();
    {
        const std::lock_guard<std::mutex> hold(s.lock);
        s.woken = true;
    }
    s.changed.notify_all();
}

void set_serving(bool serving) {
    // Android's display priority while it serves (an app may raise its own threads to it).
    (void)setpriority(PRIO_PROCESS, static_cast<id_t>(gettid()), serving ? -4 : 0);
}

}  // namespace galaxy::host::wiimote_link

namespace {

namespace wl = galaxy::host::wiimote_link;

}  // namespace

extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_WiimoteUsb_nativeInit(JNIEnv* env, jclass cls) {
    wl::State& s = wl::state();
    const std::lock_guard<std::mutex> hold(s.lock);
    if (s.java.cls != nullptr) return;
    wl::Java java;
    java.slots = env->GetStaticMethodID(cls, "slots", "()[I");
    java.open = env->GetStaticMethodID(cls, "open", "(I)Z");
    java.write = env->GetStaticMethodID(cls, "write", "(I[BI)Z");
    java.close = env->GetStaticMethodID(cls, "close", "(I)V");
    if (wl::cleared(env) || java.slots == nullptr || java.open == nullptr || java.write == nullptr || java.close == nullptr) {
        return;
    }
    java.cls = static_cast<jclass>(env->NewGlobalRef(cls));
    s.java = java;
}

extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_WiimoteUsb_nativeArrived(JNIEnv*, jclass) {
    wl::State& s = wl::state();
    {
        const std::lock_guard<std::mutex> hold(s.lock);
        s.arrived = true;
    }
    s.changed.notify_all();
}

extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_WiimoteUsb_nativeReport(JNIEnv* env, jclass, jint key, jbyteArray report,
                                                                                 jint length) {
    if (report == nullptr || length <= 0) return;
    wl::Report got;
    got.size = std::min<int>(length, static_cast<int>(got.bytes.size()));
    got.size = std::min<int>(got.size, env->GetArrayLength(report));
    env->GetByteArrayRegion(report, 0, got.size, reinterpret_cast<jbyte*>(got.bytes.data()));
    wl::State& s = wl::state();
    {
        const std::lock_guard<std::mutex> hold(s.lock);
        if (key != s.slot) return;
        if (s.reports.size() == wl::kQueued) s.reports.pop_front();
        s.reports.push_back(got);
    }
    s.changed.notify_all();
}

extern "C" JNIEXPORT void JNICALL Java_dev_nebula_galaxy_WiimoteUsb_nativeGone(JNIEnv*, jclass, jint key) {
    wl::State& s = wl::state();
    {
        const std::lock_guard<std::mutex> hold(s.lock);
        if (key != s.slot) return;
        s.gone = true;
    }
    s.changed.notify_all();
}
