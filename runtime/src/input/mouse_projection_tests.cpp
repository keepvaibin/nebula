// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/native_input.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error Mouse projection must not include Windows.h
#endif
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

int main() {
    using namespace galaxy::input;
    const auto check = [](bool value) { if (!value) throw std::runtime_error("mouse projection invariant"); };
    struct View { int width, height; NativePointerViewport content; };
    const std::array views{View{1280,720,{0,0,1280,720}}, View{800,600,{0,75,800,450}},
                           View{1024,576,{128,0,768,576}}, View{2,2,{0,0,2,2}}};
    std::uint64_t digest = 14695981039346656037ull;
    std::size_t samples = 0;
    for (const auto& view : views) {
        NativeMousePointerSample sample{false,true,true,0,0,view.width,view.height,view.content};
        for (int y = 0; y < view.height; ++y) for (int x = 0; x < view.width; ++x) {
            sample.client_x = x; sample.client_y = y;
            const auto actual = project_native_mouse_pointer(sample);
            const auto& v = view.content;
            const bool inside = x >= v.left && x < v.left+v.width && y >= v.top && y < v.top+v.height;
            check(actual.visible == inside);
            if (inside) {
                const auto expected_x = static_cast<double>(x-v.left) * 2 / (v.width-1) - 1;
                const auto expected_y = static_cast<double>(y-v.top) * 2 / (v.height-1) - 1;
                check(std::fabs(actual.x-expected_x) < 0.0000002 && std::fabs(actual.y-expected_y) < 0.0000002);
                if (x == v.left || x == v.left+v.width-1) check(std::fabs(actual.x) == 1);
                if (y == v.top || y == v.top+v.height-1) check(std::fabs(actual.y) == 1);
            }
            for (const auto bits : {std::bit_cast<std::uint32_t>(actual.x),std::bit_cast<std::uint32_t>(actual.y),std::uint32_t(actual.visible)}) {
                digest ^= bits; digest *= 1099511628211ull;
            }
            ++samples;
        }
        sample.absolute_valid = false; check(!project_native_mouse_pointer(sample).visible);
        sample.absolute_valid = true; sample.inside_client = false; check(!project_native_mouse_pointer(sample).visible);
    }
    NativeMousePointerSample invalid{true,true,true,0,0,1280,720,{0,0,1280,720}};
    for (const auto value : {std::numeric_limits<int>::min(),-1,1,std::numeric_limits<int>::max()}) {
        invalid.viewport = {value,0,std::numeric_limits<int>::max(),720};
        check(!project_native_mouse_pointer(invalid).visible);
        invalid.viewport = {0,value,1280,std::numeric_limits<int>::max()};
        check(!project_native_mouse_pointer(invalid).visible);
    }
    std::cout << "Exact viewport projection: samples=" << samples << " digest=" << std::hex << digest
              << "; bars/focus-independent geometry/invalid bounds pass\n";
}
