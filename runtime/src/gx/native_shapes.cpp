// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/native_shapes.h"

#include "galaxy/gx/fifo_parser.h"
#include "galaxy/gx/gx_state.h"
#include "galaxy/windows_power.h"

#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace galaxy::gx::shapes {
namespace {

std::uint16_t read_u16(const std::byte* p) {
    return static_cast<std::uint16_t>((std::to_integer<unsigned>(p[0]) << 8u) | std::to_integer<unsigned>(p[1]));
}

const std::byte* resolve(GuestMemory* memory, std::uint32_t address, std::uint32_t size) {
    if (const std::byte* fast = resolve_guest_fast(memory, address, size)) {
        return fast;
    }
    for (std::uint32_t i = 0; memory != nullptr && i < memory->region_count; ++i) {
        const GuestMemoryRegion& region = memory->regions[i];
        if (address >= region.guest_base &&
            static_cast<std::uint64_t>(address - region.guest_base) + size <= region.size) {
            return region.host_base + (address - region.guest_base);
        }
    }
    return nullptr;
}

struct Registry {
    std::mutex lock;
    std::condition_variable work;
    std::condition_variable idle;
    std::deque<std::pair<Request, GuestMemory*>> jobs;
    std::vector<std::shared_ptr<const Shape>> shapes;
    std::vector<std::uint8_t> pending;
    std::size_t building = 0;
    bool worker = false;
};

// Never destroyed: the worker may still wait on it while the process exits.
Registry& registry() {
    static Registry* r = new Registry;
    return *r;
}

void worker() {
    galaxy::host::name_current_thread(L"Nebula Shape Builder");
    Registry& r = registry();
    for (;;) {
        std::pair<Request, GuestMemory*> job;
        {
            std::unique_lock guard(r.lock);
            r.work.wait(guard, [&r] { return !r.jobs.empty(); });
            job = std::move(r.jobs.front());
            r.jobs.pop_front();
        }
        std::shared_ptr<const Shape> shape;
        try {
            shape = build(job.first, job.second);
        } catch (...) {
            // A list the builder cannot read draws as GX would call it.
        }
        {
            std::lock_guard guard(r.lock);
            if (job.first.handle >= r.shapes.size()) {
                r.shapes.resize(job.first.handle + 1u);
                r.pending.resize(job.first.handle + 1u);
            }
            r.shapes[job.first.handle] = std::move(shape);
            r.pending[job.first.handle] = 0u;
            --r.building;
        }
        r.idle.notify_all();
    }
}

}  // namespace

Request make_request(std::uint32_t handle, std::uint32_t list_address, std::span<const std::byte> list,
                     const GxState& state) {
    Request request;
    request.handle = handle;
    request.list_address = list_address;
    request.list.assign(list.begin(), list.end());
    for (std::size_t i = 0; i < std::size(kRequestRegisters); ++i) {
        request.registers[i] = state.cp(kRequestRegisters[i]);
    }
    return request;
}

VertexState vertex_state(const GxState& state, std::uint8_t vtxfmt) {
    VertexState v{};
    v[0] = state.cp(cp::kVcdLo);
    v[1] = state.cp(cp::kVcdHi);
    v[2] = state.cp(static_cast<std::uint8_t>(cp::kVatABase + vtxfmt));
    v[3] = state.cp(static_cast<std::uint8_t>(cp::kVatBBase + vtxfmt));
    v[4] = state.cp(static_cast<std::uint8_t>(cp::kVatCBase + vtxfmt));
    for (unsigned a = 0; a < 12u; ++a) {
        v[5u + a] = state.array_base(a);
        v[17u + a] = state.array_stride(a);
    }
    v[29] = state.vertex_desc(vtxfmt).has_pn_matrix_index ? 0u : state.cp(cp::kMatrixIndexA) & 0x3Fu;
    return v;
}

std::shared_ptr<const Shape> build(const Request& request, GuestMemory* memory) {
    GxState state;
    for (std::size_t i = 0; i < std::size(kRequestRegisters); ++i) {
        state.load_cp(kRequestRegisters[i], request.registers[i]);
    }

    // The list's draws, grouped into runs of one primitive.
    const std::span<const std::byte> list(request.list);
    std::vector<std::pair<PrimitiveClass, std::vector<CachedDrawPacket>>> runs;
    std::uint8_t vtxfmt = 0xFFu;
    VertexDescriptor desc{};
    std::size_t source_size = 0;
    std::uint32_t draws = 0;
    std::size_t at = 0;
    while (at < list.size()) {
        const auto opcode = std::to_integer<std::uint8_t>(list[at]);
        if (opcode == 0x00u) {
            ++at;
            continue;
        }
        if ((opcode & 0x80u) == 0u || list.size() - at < 3u) return nullptr;
        const auto primitive = static_cast<PrimitiveClass>(opcode >> 3u);
        if (primitive == PrimitiveClass::Lines || primitive == PrimitiveClass::LineStrip ||
            primitive == PrimitiveClass::Points) {
            return nullptr;
        }
        const auto format = static_cast<std::uint8_t>(opcode & 0x07u);
        if (vtxfmt == 0xFFu) {
            vtxfmt = format;
            desc = state.vertex_desc(vtxfmt);
            source_size = VertexLoader::source_vertex_size(desc);
        } else if (format != vtxfmt) {
            return nullptr;
        }
        const std::uint16_t count = read_u16(list.data() + at + 1u);
        const std::size_t payload = source_size * count;
        if (list.size() - at - 3u < payload) return nullptr;
        if (runs.empty() || runs.back().first != primitive) runs.emplace_back(primitive, std::vector<CachedDrawPacket>{});
        runs.back().second.push_back(CachedDrawPacket{opcode, count, at, at + 1u, payload});
        at += 3u + payload;
        ++draws;
    }
    if (runs.empty()) return nullptr;

    auto shape = std::make_shared<Shape>();
    shape->list_address = request.list_address;
    shape->list_bytes = static_cast<std::uint32_t>(request.list.size());
    shape->vtxfmt = vtxfmt;
    shape->draws = draws;
    shape->state = vertex_state(state, vtxfmt);
    VertexLoader loader;
    std::vector<GxVertexOut> vertices;
    std::vector<std::uint32_t> triangles;
    std::vector<std::uint16_t> run_triangles;
    for (const auto& [primitive, packets] : runs) {
        DecodedPacketRunVertices decoded = loader.decode_cached_packet_run_vertices_with_layout(
            list, 0u, packets, primitive, state, desc, source_size, memory, 0u, 0u, true);
        if (decoded.vertices.size() > 0xFFFFu) return nullptr;
        VertexLoader::cached_packet_run_triangles(static_cast<std::uint32_t>(decoded.vertices.size()), 0u, packets,
                                                  primitive, decoded.total_indices, {}, run_triangles);
        const auto base = static_cast<std::uint32_t>(vertices.size());
        for (const std::uint16_t i : run_triangles) {
            if (i >= decoded.vertices.size()) return nullptr;
            triangles.push_back(base + i);
        }
        vertices.insert(vertices.end(), decoded.vertices.begin(), decoded.vertices.end());
        shape->reads.insert(shape->reads.end(), decoded.guest_array_reads.begin(), decoded.guest_array_reads.end());
    }

    // A vertex the list sends again (a strip's neighbour, a corner shared by two strips) is one vertex: equal bytes
    // are the same shader inputs.
    std::unordered_map<std::string_view, std::uint16_t> distinct;
    distinct.reserve(vertices.size());
    std::vector<std::uint16_t> remap(vertices.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const std::string_view bytes(reinterpret_cast<const char*>(&vertices[i]), sizeof(GxVertexOut));
        const auto [it, added] = distinct.try_emplace(bytes, static_cast<std::uint16_t>(shape->vertices.size()));
        if (added) {
            if (shape->vertices.size() > 0xFFFFu) return nullptr;
            shape->vertices.push_back(vertices[i]);
        }
        remap[i] = it->second;
    }
    shape->indices.reserve(triangles.size());
    for (const std::uint32_t i : triangles) shape->indices.push_back(remap[i]);
    if (shape->indices.empty()) return nullptr;
    for (const VertexDecodeGuestRange& read : shape->reads) {
        const std::byte* bytes = resolve(memory, read.guest_base, read.size);
        if (bytes == nullptr) return nullptr;
        shape->read_bytes.insert(shape->read_bytes.end(), bytes, bytes + read.size);
    }
    return shape;
}

void register_later(Request request, GuestMemory* memory) {
    Registry& r = registry();
    {
        std::lock_guard guard(r.lock);
        if (request.handle >= r.shapes.size()) {
            r.shapes.resize(request.handle + 1u);
            r.pending.resize(request.handle + 1u);
        }
        r.shapes[request.handle] = nullptr;
        r.pending[request.handle] = 1u;
        r.jobs.emplace_back(std::move(request), memory);
        ++r.building;
        if (!r.worker) {
            r.worker = true;
            std::thread(worker).detach();
        }
    }
    r.work.notify_one();
}

std::shared_ptr<const Shape> find(std::uint32_t handle, bool& pending) {
    Registry& r = registry();
    std::lock_guard guard(r.lock);
    pending = handle < r.pending.size() && r.pending[handle] != 0u;
    return handle < r.shapes.size() ? r.shapes[handle] : nullptr;
}

void wait_idle() {
    Registry& r = registry();
    std::unique_lock guard(r.lock);
    r.idle.wait(guard, [&r] { return r.building == 0u; });
}

}  // namespace galaxy::gx::shapes
