#include "galaxy/gx/owned_fifo_packet.h"
#include "galaxy/gx/owned_fifo_event_replay.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>

namespace {

using galaxy::gx::OwnedFifoPacket;

template <typename Exception, typename Fn>
bool throws(Fn&& action) {
    try {
        action();
    } catch (const Exception&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

bool owned_bytes_and_ordered_effects() {
    // Two FIFO segments model entering a nested display list and returning.
    // Command ordinals, not FIFO offsets, keep PE and XFB effects in encounter
    // order.
    std::array<std::byte, 3> first_fifo{
        std::byte{0x40}, std::byte{0x12}, std::byte{0x34}};
    std::array<std::byte, 2> second_fifo{
        std::byte{0x61}, std::byte{0x7f}};
    std::array<std::byte, 3> first_dependency{
        std::byte{0xa1}, std::byte{0xb2}, std::byte{0xc3}};
    std::array<std::byte, 2> second_dependency{
        std::byte{0xd4}, std::byte{0xe5}};

    OwnedFifoPacket packet;
    packet.append_fifo(first_fifo);
    packet.append_dependency(0x2000u, first_dependency);
    packet.append_ordered_op(OwnedFifoPacket::OrderedOp{
        .command_ordinal = 7u,
        .kind = OwnedFifoPacket::OpKind::PeToken,
        .token = 0x1234u,
        .interrupt = true,
    });
    packet.append_fifo(second_fifo);
    // A later command may read a lower address; encounter order still wins.
    packet.append_dependency(0x1000u, second_dependency);
    packet.append_ordered_op(OwnedFifoPacket::OrderedOp{
        .command_ordinal = 9u,
        .kind = OwnedFifoPacket::OpKind::XfbCopy,
        .xfb_dest_addr = 0x10ff8160u,
        .xfb_exec_command = 0x4000u,
    });
    packet.append_ordered_op(OwnedFifoPacket::OrderedOp{
        .command_ordinal = 11u,
        .kind = OwnedFifoPacket::OpKind::PeFinish,
    });
    packet.set_presentation({true, 0x10ff8160u});

    // The producer may reuse its buffers immediately after sealing.
    first_fifo.fill(std::byte{0});
    second_fifo.fill(std::byte{0});
    first_dependency.fill(std::byte{0});
    second_dependency.fill(std::byte{0});
    if (!expect(throws<std::logic_error>([&] { (void)packet.fifo(); }),
                "an unsealed packet exposed source bytes")) {
        return false;
    }
    packet.seal();

    const std::array<std::byte, 5> expected_fifo{
        std::byte{0x40}, std::byte{0x12}, std::byte{0x34},
        std::byte{0x61}, std::byte{0x7f}};
    const std::array<std::byte, 3> expected_first_dependency{
        std::byte{0xa1}, std::byte{0xb2}, std::byte{0xc3}};
    const auto fifo = packet.fifo();
    const auto ops = packet.ordered_ops();
    const auto deps = packet.dependencies();
    return expect(packet.sealed(), "packet was not sealed") &&
        expect(std::equal(fifo.begin(), fifo.end(), expected_fifo.begin(),
                          expected_fifo.end()),
               "sealed FIFO changed with the producer buffer") &&
        expect(deps.size() == 2u && deps[0].guest_base == 0x2000u &&
                   deps[0].size == 3u && deps[1].guest_base == 0x1000u &&
                   deps[1].size == 2u,
               "guest dependency identity changed") &&
        expect(std::equal(packet.dependency_bytes(0).begin(),
                          packet.dependency_bytes(0).end(),
                          expected_first_dependency.begin(),
                          expected_first_dependency.end()),
               "sealed dependency changed with guest memory") &&
        expect(ops.size() == 3u &&
                   ops[0].kind == OwnedFifoPacket::OpKind::PeToken &&
                   ops[0].command_ordinal == 7u &&
                   ops[0].token == 0x1234u && ops[0].interrupt &&
                   ops[1].kind == OwnedFifoPacket::OpKind::XfbCopy &&
                   ops[1].command_ordinal == 9u &&
                   ops[1].xfb_dest_addr == 0x10ff8160u &&
                   ops[1].xfb_exec_command == 0x4000u &&
                   ops[2].kind == OwnedFifoPacket::OpKind::PeFinish &&
                   ops[2].command_ordinal == 11u,
               "PE/XFB encounter order or payload changed") &&
        expect(packet.fifo_prefix(0).size() == 3u &&
                   packet.fifo_prefix(1).size() == 5u &&
                   packet.dependency_prefix(0).size() == 1u &&
                   packet.dependency_prefix(1).size() == 2u &&
                   ops[0].dependency_high_water == 3u &&
                   ops[1].dependency_high_water == 5u,
               "ordered op observed a later staging prefix") &&
        expect(packet.presentation().requested &&
                   packet.presentation().displayed_xfb_addr == 0x10ff8160u,
               "VI-selected XFB identity changed") &&
        expect(throws<std::logic_error>([&] { packet.append_fifo({}); }) &&
                   throws<std::logic_error>([&] { packet.seal(); }),
               "sealed packet accepted mutation");
}

bool rejects_ambiguous_or_unbounded_inputs() {
    OwnedFifoPacket::Limits limits{};
    limits.fifo_bytes = 3u;
    limits.dependency_bytes = 3u;
    limits.dependencies = 1u;
    limits.ordered_ops = 1u;
    OwnedFifoPacket packet(limits);
    const std::array<std::byte, 3> three{
        std::byte{1}, std::byte{2}, std::byte{3}};
    const std::array<std::byte, 1> one{std::byte{4}};

    packet.append_fifo(three);
    packet.append_dependency(0xfffffffdu, three);
    if (!expect(throws<std::length_error>([&] { packet.append_fifo(one); }),
                "FIFO limit did not fail closed") ||
        !expect(throws<std::invalid_argument>([&] {
                    packet.append_dependency(0xfffffffeu, one);
                }),
                "overlapping guest range was accepted") ||
        !expect(throws<std::invalid_argument>([&] {
                    packet.append_dependency(0x1000u, one);
                }),
                "dependency count limit was accepted")) {
        return false;
    }
    packet.append_ordered_op(OwnedFifoPacket::OrderedOp{
        .command_ordinal = 2u,
        .kind = OwnedFifoPacket::OpKind::PeFinish,
    });
    const bool bounded =
        expect(throws<std::invalid_argument>([&] {
                       packet.append_ordered_op(OwnedFifoPacket::OrderedOp{
                           .command_ordinal = 3u,
                           .kind = OwnedFifoPacket::OpKind::XfbCopy,
                       });
                   }),
               "ordered effect count limit was accepted") &&
        expect(throws<std::out_of_range>([&] {
                   packet.seal();
                   (void)packet.dependency_bytes(1u);
               }),
               "missing guest dependency did not fail closed");

    OwnedFifoPacket ordered;
    ordered.append_ordered_op(OwnedFifoPacket::OrderedOp{
        .command_ordinal = 2u,
        .kind = OwnedFifoPacket::OpKind::PeFinish,
    });
    return bounded &&
        expect(throws<std::invalid_argument>([&] {
                   ordered.append_ordered_op(OwnedFifoPacket::OrderedOp{
                       .command_ordinal = 1u,
                       .kind = OwnedFifoPacket::OpKind::XfbCopy,
                   });
               }),
               "nonmonotonic PE/XFB order was accepted") &&
        expect(throws<std::invalid_argument>([&] {
                   ordered.append_ordered_op(OwnedFifoPacket::OrderedOp{
                       .command_ordinal = 3u,
                       .kind = OwnedFifoPacket::OpKind::PeFinish,
                       .token = 1u,
                   });
               }),
               "invalid PE payload was accepted");
}

bool preserves_versioned_reads_and_effect_order() {
    std::array<std::byte, 2> version_a{
        std::byte{0xa1}, std::byte{0xa2}};
    std::array<std::byte, 2> version_b{
        std::byte{0xb1}, std::byte{0xb2}};
    OwnedFifoPacket packet;
    packet.append_versioned_dependency(4u, 1u, 0x2000u, version_a);
    packet.append_ordered_op(OwnedFifoPacket::OrderedOp{
        .command_ordinal = 5u,
        .kind = OwnedFifoPacket::OpKind::PeToken,
        .token = 0x77u,
        .interrupt = true,
    });
    packet.append_versioned_dependency(6u, 2u, 0x2000u, version_b);
    packet.append_ordered_op(OwnedFifoPacket::OrderedOp{
        .command_ordinal = 7u,
        .kind = OwnedFifoPacket::OpKind::XfbCopy,
        .xfb_dest_addr = 0x10ff8160u,
        .xfb_exec_command = 0x4000u,
    });
    packet.set_presentation({true, 0x10ff8160u});
    version_a.fill(std::byte{0});
    version_b.fill(std::byte{0});
    packet.seal();

    const std::array<std::byte, 2> expected_a{
        std::byte{0xa1}, std::byte{0xa2}};
    const std::array<std::byte, 2> expected_b{
        std::byte{0xb1}, std::byte{0xb2}};
    const auto deps = packet.dependencies();
    const auto ops = packet.ordered_ops();
    const auto read_a = packet.read_bytes_for_op(0);
    const auto read_b = packet.read_bytes_for_op(2);
    return expect(deps.size() == 2u &&
                      deps[0].guest_base == deps[1].guest_base &&
                      deps[0].guest_base == 0x2000u &&
                      deps[0].read_version == 1u &&
                      deps[1].read_version == 2u,
                  "same-address read versions lost their identity") &&
        expect(ops.size() == 4u &&
                   ops[0].kind == OwnedFifoPacket::OpKind::GuestRead &&
                   ops[0].command_ordinal == 4u &&
                   ops[0].read_version == 1u &&
                   ops[1].kind == OwnedFifoPacket::OpKind::PeToken &&
                   ops[1].token == 0x77u && ops[1].interrupt &&
                   ops[2].kind == OwnedFifoPacket::OpKind::GuestRead &&
                   ops[2].command_ordinal == 6u &&
                   ops[2].read_version == 2u &&
                   ops[3].kind == OwnedFifoPacket::OpKind::XfbCopy &&
                   ops[3].xfb_dest_addr == 0x10ff8160u,
               "read/PE/XFB encounter order changed") &&
        expect(std::equal(read_a.begin(), read_a.end(),
                          expected_a.begin(), expected_a.end()) &&
                   std::equal(read_b.begin(), read_b.end(),
                              expected_b.begin(), expected_b.end()),
               "sealed reads did not retain both byte versions") &&
        expect(packet.dependency_prefix(0).size() == 1u &&
                   packet.dependency_prefix(1).size() == 1u &&
                   packet.dependency_prefix(2).size() == 2u &&
                   ops[0].dependency_high_water == 2u &&
                   ops[2].dependency_high_water == 4u,
               "earlier operation observed a later read version") &&
        expect(packet.presentation().requested &&
                   packet.presentation().displayed_xfb_addr == 0x10ff8160u,
               "versioned reads changed presented XFB identity") &&
        expect(throws<std::out_of_range>([&] {
                   (void)packet.read_bytes_for_op(1u);
               }) &&
                   throws<std::logic_error>([&] {
                       (void)packet.dependency_bytes(1u);
                   }) &&
                   throws<std::logic_error>([&] {
                       packet.append_versioned_dependency(
                           8u, 3u, 0x2000u, expected_a);
                   }),
               "non-read accessor or sealed mutation was accepted");
}

bool rejects_wrong_replayed_or_unbounded_versions() {
    const std::array<std::byte, 2> bytes{
        std::byte{0x11}, std::byte{0x22}};
    OwnedFifoPacket packet;
    packet.append_versioned_dependency(2u, 1u, 0x3000u, bytes);
    if (!expect(throws<std::invalid_argument>([&] {
                    packet.append_versioned_dependency(
                        3u, 1u, 0x3000u, bytes);
                }), "replayed read version was accepted") ||
        !expect(throws<std::invalid_argument>([&] {
                    packet.append_versioned_dependency(
                        3u, 3u, 0x3000u, bytes);
                }), "skipped read version was accepted") ||
        !expect(throws<std::invalid_argument>([&] {
                    packet.append_versioned_dependency(
                        2u, 2u, 0x3000u, bytes);
                }), "replayed encounter ordinal was accepted") ||
        !expect(throws<std::invalid_argument>([&] {
                    packet.append_ordered_op(OwnedFifoPacket::OrderedOp{
                        .command_ordinal = 3u,
                        .kind = OwnedFifoPacket::OpKind::GuestRead,
                        .dependency_index = 0u,
                        .read_version = 1u,
                    });
                }), "read operation without atomic owned bytes was accepted") ||
        !expect(throws<std::invalid_argument>([&] {
                    packet.append_dependency(0x3000u, bytes);
                }), "versioned and legacy dependencies were mixed") ||
        !expect(throws<std::logic_error>([&] {
                    (void)packet.dependencies();
                }), "unsealed dependency data became visible")) {
        return false;
    }
    packet.append_versioned_dependency(3u, 2u, 0x3000u, bytes);
    packet.seal();
    if (!expect(packet.dependencies().size() == 2u &&
                    packet.ordered_ops().size() == 2u,
                "failed appends changed the sealed read stream")) {
        return false;
    }

    OwnedFifoPacket::Limits limits{};
    limits.dependency_bytes = 2u;
    limits.dependencies = 1u;
    limits.ordered_ops = 1u;
    OwnedFifoPacket bounded(limits);
    bounded.append_versioned_dependency(1u, 1u, 0x4000u, bytes);
    const std::array<std::byte, 1> one{std::byte{0x33}};
    OwnedFifoPacket::Limits byte_limits{};
    byte_limits.dependency_bytes = 2u;
    OwnedFifoPacket byte_bounded(byte_limits);
    byte_bounded.append_versioned_dependency(1u, 1u, 0x5000u, bytes);
    return expect(throws<std::invalid_argument>([&] {
                      bounded.append_versioned_dependency(
                          2u, 2u, 0x4000u, one);
                  }), "read count or operation count bound was bypassed") &&
        expect(throws<std::invalid_argument>([&] {
                   bounded.append_versioned_dependency(
                       2u, 2u, 0xffffffffu, bytes);
               }), "versioned guest address overflow was accepted") &&
        expect(throws<std::invalid_argument>([&] {
                   bounded.append_ordered_op(OwnedFifoPacket::OrderedOp{
                       .command_ordinal = 2u,
                       .kind = OwnedFifoPacket::OpKind::PeFinish,
                   });
               }), "versioned operation count bound was bypassed") &&
        expect(throws<std::length_error>([&] {
                   byte_bounded.append_versioned_dependency(
                       2u, 2u, 0x5000u, one);
               }), "versioned dependency byte bound was bypassed") &&
        expect(throws<std::invalid_argument>([&] {
                   OwnedFifoPacket legacy;
                   legacy.append_dependency(0x1000u, one);
                   legacy.append_versioned_dependency(
                       1u, 1u, 0x1000u, one);
               }), "legacy and versioned dependencies were mixed");
}

bool replays_real_capture_event_order_and_alias_versions() {
    using galaxy::gx::DependencyAliasKey;
    using galaxy::gx::DependencyReadSource;
    using galaxy::gx::OwnedDependencyEvents;
    const std::array<std::byte, 3> parser_input{
        std::byte{0x40}, std::byte{0x61}, std::byte{0x98}};
    const std::array<std::byte, 2> version_a{
        std::byte{0x10}, std::byte{0x11}};
    const std::array<std::byte, 2> version_b{
        std::byte{0x20}, std::byte{0x21}};
    const DependencyAliasKey alias{0x9000u, 128u, 64u, 3u};
    OwnedDependencyEvents captured;
    captured.guest_read(DependencyReadSource::IndexedVertex,
                        0x2000u, version_a);
    captured.alias_copy(alias, 0x1234u);
    captured.alias_bind(alias, 0x1234u);
    captured.pe_token(0x5au, true);
    captured.guest_read(DependencyReadSource::Texture,
                        0x2000u, version_b);
    captured.alias_retire(alias, 0x1234u);
    captured.alias_bind(alias, 0x5678u);  // Prior-frame GPU resource.
    captured.xfb_copy(0x10ff8160u, 0x4000u);
    captured.pe_finish();

    auto packet = galaxy::gx::capture_owned_fifo_events(
        parser_input, captured, {true, 0x10ff8160u});
    const auto replayed = galaxy::gx::replay_owned_fifo_events(packet);
    const auto ops = packet.ordered_ops();
    const auto dependencies = packet.dependencies();
    return expect(packet.sealed() && packet.fifo().size() == 3u &&
                      packet.presentation().displayed_xfb_addr ==
                          0x10ff8160u,
                  "real parse packet lost FIFO/presentation identity") &&
        expect(ops.size() == 9u && dependencies.size() == 2u &&
                   dependencies[0].guest_base == dependencies[1].guest_base &&
                   dependencies[0].read_version == 1u &&
                   dependencies[1].read_version == 2u &&
                   ops[0].read_source == DependencyReadSource::IndexedVertex &&
                   ops[4].read_source == DependencyReadSource::Texture &&
                   ops[1].alias_generation == 1u &&
                   ops[2].alias_generation == 1u &&
                   ops[5].alias_generation == 1u &&
                   ops[6].alias_generation == 0u,
               "ordered read sources or GPU alias versions changed") &&
        expect(galaxy::gx::owned_fifo_events_equal(captured, replayed) &&
                   replayed.has_external_resources(),
               "sealed packet could not replay exact event chronology") &&
        expect(throws<std::invalid_argument>([] {
                   OwnedFifoPacket invalid;
                   invalid.append_ordered_op(OwnedFifoPacket::OrderedOp{
                       .command_ordinal = 1u,
                       .kind = OwnedFifoPacket::OpKind::AliasCopy,
                       .alias_resource = 0x1234u,
                   });
               }), "alias copy without a generation was accepted");
}

}  // namespace

int main() {
    const bool ok = owned_bytes_and_ordered_effects() &&
        rejects_ambiguous_or_unbounded_inputs() &&
        preserves_versioned_reads_and_effect_order() &&
        rejects_wrong_replayed_or_unbounded_versions() &&
        replays_real_capture_event_order_and_alias_versions();
    if (ok) {
        std::cout << "owned GX FIFO packet tests passed\n";
        return 0;
    }
    return 1;
}
