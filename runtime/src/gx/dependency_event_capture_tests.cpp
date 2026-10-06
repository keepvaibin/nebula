#include "galaxy/gx/dependency_event_capture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>

using galaxy::gx::DependencyAliasKey;
using galaxy::gx::DependencyReadSource;
using galaxy::gx::OwnedDependencyEvents;

namespace {

bool expect(bool value, const char* message) {
    if (!value) std::cerr << "FAIL: " << message << '\n';
    return value;
}

bool repeated_address_versions_and_order() {
    OwnedDependencyEvents capture;
    std::array<std::byte, 2> guest{std::byte{0x12}, std::byte{0x34}};
    capture.guest_read(DependencyReadSource::Parser, 0x1000u, guest);
    capture.pe_token(0x4321u, true);
    guest[0] = std::byte{0x56};
    capture.guest_read(DependencyReadSource::IndexedVertex, 0x1000u, guest);
    capture.xfb_copy(0x180000u, 0x4000u);
    capture.pe_finish();

    const auto events = capture.events();
    const auto first = capture.read_bytes(events[0]);
    const auto second = capture.read_bytes(events[2]);
    return expect(events.size() == 5u, "event count") &&
        expect(events[0].ordinal == 1u && events[1].ordinal == 2u &&
                   events[2].ordinal == 3u && events[3].ordinal == 4u &&
                   events[4].ordinal == 5u, "ordered event identity") &&
        expect(first[0] == std::byte{0x12} &&
                   second[0] == std::byte{0x56},
               "same address lost distinct byte versions") &&
        expect(events[1].kind == OwnedDependencyEvents::Kind::PeToken &&
                   events[1].token == 0x4321u && events[1].interrupt &&
                   events[3].kind == OwnedDependencyEvents::Kind::XfbCopy &&
                   events[3].guest_addr == 0x180000u &&
                   events[4].kind == OwnedDependencyEvents::Kind::PeFinish,
               "PE and XFB order or payload");
}

bool alias_reuse_generation_and_retirement() {
    OwnedDependencyEvents capture;
    const DependencyAliasKey water{0x2000u, 640u, 456u, 6u};
    constexpr std::uintptr_t resource = 0xABCDu;
    capture.alias_copy(water, resource);
    capture.alias_bind(water, resource);
    capture.alias_copy(water, resource);  // Same GPU resource, new pixels.
    capture.alias_bind(water, resource);
    capture.alias_retire(water, resource);
    capture.alias_bind(water, resource);  // An external/preexisting binding.

    const auto events = capture.events();
    return expect(events.size() == 6u, "alias event count") &&
        expect(events[0].generation != 0u &&
                   events[1].generation == events[0].generation &&
                   events[2].generation > events[0].generation &&
                   events[3].generation == events[2].generation &&
                   events[4].generation == events[2].generation,
               "same-resource EFB copy did not advance generation") &&
        expect(events[5].generation == 0u &&
                   capture.has_external_resources(),
               "retired alias was accepted as replayable");
}

bool bounded_capture_fails_closed() {
    OwnedDependencyEvents capture({.events = 1u, .bytes = 2u});
    const std::array<std::byte, 2> bytes{std::byte{1}, std::byte{2}};
    capture.guest_read(DependencyReadSource::Texture, 0x3000u, bytes);
    bool rejected = false;
    try {
        capture.guest_read(DependencyReadSource::Tlut, 0x3000u, bytes);
    } catch (const std::length_error&) {
        rejected = true;
    }
    return expect(rejected && capture.events().size() == 1u &&
                      capture.captured_bytes() == 2u,
                  "capture limit mutated prior bytes or events");
}

}  // namespace

int main() {
    if (repeated_address_versions_and_order() &&
        alias_reuse_generation_and_retirement() &&
        bounded_capture_fails_closed()) {
        std::cout << "GX dependency event capture tests passed\n";
        return 0;
    }
    return 1;
}
