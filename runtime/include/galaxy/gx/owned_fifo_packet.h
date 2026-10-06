#pragma once

// Adapted from Aurora's frame-owned staging and ordered pass sealing:
//   lib/gfx/frame_packet.hpp::{StagingHighWater,FrameOp,FramePacket}
//   lib/gfx/recording.cpp::{current_high_water,capture_frame_op,seal_pass,push}
// https://github.com/encounter/aurora/tree/77326d45415a64c40e560cebd2cdef0a0f08d840
// Versioned read operations additionally adapt the same symbols at
// aeb38ab1fcc6a018999cd440f55b445236b736a0.
// Copyright (c) 2022 Luke Street. MIT license; see
// THIRD-PARTY-NOTICES.md (Aurora MIT notice). Galaxy replaces WebGPU objects with
// opaque GX FIFO bytes, owned guest dependencies, and ordered PE/XFB effects.

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <vector>

#include "galaxy/gx/dependency_event_capture.h"

namespace galaxy::gx {

// A self-owned record of one native GX FIFO parse. This object does not
// classify commands or issue guest-visible effects. The caller must supply
// the exact encountered command/read order, including nested CALL_DL.
// A consumer may inspect the packet only after seal(), when no source pointer
// or mutable staging area can change under delayed rendering.
class OwnedFifoPacket final {
public:
    struct Limits {
        std::size_t fifo_bytes = 16u * 1024u * 1024u;
        std::size_t dependency_bytes = 128u * 1024u * 1024u;
        std::size_t dependencies = 65'536u;
        std::size_t ordered_ops = 65'536u;
    };

    struct Dependency {
        std::uint32_t guest_base = 0;
        std::uint32_t size = 0;
        std::uint32_t staging_offset = 0;
        // Zero identifies the legacy disjoint-range API. A nonzero version
        // identifies one immutable read, even if a later read overlaps it.
        std::uint64_t read_version = 0;
    };

    enum class OpKind : std::uint8_t {
        PeFinish,
        PeToken,
        XfbCopy,
        GuestRead,
        AliasCopy,
        AliasBind,
        AliasRetire,
    };

    struct OrderedOp {
        // Monotonic encounter ordinal, including every read inside a command
        // and commands in nested display lists. A FIFO offset or GX command
        // ordinal alone is not a unique identity for several reads in a draw.
        std::uint64_t command_ordinal = 0;
        OpKind kind = OpKind::PeFinish;
        std::uint16_t token = 0;
        bool interrupt = false;
        std::uint32_t xfb_dest_addr = 0;
        std::uint32_t xfb_exec_command = 0;
        std::size_t dependency_index = 0;
        std::uint64_t read_version = 0;
        DependencyReadSource read_source = DependencyReadSource::Parser;
        DependencyAliasKey alias{};
        std::uintptr_t alias_resource = 0;
        std::uint64_t alias_generation = 0;
        // Aurora's high-water rule: an op can consume only bytes staged by
        // the time it was sealed, never bytes appended for a later op.
        std::size_t fifo_high_water = 0;
        std::size_t dependency_high_water = 0;
        std::size_t dependency_count_high_water = 0;
    };

    struct Presentation {
        bool requested = false;
        std::uint32_t displayed_xfb_addr = 0;
    };

    OwnedFifoPacket() = default;
    explicit OwnedFifoPacket(Limits limits) : limits_(limits) {}

    void append_fifo(std::span<const std::byte> bytes) {
        require_mutable();
        require_room(fifo_.size(), bytes.size(), limits_.fifo_bytes,
                     "owned GX FIFO exceeds packet limit");
        fifo_.insert(fifo_.end(), bytes.begin(), bytes.end());
    }

    void append_dependency(
        std::uint32_t guest_base,
        std::span<const std::byte> bytes) {
        require_mutable();
        if (versioned_dependencies_) {
            throw std::invalid_argument(
                "cannot mix versioned and disjoint GX dependencies");
        }
        if (bytes.empty() || dependencies_.size() >= limits_.dependencies ||
            bytes.size() > std::numeric_limits<std::uint32_t>::max() ||
            static_cast<std::uint64_t>(guest_base) + bytes.size() >
                (std::uint64_t{1} << 32u)) {
            throw std::invalid_argument("invalid owned GX dependency range");
        }
        // Streaming GX commands can read addresses in any order. Reject
        // overlap, including duplicate ranges, so an owned packet cannot
        // ambiguously expose two captured versions of one guest byte.
        const std::uint64_t end =
            static_cast<std::uint64_t>(guest_base) + bytes.size();
        const auto next = dependency_intervals_.lower_bound(guest_base);
        const bool overlaps_next =
            next != dependency_intervals_.end() && end > next->first;
        const bool overlaps_previous =
            next != dependency_intervals_.begin() &&
            std::prev(next)->second > guest_base;
        if (overlaps_next || overlaps_previous) {
            throw std::invalid_argument(
                "owned GX dependencies must be disjoint");
        }
        require_room(dependency_bytes_.size(), bytes.size(),
                     limits_.dependency_bytes,
                     "owned GX dependency staging exceeds packet limit");
        // Reserve both contiguous arrays before inserting the interval, so
        // none of the subsequent byte/descriptor appends can allocate.
        dependency_bytes_.reserve(dependency_bytes_.size() + bytes.size());
        dependencies_.reserve(dependencies_.size() + 1u);
        dependency_intervals_.emplace(guest_base, end);
        const auto offset = static_cast<std::uint32_t>(dependency_bytes_.size());
        dependency_bytes_.insert(
            dependency_bytes_.end(), bytes.begin(), bytes.end());
        dependencies_.push_back(Dependency{
            guest_base,
            static_cast<std::uint32_t>(bytes.size()),
            offset,
        });
        legacy_dependencies_ = true;
    }

    // Append one immutable guest read and its ordered FrameOp atomically.
    // Read versions are packet-local, contiguous and strictly increasing;
    // overlapping guest addresses retain every captured byte version. The
    // caller must supply the actual bytes seen at this encounter, not a later
    // read of mutable guest memory. The ordinal is an event ordinal (several
    // reads within one GX command each need a distinct ordinal).
    void append_versioned_dependency(
        std::uint64_t encounter_ordinal,
        std::uint64_t read_version,
        std::uint32_t guest_base,
        std::span<const std::byte> bytes,
        DependencyReadSource read_source = DependencyReadSource::Parser) {
        require_mutable();
        if (legacy_dependencies_ || bytes.empty() ||
            dependencies_.size() >= limits_.dependencies ||
            ops_.size() >= limits_.ordered_ops ||
            bytes.size() > std::numeric_limits<std::uint32_t>::max() ||
            static_cast<std::uint64_t>(guest_base) + bytes.size() >
                (std::uint64_t{1} << 32u) ||
            read_version == 0u ||
            last_read_version_ == std::numeric_limits<std::uint64_t>::max() ||
            read_version != last_read_version_ + 1u ||
            encounter_ordinal == 0u ||
            (!ops_.empty() &&
             encounter_ordinal <= ops_.back().command_ordinal)) {
            throw std::invalid_argument(
                "invalid versioned GX dependency encounter");
        }
        require_room(dependency_bytes_.size(), bytes.size(),
                     limits_.dependency_bytes,
                     "owned GX dependency staging exceeds packet limit");
        // All possible allocations precede any visible mutation. Byte and
        // aggregate appends below cannot allocate once these reserves pass.
        dependency_bytes_.reserve(dependency_bytes_.size() + bytes.size());
        dependencies_.reserve(dependencies_.size() + 1u);
        ops_.reserve(ops_.size() + 1u);
        const auto index = dependencies_.size();
        const auto offset = static_cast<std::uint32_t>(dependency_bytes_.size());
        dependency_bytes_.insert(
            dependency_bytes_.end(), bytes.begin(), bytes.end());
        dependencies_.push_back(Dependency{
            guest_base,
            static_cast<std::uint32_t>(bytes.size()),
            offset,
            read_version,
        });
        OrderedOp op{};
        op.command_ordinal = encounter_ordinal;
        op.kind = OpKind::GuestRead;
        op.dependency_index = index;
        op.read_version = read_version;
        op.read_source = read_source;
        op.fifo_high_water = fifo_.size();
        op.dependency_high_water = dependency_bytes_.size();
        op.dependency_count_high_water = dependencies_.size();
        ops_.push_back(op);
        last_read_version_ = read_version;
        versioned_dependencies_ = true;
    }

    void append_ordered_op(OrderedOp op) {
        require_mutable();
        switch (op.kind) {
        case OpKind::PeFinish:
        case OpKind::PeToken:
        case OpKind::XfbCopy:
        case OpKind::AliasCopy:
        case OpKind::AliasBind:
        case OpKind::AliasRetire:
            break;
        case OpKind::GuestRead:
            // Only append_versioned_dependency may create a read operation;
            // its byte payload and version are captured in the same step.
            throw std::invalid_argument(
                "GX guest read requires an owned versioned dependency");
        default:
            throw std::invalid_argument("unknown owned GX effect kind");
        }
        if (ops_.size() >= limits_.ordered_ops ||
            (!ops_.empty() &&
             op.command_ordinal <= ops_.back().command_ordinal)) {
            throw std::invalid_argument(
                "owned GX effects must follow exact command order");
        }
        const bool alias_op = op.kind == OpKind::AliasCopy ||
            op.kind == OpKind::AliasBind || op.kind == OpKind::AliasRetire;
        if ((op.kind == OpKind::PeFinish &&
             (op.token != 0u || op.interrupt || op.xfb_dest_addr != 0u ||
              op.xfb_exec_command != 0u)) ||
            (op.kind == OpKind::PeToken &&
             (op.xfb_dest_addr != 0u || op.xfb_exec_command != 0u)) ||
            (op.kind == OpKind::XfbCopy &&
             (op.token != 0u || op.interrupt)) ||
            (alias_op &&
             (op.token != 0u || op.interrupt || op.xfb_dest_addr != 0u ||
              op.xfb_exec_command != 0u)) ||
            (op.kind == OpKind::AliasCopy &&
             (op.alias_resource == 0u || op.alias_generation == 0u)) ||
            (!alias_op &&
             (op.alias_resource != 0u || op.alias_generation != 0u ||
              op.alias != DependencyAliasKey{})) ||
            op.dependency_index != 0u || op.read_version != 0u ||
            op.read_source != DependencyReadSource::Parser) {
            throw std::invalid_argument("invalid owned GX effect payload");
        }
        // The caller cannot forge a high-water mark for a future resource.
        op.fifo_high_water = fifo_.size();
        op.dependency_high_water = dependency_bytes_.size();
        op.dependency_count_high_water = dependencies_.size();
        ops_.push_back(op);
    }

    void set_presentation(Presentation presentation) {
        require_mutable();
        presentation_ = presentation;
    }

    void seal() {
        require_mutable();
        sealed_ = true;
    }

    [[nodiscard]] bool sealed() const noexcept { return sealed_; }

    [[nodiscard]] std::span<const std::byte> fifo() const {
        require_sealed();
        return fifo_;
    }

    [[nodiscard]] std::span<const Dependency> dependencies() const {
        require_sealed();
        return dependencies_;
    }

    [[nodiscard]] std::span<const std::byte> dependency_bytes(
        std::size_t index) const {
        require_sealed();
        if (versioned_dependencies_) {
            throw std::logic_error(
                "versioned GX bytes require an ordered read operation");
        }
        const Dependency& dependency = dependencies_.at(index);
        return std::span<const std::byte>(dependency_bytes_)
            .subspan(dependency.staging_offset, dependency.size);
    }

    // The only accessor for a read at a specific ordered operation. A PE/XFB
    // op, a forged index, or a version outside that op's high-water fails.
    [[nodiscard]] std::span<const std::byte> read_bytes_for_op(
        std::size_t op_index) const {
        require_sealed();
        const OrderedOp& op = ops_.at(op_index);
        if (op.kind != OpKind::GuestRead ||
            op.dependency_index >= op.dependency_count_high_water ||
            op.dependency_index >= dependencies_.size()) {
            throw std::out_of_range("not an owned GX guest read operation");
        }
        const Dependency& dependency = dependencies_[op.dependency_index];
        if (dependency.read_version == 0u ||
            dependency.read_version != op.read_version ||
            static_cast<std::uint64_t>(dependency.staging_offset) +
                    dependency.size > op.dependency_high_water) {
            throw std::logic_error("GX guest read version escaped its operation");
        }
        return std::span<const std::byte>(dependency_bytes_)
            .subspan(dependency.staging_offset, dependency.size);
    }

    [[nodiscard]] std::span<const OrderedOp> ordered_ops() const {
        require_sealed();
        return ops_;
    }

    [[nodiscard]] std::span<const std::byte> fifo_prefix(
        std::size_t op_index) const {
        require_sealed();
        return std::span<const std::byte>(fifo_)
            .first(ops_.at(op_index).fifo_high_water);
    }

    [[nodiscard]] std::span<const Dependency> dependency_prefix(
        std::size_t op_index) const {
        require_sealed();
        return std::span<const Dependency>(dependencies_)
            .first(ops_.at(op_index).dependency_count_high_water);
    }

    [[nodiscard]] Presentation presentation() const {
        require_sealed();
        return presentation_;
    }

private:
    static void require_room(
        std::size_t used,
        std::size_t incoming,
        std::size_t limit,
        const char* message) {
        if (used > limit || incoming > limit - used ||
            used + incoming > std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error(message);
        }
    }

    void require_mutable() const {
        if (sealed_) {
            throw std::logic_error("owned GX packet is sealed");
        }
    }

    void require_sealed() const {
        if (!sealed_) {
            throw std::logic_error("owned GX packet is not sealed");
        }
    }

    Limits limits_{};
    std::vector<std::byte> fifo_{};
    std::vector<std::byte> dependency_bytes_{};
    std::vector<Dependency> dependencies_{};
    std::map<std::uint32_t, std::uint64_t> dependency_intervals_{};
    std::vector<OrderedOp> ops_{};
    Presentation presentation_{};
    std::uint64_t last_read_version_ = 0;
    bool legacy_dependencies_ = false;
    bool versioned_dependencies_ = false;
    bool sealed_ = false;
};

}  // namespace galaxy::gx
