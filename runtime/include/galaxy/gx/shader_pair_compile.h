#pragma once

// Compiles a GX vertex/pixel shader pair in parallel.

#include <functional>
#include <future>
#include <string>
#include <type_traits>
#include <utility>

namespace galaxy::gx {

// The vertex shader compiles on a worker thread, the pixel shader on the caller's. `compiler` must be safe to run
// concurrently and sees no renderer, cache or game state; the caller publishes both results and creates the PSO
// afterwards. Neither source may change during the call.
template <class Compiler>
[[nodiscard]] auto compile_independent_shader_pair(
    const std::string& vertex_source,
    const std::string& pixel_source,
    Compiler compiler) {
    using Result = std::invoke_result_t<
        Compiler, const std::string&, const char*, const char*>;
    auto vertex = std::async(
        std::launch::async,
        [compiler, &vertex_source]() mutable {
            return std::invoke(
                compiler, vertex_source, "vs_5_1", "gx_vs");
        });
    Result pixel = std::invoke(
        compiler, pixel_source, "ps_5_1", "gx_ps");
    // get() rethrows the worker's exception. If the pixel compile throws, the future's destructor joins the worker
    // before the sources go out of scope.
    return std::pair<Result, Result>{vertex.get(), std::move(pixel)};
}

}  // namespace galaxy::gx
