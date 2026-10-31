// Environment variables for the host's switches and test hooks (host_env.h).
#include "galaxy/host_env.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace galaxy::host {
namespace {
std::atomic<bool> g_env_flag_cache_enabled{false};
}  // namespace

std::optional<std::filesystem::path> get_env_path(const wchar_t* name) {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(name, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return std::nullopt;
    }
    return std::filesystem::path(buffer);
}

bool read_env_flag_uncached(const char* name, bool default_value) {
    char buffer[32]{};
    std::size_t length = 0;
    if (getenv_s(&length, buffer, sizeof(buffer), name) != 0 ||
        length <= 1) {
        return default_value;
    }
    return buffer[0] != '0' && buffer[0] != 'f' && buffer[0] != 'F' &&
           buffer[0] != 'n' && buffer[0] != 'N';
}

// The input report reads its script switches on every poll, and getenv_s scans the whole environment block each
// time. Once set_env_flag_cache_enabled(true) runs, a value is read once per literal name (the pointer is the key).
// Tests leave the cache off so the variables they change stay visible.
namespace {
struct EnvFlagCacheSlot {
    std::atomic<const char*> name{nullptr};
    std::atomic<int> value{0};
};
EnvFlagCacheSlot g_env_flag_cache[256];
}  // namespace

bool read_env_flag(const char* name, bool default_value) {
    if (!g_env_flag_cache_enabled.load(std::memory_order_acquire)) {
        return read_env_flag_uncached(name, default_value);
    }
    const auto key = reinterpret_cast<std::uintptr_t>(name);
    std::size_t index = static_cast<std::size_t>((key >> 3u) ^ (key >> 13u)) & 255u;
    for (std::size_t probe = 0; probe < 8u; ++probe, index = (index + 1u) & 255u) {
        EnvFlagCacheSlot& slot = g_env_flag_cache[index];
        const char* const owner = slot.name.load(std::memory_order_acquire);
        if (owner == name) {
            const int value = slot.value.load(std::memory_order_acquire);
            if (value != 0) return value > 0;
            break;
        }
        if (owner == nullptr) {
            const bool result = read_env_flag_uncached(name, default_value);
            const char* expected = nullptr;
            if (slot.name.compare_exchange_strong(expected, name, std::memory_order_acq_rel)) {
                slot.value.store(result ? 1 : -1, std::memory_order_release);
            }
            return result;
        }
    }
    return read_env_flag_uncached(name, default_value);
}

std::optional<std::string> read_env_string_uncached(const char* name) {
    // Short values fit the stack buffer; getenv_s reports the size of a longer one for an exact allocation.
    constexpr std::size_t kStackEnvironmentValueLength = 512u;
    char stack_buffer[kStackEnvironmentValueLength];
    std::size_t length = 0;
    const errno_t result =
        getenv_s(&length, stack_buffer, sizeof(stack_buffer), name);
    if (result == 0) {
        if (length <= 1u) {
            return std::nullopt;
        }
        return std::string(stack_buffer, length - 1u);
    }
    std::vector<char> buffer(length + 1u);
    if (getenv_s(&length, buffer.data(), buffer.size(), name) != 0) {
        throw std::runtime_error(
            std::string("environment variable is too long: ") + name);
    }
    if (length <= 1u) {
        return std::nullopt;
    }
    return std::string(buffer.data(), length - 1u);
}

// The string counterpart of the flag cache, for the scripted input read on every report.
namespace {
struct EnvStringCacheSlot {
    std::atomic<const char*> name{nullptr};
    std::atomic<int> state{0};  // 0 filling, 1 absent, 2 present
    std::string value;
};
EnvStringCacheSlot g_env_string_cache[128];
}  // namespace

std::optional<std::string> read_env_string(const char* name) {
    if (!g_env_flag_cache_enabled.load(std::memory_order_acquire)) {
        return read_env_string_uncached(name);
    }
    const auto key = reinterpret_cast<std::uintptr_t>(name);
    std::size_t index = static_cast<std::size_t>((key >> 3u) ^ (key >> 11u)) & 127u;
    for (std::size_t probe = 0; probe < 8u; ++probe, index = (index + 1u) & 127u) {
        EnvStringCacheSlot& slot = g_env_string_cache[index];
        const char* const owner = slot.name.load(std::memory_order_acquire);
        if (owner == name) {
            const int state = slot.state.load(std::memory_order_acquire);
            if (state == 1) return std::nullopt;
            if (state == 2) return slot.value;
            break;
        }
        if (owner == nullptr) {
            const char* expected = nullptr;
            if (!slot.name.compare_exchange_strong(expected, name, std::memory_order_acq_rel)) {
                if (expected == name) break;
                continue;
            }
            std::optional<std::string> value = read_env_string_uncached(name);
            if (value.has_value()) slot.value = *value;
            slot.state.store(value.has_value() ? 2 : 1, std::memory_order_release);
            return value;
        }
    }
    return read_env_string_uncached(name);
}

void set_env_flag_cache_enabled(bool enabled) noexcept {
    g_env_flag_cache_enabled.store(enabled, std::memory_order_release);
}

}  // namespace galaxy::host
