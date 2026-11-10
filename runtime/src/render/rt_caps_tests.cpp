// SPDX-License-Identifier: GPL-3.0-only
// The ray tracing capability record (rt_caps.h): a device traces only when it reports everything the tracer needs, each
// missing piece has its own reason, unified memory refuses nothing, the stored record reads back as set, and a Direct3D 12
// build holds the tracer's shaders.
#include "galaxy/render/rt/rt_shaders.h"
#include "galaxy/render/rt_caps.h"

#include <cstring>
#include <iostream>

namespace {

using galaxy::render::RtCaps;

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

RtCaps full() {
    RtCaps caps;
    caps.ray_queries = caps.acceleration_structures = caps.bindless = caps.shaders = caps.enabled = true;
    return caps;
}

bool each_missing_piece_refuses() {
    bool ok = expect(galaxy::render::rt_capable(full()), "a device with everything traces");
    ok &= expect(!galaxy::render::rt_capable(RtCaps{}), "a device with nothing does not");
    bool RtCaps::* const pieces[] = {&RtCaps::ray_queries, &RtCaps::acceleration_structures, &RtCaps::bindless, &RtCaps::shaders,
                                     &RtCaps::enabled};
    const char* reasons[5]{};
    for (unsigned i = 0; i < 5u; ++i) {
        RtCaps caps = full();
        caps.*pieces[i] = false;
        reasons[i] = galaxy::render::rt_refusal(caps);
        ok &= expect(reasons[i] != nullptr && !galaxy::render::rt_capable(caps), "a missing piece refuses");
    }
    for (unsigned i = 0; ok && i < 5u; ++i) {
        for (unsigned j = i + 1u; j < 5u; ++j) {
            ok &= expect(std::strcmp(reasons[i], reasons[j]) != 0, "each missing piece has its own reason");
        }
    }
    return ok;
}

bool unified_memory_and_micromaps_refuse_nothing() {
    RtCaps caps = full();
    caps.unified_memory = true;
    bool ok = expect(galaxy::render::rt_capable(caps), "an integrated GPU that reports ray queries traces");
    caps.micromaps = true;
    ok &= expect(galaxy::render::rt_capable(caps), "micromaps are optional");
    return ok;
}

bool the_record_reads_back() {
    bool ok = true;
    for (unsigned bits = 0; bits < 128u; ++bits) {
        RtCaps caps;
        caps.ray_queries = (bits & 1u) != 0u;
        caps.acceleration_structures = (bits & 2u) != 0u;
        caps.bindless = (bits & 4u) != 0u;
        caps.shaders = (bits & 8u) != 0u;
        caps.micromaps = (bits & 16u) != 0u;
        caps.unified_memory = (bits & 32u) != 0u;
        caps.enabled = (bits & 64u) != 0u;
        galaxy::render::set_rt_caps(caps);
        const RtCaps back = galaxy::render::rt_caps();
        ok &= back.ray_queries == caps.ray_queries && back.acceleration_structures == caps.acceleration_structures &&
              back.bindless == caps.bindless && back.shaders == caps.shaders && back.micromaps == caps.micromaps &&
              back.unified_memory == caps.unified_memory && back.enabled == caps.enabled;
    }
    galaxy::render::set_rt_caps(RtCaps{});
    return expect(ok, "every record reads back as it was set");
}

bool the_build_holds_the_shaders() {
    const galaxy::render::RtShaderBytes probe = galaxy::render::rt_shader(galaxy::render::RtShader::Probe);
    bool ok = expect(galaxy::render::rt_shaders_present(), "a Direct3D 12 build holds the tracer's shaders");
    ok &= expect(probe.size > 4u && std::memcmp(probe.data, "DXBC", 4u) == 0, "the measuring shader is a DXIL container");
    ok &= expect(galaxy::render::rt_shader(galaxy::render::RtShader::Count).size == 0u, "a number past the table is empty");
    return ok;
}

}  // namespace

int main() {
    bool ok = each_missing_piece_refuses();
    ok &= unified_memory_and_micromaps_refuse_nothing();
    ok &= the_record_reads_back();
    ok &= the_build_holds_the_shaders();
    if (ok) {
        std::cout << "rt caps tests passed\n";
    }
    return ok ? 0 : 1;
}
