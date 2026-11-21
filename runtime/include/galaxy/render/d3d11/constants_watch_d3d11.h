// SPDX-License-Identifier: GPL-3.0-only
// The constants watch (--diagnostics constants-watch, ledger P-332). Once a frame a piece of the constant ring holds three
// words (a running number, the ring's generation, the piece's first byte in the ring) and is bound for the compute stage
// as the renderer's own compute passes bind theirs (ComputeConstants); a one-thread dispatch
// (renderer_common.h constants_watch_shader_source) copies the three words it reads into a small buffer, which is read
// back once the frame's fence has passed. A difference prints one line, "[constants] watch: ...", with the words
// written and the words read: an older number is a binding left from before, a later one a late read, another
// generation another ring. The test runner fails a run that prints it (tools/lumacore/luma_test.py).
#pragma once

#include "galaxy/render/d3d11/d3d11_context.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace galaxy::render::d3d11 {

class ConstantsWatch {
public:
    // What a piece holds, and what a dispatch read of it.
    struct Words {
        std::uint32_t number = 0u;
        std::uint32_t generation = 0u;
        std::uint32_t offset = 0u;  // the piece's first byte in the ring

        friend bool operator==(const Words&, const Words&) = default;
    };

    // The shader's bytecode (ConstantsWatchCs). False when the device refuses an object; the watch then does nothing.
    bool initialize(ID3D11Device1* device, std::span<const unsigned char> shader);
    [[nodiscard]] bool ready() const noexcept { return shader_ != nullptr; }

    // One frame's watch (render thread, before the frame's signal): the reads whose frames have passed are compared,
    // then a new piece is written and read. A frame with every read still in flight writes none.
    void frame(ID3D11DeviceContext1* context);

    // The reads whose frames have passed, compared with what was written; returns the differences found, each printed.
    unsigned check(ID3D11DeviceContext1* context);
    // A new piece with the next number, and its place; a place without a buffer when there is no room or no free read.
    [[nodiscard]] Context::ConstantPlace write();
    // The dispatch that reads `place` through ComputeConstants, compared later with the piece write() gave last. The
    // renderer passes that piece's place; a test passes another to plant a wrong binding.
    bool record(ID3D11DeviceContext1* context, const Context::ConstantPlace& place);

    [[nodiscard]] unsigned compared() const noexcept { return compared_; }  // reads read back and compared
    [[nodiscard]] unsigned differences() const noexcept { return differences_; }
    [[nodiscard]] const std::string& last_line() const noexcept { return last_line_; }

private:
    static constexpr std::size_t kReads = 4u;  // reads in flight: the frames the fence may be behind, and one
    struct Read {
        ComPtr<ID3D11Buffer> seen;  // the dispatch's copy of the words
        ComPtr<ID3D11UnorderedAccessView> seen_view;
        ComPtr<ID3D11Buffer> readback;
        Words written;
        std::uint64_t fence = 0u;  // the signal that covers it; 0: free
    };
    [[nodiscard]] Read* free_read() noexcept;

    ComPtr<ID3D11ComputeShader> shader_;
    std::array<Read, kReads> reads_{};
    Words piece_{};  // the last piece write() gave, until record() takes it
    bool piece_written_ = false;
    std::uint32_t number_ = 0u;
    unsigned compared_ = 0u;
    unsigned differences_ = 0u;
    std::string last_line_;
};

}  // namespace galaxy::render::d3d11
