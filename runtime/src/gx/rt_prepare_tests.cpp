// SPDX-License-Identifier: GPL-3.0-only
// The ray tracing pipeline library's header (rt_prepare.h): written, read back, and what it covers.
#include "galaxy/gx/rt_prepare.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using galaxy::gx::rt_prepare::format_header;
using galaxy::gx::rt_prepare::Header;
using galaxy::gx::rt_prepare::header_covers;
using galaxy::gx::rt_prepare::Identity;
using galaxy::gx::rt_prepare::kHeaderBytes;
using galaxy::gx::rt_prepare::parse_header;

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

Header sample() {
    Header header;
    header.shaders = 0x0123456789ABCDEFull;
    header.adapter = "10de:2b85:89ef1043:a1";
    header.driver = "32.0.16.1088";
    header.complete = true;
    return header;
}

bool round_trip() {
    bool ok = true;
    const std::vector<std::uint8_t> bytes = format_header(sample(), 6431316u);
    ok &= expect(bytes.size() == kHeaderBytes, "the header's size");
    Header read;
    std::uint64_t blob = 0u;
    ok &= expect(parse_header(bytes, read, blob), "a written header reads back");
    ok &= expect(read.shaders == sample().shaders && read.adapter == sample().adapter &&
                     read.driver == sample().driver && read.complete && blob == 6431316u,
                 "every field reads back");
    return ok;
}

bool refusals() {
    bool ok = true;
    Header read;
    std::uint64_t blob = 0u;
    std::vector<std::uint8_t> bytes = format_header(sample(), 1u);
    ok &= expect(!parse_header(std::vector<std::uint8_t>(bytes.begin(), bytes.end() - 1), read, blob), "a short header");
    bytes[0] ^= 0xFFu;
    ok &= expect(!parse_header(bytes, read, blob), "another file's magic");
    bytes = format_header(sample(), 1u);
    bytes[4] = 9u;
    ok &= expect(!parse_header(bytes, read, blob), "another version");
    Header long_name = sample();
    long_name.adapter = std::string(80u, 'a');
    ok &= expect(parse_header(format_header(long_name, 1u), read, blob) && read.adapter.size() == 39u,
                 "a long name is cut to its field");
    return ok;
}

bool coverage() {
    bool ok = true;
    const Identity identity{"10de:2b85:89ef1043:a1", "32.0.16.1088"};
    const Header header = sample();
    ok &= expect(header_covers(header, header.shaders, identity), "the same shaders, adapter and driver");
    ok &= expect(!header_covers(header, header.shaders + 1u, identity), "other shaders");
    ok &= expect(!header_covers(header, header.shaders, Identity{"1002:73bf:0e3a1002:c1", identity.driver}),
                 "another adapter");
    ok &= expect(!header_covers(header, header.shaders, Identity{identity.adapter, "32.0.16.1100"}), "another driver");
    ok &= expect(!header_covers(header, header.shaders, Identity{}), "an unknown adapter");
    Header incomplete = header;
    incomplete.complete = false;
    ok &= expect(!header_covers(incomplete, header.shaders, identity), "an incomplete library");
    return ok;
}

}  // namespace

int main() {
    bool ok = round_trip();
    ok &= refusals();
    ok &= coverage();
    std::cout << (ok ? "rt_prepare tests passed\n" : "rt_prepare tests FAILED\n");
    return ok ? 0 : 1;
}
