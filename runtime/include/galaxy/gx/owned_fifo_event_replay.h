#pragma once

// Galaxy adapter of Aurora's ordered, sealed frame staging contract:
// encounter/aurora@aeb38ab1fcc6a018999cd440f55b445236b736a0,
// lib/gfx/frame_packet.hpp::{FrameOp,FramePacket} and
// lib/gfx/recording.cpp::{capture_frame_op,seal_pass,push} (MIT; see
// THIRD-PARTY-NOTICES.md). Owns GX read bytes and copies alias identities
// only; GPU resources are never owned or replayed.

#include "galaxy/gx/dependency_event_capture.h"
#include "galaxy/gx/owned_fifo_packet.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace galaxy::gx {

// Call after a render parse while its captured event stream is still alive.
// `fifo` is that parse's input, including any pending command prefix; it is
// retained for inspection and not parsed again.
[[nodiscard]] inline OwnedFifoPacket capture_owned_fifo_events(
    std::span<const std::byte> parser_input,
    const OwnedDependencyEvents& captured,
    OwnedFifoPacket::Presentation presentation = {}) {
    const OwnedFifoPacket::Limits limits{};
    if (parser_input.size() > limits.fifo_bytes ||
        captured.captured_bytes() > limits.dependency_bytes ||
        captured.events().size() > limits.ordered_ops) {
        throw std::length_error("GX ordered packet capture limit exceeded");
    }
    OwnedFifoPacket packet(limits);
    packet.append_fifo(parser_input);
    std::uint64_t read_version = 0;
    for (const auto& event : captured.events()) {
        using EventKind = OwnedDependencyEvents::Kind;
        using OpKind = OwnedFifoPacket::OpKind;
        if (event.kind == EventKind::GuestRead) {
            packet.append_versioned_dependency(
                event.ordinal, ++read_version, event.guest_addr,
                captured.read_bytes(event), event.read_source);
            continue;
        }
        OwnedFifoPacket::OrderedOp op{};
        op.command_ordinal = event.ordinal;
        switch (event.kind) {
        case EventKind::AliasCopy: op.kind = OpKind::AliasCopy; break;
        case EventKind::AliasBind: op.kind = OpKind::AliasBind; break;
        case EventKind::AliasRetire: op.kind = OpKind::AliasRetire; break;
        case EventKind::PeFinish: op.kind = OpKind::PeFinish; break;
        case EventKind::PeToken: op.kind = OpKind::PeToken; break;
        case EventKind::XfbCopy: op.kind = OpKind::XfbCopy; break;
        case EventKind::GuestRead:
            throw std::logic_error("GX guest read dispatch failed");
        }
        op.alias = event.alias;
        op.alias_resource = event.resource;
        op.alias_generation = event.generation;
        op.token = event.token;
        op.interrupt = event.interrupt;
        if (event.kind == EventKind::XfbCopy) {
            op.xfb_dest_addr = event.guest_addr;
            op.xfb_exec_command = event.exec_command;
        }
        packet.append_ordered_op(op);
    }
    packet.set_presentation(presentation);
    packet.seal();
    return packet;
}

// Replays only the event description, so repeated guest reads, EFB alias
// generations and PE/XFB ordering can be checked after sealing. Opaque GPU
// resource identities are never dereferenced or submitted to D3D12.
[[nodiscard]] inline OwnedDependencyEvents replay_owned_fifo_events(
    const OwnedFifoPacket& packet) {
    OwnedDependencyEvents replayed;
    const auto ops = packet.ordered_ops();
    const auto dependencies = packet.dependencies();
    for (std::size_t index = 0; index < ops.size(); ++index) {
        const auto& op = ops[index];
        using OpKind = OwnedFifoPacket::OpKind;
        switch (op.kind) {
        case OpKind::GuestRead:
            if (op.dependency_index >= dependencies.size()) {
                throw std::logic_error("GX packet read index escaped staging");
            }
            replayed.guest_read(
                op.read_source,
                dependencies[op.dependency_index].guest_base,
                packet.read_bytes_for_op(index));
            break;
        case OpKind::AliasCopy:
            replayed.alias_copy(op.alias, op.alias_resource);
            break;
        case OpKind::AliasBind:
            replayed.alias_bind(op.alias, op.alias_resource);
            break;
        case OpKind::AliasRetire:
            replayed.alias_retire(op.alias, op.alias_resource);
            break;
        case OpKind::PeFinish: replayed.pe_finish(); break;
        case OpKind::PeToken:
            replayed.pe_token(op.token, op.interrupt);
            break;
        case OpKind::XfbCopy:
            replayed.xfb_copy(op.xfb_dest_addr, op.xfb_exec_command);
            break;
        }
        if (replayed.events().back().ordinal != op.command_ordinal ||
            ((op.kind == OpKind::AliasCopy || op.kind == OpKind::AliasBind ||
              op.kind == OpKind::AliasRetire) &&
             replayed.events().back().generation != op.alias_generation)) {
            throw std::logic_error("GX packet event order/version mismatch");
        }
    }
    return replayed;
}

[[nodiscard]] inline bool owned_fifo_events_equal(
    const OwnedDependencyEvents& captured,
    const OwnedDependencyEvents& replayed) {
    const auto lhs = captured.events();
    const auto rhs = replayed.events();
    if (lhs.size() != rhs.size() ||
        captured.has_external_resources() !=
            replayed.has_external_resources()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const auto& a = lhs[index];
        const auto& b = rhs[index];
        if (a.ordinal != b.ordinal || a.kind != b.kind ||
            a.read_source != b.read_source || a.guest_addr != b.guest_addr ||
            a.byte_size != b.byte_size || a.alias != b.alias ||
            a.resource != b.resource || a.generation != b.generation ||
            a.exec_command != b.exec_command || a.token != b.token ||
            a.interrupt != b.interrupt) {
            return false;
        }
        if (a.kind == OwnedDependencyEvents::Kind::GuestRead) {
            const auto original = captured.read_bytes(a);
            const auto restored = replayed.read_bytes(b);
            if (!std::equal(original.begin(), original.end(),
                            restored.begin(), restored.end())) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace galaxy::gx
