#pragma once

#include <functional>
#include <future>
#include <string>
#include <type_traits>
#include <utility>

namespace galaxy::gx {

// Both sources are immutable for the entire call. The compiler must support
// independent concurrent invocations; it receives no renderer/cache/guest state.
// Keep cache publication and PSO creation on the caller after both results exist.
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
    // get propagates a worker exception. If pixel compilation throws first,
    // this async future's destructor joins the worker before either source or
    // the caller's owning scope can unwind. No incomplete result is published.
    return std::pair<Result, Result>{vertex.get(), std::move(pixel)};
}

}  // namespace galaxy::gx
