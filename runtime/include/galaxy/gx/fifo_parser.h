#pragma once

// GX FIFO command parser: dispatches the big-endian command stream to GxState and a FifoSink.
//
// An unknown opcode, a truncated payload or an unmapped guest pointer throws GxFatalError after
// the FIFO is written to a .gxdump file. Hardware no-op opcodes are accepted only where listed.

#include "galaxy/gx/gx_bitfields.h"
#include "galaxy/gx/gx_state.h"
#include "galaxy/guest_memory.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace galaxy::gx {

// FIFO opcodes (high byte of each command).
namespace op {
inline constexpr std::uint8_t kNop = 0x00;
inline constexpr std::uint8_t kLoadCpReg = 0x08;
inline constexpr std::uint8_t kLoadXfReg = 0x10;
inline constexpr std::uint8_t kLoadIndxA = 0x20;
inline constexpr std::uint8_t kLoadIndxB = 0x28;
inline constexpr std::uint8_t kLoadIndxC = 0x30;
inline constexpr std::uint8_t kLoadIndxD = 0x38;
inline constexpr std::uint8_t kUnknownMetrics = 0x44;
inline constexpr std::uint8_t kInvalidateVertexCache = 0x48;
inline constexpr std::uint8_t kLoadBpReg = 0x61;
inline constexpr std::uint8_t kDrawFirst = 0x80;
inline constexpr std::uint8_t kDrawLast = 0xBF;
// Host draw-identity marker, top level only (the runtime inserts it; the game
// never writes this opcode): u8 kind, u16 aux, u32 a, u32 b.
inline constexpr std::uint8_t kHostMarker = 0xF0;
inline constexpr std::size_t kHostMarkerBytes = 12u;
}  // namespace op

// Fatal parse error, with the FIFO offset and opcode of the failure.
class GxFatalError : public std::runtime_error {
public:
    GxFatalError(
        std::string message,
        std::size_t fifo_offset,
        std::uint8_t opcode)
        : std::runtime_error(std::move(message)),
          fifo_offset_(fifo_offset),
          opcode_(opcode) {}

    [[nodiscard]] std::size_t fifo_offset() const { return fifo_offset_; }
    [[nodiscard]] std::uint8_t opcode() const { return opcode_; }

private:
    std::size_t fifo_offset_;
    std::uint8_t opcode_;
};

// The top-level capture ended inside a command. VI retraces are not command boundaries, so
// the caller keeps the bytes from command_offset() and retries when more FIFO data arrives.
class FifoNeedMoreData final : public std::exception {
public:
    explicit FifoNeedMoreData(std::size_t command_offset)
        : command_offset_(command_offset) {}

    [[nodiscard]] std::size_t command_offset() const {
        return command_offset_;
    }

private:
    std::size_t command_offset_;
};

// Byte-swaps `count` big-endian 32-bit values from src into dst (no aliasing), 16 bytes per
// vector with a scalar tail.
void swap32_block(
    const std::byte* src,
    std::uint32_t* dst,
    std::size_t count);

// Checked big-endian reads over one command stream (the top-level FIFO or a CALL_DL window).
// An underrun throws GxFatalError, or FifoNeedMoreData when recover_truncation is set.
struct FifoCursor {
    std::span<const std::byte> data;
    std::size_t offset = 0;
    // Added to `offset` in error reports: 0 at top level, the CALL_DL opcode offset in a list.
    std::size_t base_offset = 0;
    std::size_t command_offset = 0;
    bool recover_truncation = false;

    [[nodiscard]] std::size_t remaining() const {
        return data.size() - offset;
    }
    [[nodiscard]] bool empty() const { return offset >= data.size(); }

    std::uint8_t read_u8() {
        require(1);
        return static_cast<std::uint8_t>(data[offset++]);
    }
    std::uint16_t read_u16() {
        require(2);
        const auto hi = static_cast<std::uint16_t>(data[offset]);
        const auto lo = static_cast<std::uint16_t>(data[offset + 1]);
        offset += 2;
        return static_cast<std::uint16_t>((hi << 8) | lo);
    }
    std::uint32_t read_u32() {
        require(4);
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v = (v << 8) | static_cast<std::uint32_t>(data[offset + i]);
        }
        offset += 4;
        return v;
    }

    // Returns the next `size` bytes unswapped, for bulk consumers, and advances past them.
    std::span<const std::byte> take(std::size_t size) {
        require(size);
        const auto window = data.subspan(offset, size);
        offset += size;
        return window;
    }

    void require(std::size_t size) const {
        if (data.size() - offset < size) {
            if (recover_truncation) {
                throw FifoNeedMoreData(command_offset);
            }
            throw GxFatalError(
                "GX FIFO truncated payload",
                base_offset + offset,
                offset < data.size()
                    ? static_cast<std::uint8_t>(data[offset])
                    : 0u);
        }
    }
};

}  // namespace galaxy::gx
