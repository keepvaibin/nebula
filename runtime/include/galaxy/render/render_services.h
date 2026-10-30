#pragma once

#include <cstdint>

namespace galaxy {

enum class RenderLogLevel : std::uint32_t {
    Trace = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
};

// What the renderer asks of its host: a log line.
struct RenderServices {
    void* user{};
    void (*log)(void* user, RenderLogLevel level, const char* message){};
};

}  // namespace galaxy
