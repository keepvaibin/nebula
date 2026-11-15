// SPDX-License-Identifier: GPL-3.0-only
// gpu::Encoder on Direct3D 11 (core/gpu_encoder.h): the changes it lets through, as DrawOps in the open pass's stream.
#include "render/core/draw_record.h"

#include <cstring>

namespace galaxy::render::gpu {

namespace {

DrawOp& next(std::vector<DrawOp>* ops, DrawOp::Kind kind) {
    DrawOp& op = ops->emplace_back();
    op.kind = kind;
    return op;
}

}  // namespace

void Encoder::emit_pipeline(Pipeline* pipeline) noexcept {
    next(ops_, DrawOp::Kind::Pipeline).a[0] = reinterpret_cast<std::uintptr_t>(pipeline);
}

void Encoder::emit_view(Address view) noexcept {
    next(ops_, DrawOp::Kind::View).a[0] = view;
}

void Encoder::emit_material(Address vertex_rows, Address pixel_rows) noexcept {
    DrawOp& op = next(ops_, DrawOp::Kind::Material);
    op.a[0] = vertex_rows;
    op.a[1] = pixel_rows;
}

void Encoder::emit_root(const DrawRoot& root) noexcept {
    std::memcpy(next(ops_, DrawOp::Kind::Root).w, &root, sizeof(root));
}

void Encoder::emit_tables(Table textures, Table samplers) noexcept {
    DrawOp& op = next(ops_, DrawOp::Kind::Tables);
    op.a[0] = textures.ptr;
    op.a[1] = samplers.ptr;
}

void Encoder::emit_rows(Address rows, Address instances) noexcept {
    DrawOp& op = next(ops_, DrawOp::Kind::Rows);
    op.a[0] = rows;
    op.a[1] = instances;
}

void Encoder::emit_scissor(const Rect& rect) noexcept {
    std::memcpy(next(ops_, DrawOp::Kind::Scissor).w, &rect, sizeof(rect));
}

void Encoder::emit_viewport(const ViewportRect& rect) noexcept {
    std::memcpy(next(ops_, DrawOp::Kind::Viewport).w, &rect, sizeof(rect));
}

void Encoder::emit_stream(unsigned slot, const StreamView& view) noexcept {
    DrawOp& op = next(ops_, DrawOp::Kind::Stream);
    op.slot = static_cast<std::uint8_t>(slot);
    op.a[0] = view.address;
    op.w[0] = view.bytes;
    op.w[1] = view.stride;
}

void Encoder::emit_indices(Address address, std::uint32_t bytes, std::uint32_t index_bytes) noexcept {
    DrawOp& op = next(ops_, DrawOp::Kind::Indices);
    op.a[0] = address;
    op.w[0] = bytes;
    op.w[1] = index_bytes;
}

void Encoder::draw_indexed(std::uint32_t count, std::uint32_t instances, std::uint32_t first, std::int32_t base_vertex) noexcept {
    DrawOp& op = next(ops_, DrawOp::Kind::DrawIndexed);
    op.w[0] = count;
    op.w[1] = instances;
    op.w[2] = first;
    op.w[3] = static_cast<std::uint32_t>(base_vertex);
}

void Encoder::draw(std::uint32_t count, std::uint32_t instances, std::uint32_t first) noexcept {
    DrawOp& op = next(ops_, DrawOp::Kind::Draw);
    op.w[0] = count;
    op.w[1] = instances;
    op.w[2] = first;
}

}  // namespace galaxy::render::gpu
