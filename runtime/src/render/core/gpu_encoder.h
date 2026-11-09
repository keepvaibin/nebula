// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// What the walker records a frame's draws through. One class per backend, chosen when the backend is built; every call
// compares against what is bound.

#include "galaxy/render/gpu_types.h"

#include <cstdint>

namespace galaxy::render::gpu {

// Vertex buffer slots. Registered meshes use 0 to 3; a ring or site vertex is one stream at 0; 4 reads zero.
enum StreamSlot : unsigned {
    kStreamPosition = 0u,
    kStreamAttributes = 1u,
    kStreamMatrix = 2u,
    kStreamFurther = 3u,
    kStreamZero = 4u,
    kStreamBefore = 5u,  // a site's vertices of the frame before, for motion
    kStreamCount = 6u,
};

struct StreamView {
    Address address = 0u;
    std::uint32_t bytes = 0u;
    std::uint32_t stride = 0u;
};

// b3: what a draw tells its programs directly.
struct DrawRoot {
    std::uint32_t first_instance = 0u;
    std::uint32_t flags = 0u;
    float half_line = 0.0f;
    float half_point = 0.0f;
    std::uint32_t table = 0u;  // the draw's first entry in the frame's texture lists (draw_tables)
};

inline constexpr unsigned kDrawRootWords = sizeof(DrawRoot) / 4u;
static_assert(sizeof(DrawRoot) == 20u);

struct ViewportRect {
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f, min_depth = 0.0f, max_depth = 1.0f;
    [[nodiscard]] bool operator==(const ViewportRect&) const noexcept = default;
};

}  // namespace galaxy::render::gpu

#if !defined(NEBULA_GPU_VULKAN)
#include <d3d12.h>

namespace galaxy::render::gpu {

class Encoder {
public:
    void attach(ID3D12GraphicsCommandList* list) noexcept {
        list_ = list;
        reset();
    }
    // Other code bound things: nothing is taken as bound any more.
    void reset() noexcept {
        pipeline_ = nullptr;
        view_ = vertex_rows_ = pixel_rows_ = rows_ = instances_ = lists_ = 0u;
        textures_ = samplers_ = 0u;
        root_valid_ = scissor_valid_ = viewport_valid_ = topology_set_ = index_valid_ = false;
        for (StreamView& s : streams_) s = StreamView{};
    }
    // Other code set the viewport and scissor.
    void forget_rects() noexcept { scissor_valid_ = viewport_valid_ = false; }
    void set_pipeline(Pipeline* pipeline) noexcept {
        if (pipeline != pipeline_) {
            list_->SetPipelineState(pipeline);
            pipeline_ = pipeline;
        }
    }
    void set_view(Address view) noexcept {
        if (view != view_) {
            list_->SetGraphicsRootConstantBufferView(0u, view);
            view_ = view;
        }
    }
    void set_material(Address vertex_rows, Address pixel_rows) noexcept {
        if (vertex_rows != vertex_rows_) {
            list_->SetGraphicsRootConstantBufferView(1u, vertex_rows);
            vertex_rows_ = vertex_rows;
        }
        if (pixel_rows != pixel_rows_) {
            list_->SetGraphicsRootConstantBufferView(2u, pixel_rows);
            pixel_rows_ = pixel_rows;
        }
    }
    void set_root(const DrawRoot& root) noexcept {
        if (!root_valid_ || root.first_instance != root_.first_instance || root.flags != root_.flags ||
            root.half_line != root_.half_line || root.half_point != root_.half_point || root.table != root_.table) {
            list_->SetGraphicsRoot32BitConstants(3u, kDrawRootWords, &root, 0u);
            root_ = root;
            root_valid_ = true;
        }
    }
    // Every texture and every sampler (the heaps' starts; the heaps themselves are the caller's, DescriptorRing::bind) and
    // the frame's texture lists: bound once per list, read through each draw's DrawRoot::table.
    void set_texture_arrays(Table textures, Table samplers, Address lists) noexcept {
        if (textures.ptr != textures_) {
            list_->SetGraphicsRootDescriptorTable(4u, textures);
            textures_ = textures.ptr;
        }
        if (samplers.ptr != samplers_) {
            list_->SetGraphicsRootDescriptorTable(5u, samplers);
            samplers_ = samplers.ptr;
        }
        if (lists != lists_) {
            list_->SetGraphicsRootShaderResourceView(8u, lists);
            lists_ = lists;
        }
    }
    void set_rows(Address rows, Address instances) noexcept {
        if (rows != rows_) {
            list_->SetGraphicsRootShaderResourceView(6u, rows);
            rows_ = rows;
        }
        if (instances != instances_) {
            list_->SetGraphicsRootShaderResourceView(7u, instances);
            instances_ = instances;
        }
    }
    void set_scissor(const Rect& rect) noexcept {
        if (!scissor_valid_ || rect.left != scissor_.left || rect.top != scissor_.top || rect.right != scissor_.right ||
            rect.bottom != scissor_.bottom) {
            list_->RSSetScissorRects(1u, &rect);
            scissor_ = rect;
            scissor_valid_ = true;
        }
    }
    void set_viewport(const ViewportRect& rect) noexcept {
        if (!viewport_valid_ || !(rect == viewport_)) {
            const D3D12_VIEWPORT vp{rect.x, rect.y, rect.width, rect.height, rect.min_depth, rect.max_depth};
            list_->RSSetViewports(1u, &vp);
            viewport_ = rect;
            viewport_valid_ = true;
        }
    }
    void set_stream(unsigned slot, const StreamView& view) noexcept {
        StreamView& bound = streams_[slot];
        if (view.address != bound.address || view.bytes != bound.bytes || view.stride != bound.stride) {
            const D3D12_VERTEX_BUFFER_VIEW vb{view.address, view.bytes, view.stride};
            list_->IASetVertexBuffers(slot, 1u, &vb);
            bound = view;
        }
    }
    void set_indices(Address address, std::uint32_t bytes, std::uint32_t index_bytes) noexcept {
        if (!index_valid_ || address != index_address_ || bytes != index_size_ || index_bytes != index_stride_) {
            const D3D12_INDEX_BUFFER_VIEW ib{address, bytes, index_bytes == 2u ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT};
            list_->IASetIndexBuffer(&ib);
            index_address_ = address;
            index_size_ = bytes;
            index_stride_ = index_bytes;
            index_valid_ = true;
        }
    }
    void draw_indexed(std::uint32_t count, std::uint32_t instances, std::uint32_t first, std::int32_t base_vertex) noexcept {
        prepare_topology();
        list_->DrawIndexedInstanced(count, instances, first, base_vertex, 0u);
    }
    void draw(std::uint32_t count, std::uint32_t instances, std::uint32_t first) noexcept {
        prepare_topology();
        list_->DrawInstanced(count, instances, first, 0u);
    }

private:
    void prepare_topology() noexcept {
        if (!topology_set_) {
            list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            topology_set_ = true;
        }
    }
    ID3D12GraphicsCommandList* list_ = nullptr;
    Pipeline* pipeline_ = nullptr;
    Address view_ = 0u, vertex_rows_ = 0u, pixel_rows_ = 0u, rows_ = 0u, instances_ = 0u, lists_ = 0u;
    std::uint64_t textures_ = 0u, samplers_ = 0u;
    DrawRoot root_{};
    Rect scissor_{};
    ViewportRect viewport_{};
    StreamView streams_[kStreamCount]{};
    Address index_address_ = 0u;
    std::uint32_t index_size_ = 0u, index_stride_ = 0u;
    bool root_valid_ = false, scissor_valid_ = false, viewport_valid_ = false, topology_set_ = false, index_valid_ = false;
};

}  // namespace galaxy::render::gpu
#else
#include <volk.h>

#include <array>

namespace galaxy::render::gpu {

// Implemented over the renderer's draw layout (renderer_vk.cpp): set 0 the dynamic constants, the row buffers and (with
// descriptor indexing) the frame's texture lists, set 1 the textures, set 2 the samplers (every one, or one draw's bound
// table), push constants b3. `Address` is the renderer's address space; `resolve` turns one into a buffer and an offset.
class Encoder {
public:
    struct Layout {
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkDescriptorSet (*constants_set)(void* renderer, const VkBuffer* buffers, Address rows, Address instances,
                                         Address lists) = nullptr;
        void (*resolve)(void* renderer, Address address, VkBuffer& buffer, VkDeviceSize& offset) = nullptr;
        void* renderer = nullptr;
    };
    void configure(const Layout& layout) noexcept { layout_ = layout; }
    void attach(VkCommandBuffer commands) noexcept {
        commands_ = commands;
        reset();
    }
    // Nothing is taken as bound any more. The rows and instances asked for stay: a draw pass names them once, and the
    // renderer's own work inside it (a held screen probe read when the scene pass breaks) resets the bindings.
    void reset() noexcept {
        pipeline_ = VK_NULL_HANDLE;
        set0_ = VK_NULL_HANDLE;
        offsets_valid_ = false;
        view_ = vertex_rows_ = pixel_rows_ = lists_ = 0u;
        bound_view_ = bound_vertex_rows_ = bound_pixel_rows_ = bound_rows_ = bound_instances_ = bound_lists_ = 0u;
        view_buffer_ = vertex_buffer_ = pixel_buffer_ = VK_NULL_HANDLE;
        textures_ = samplers_ = 0u;
        root_valid_ = scissor_valid_ = viewport_valid_ = index_valid_ = false;
        for (StreamView& s : streams_) s = StreamView{};
    }
    // Other code set the viewport and scissor.
    void forget_rects() noexcept { scissor_valid_ = viewport_valid_ = false; }
    void set_pipeline(Pipeline* pipeline) noexcept {
        const VkPipeline value = reinterpret_cast<VkPipeline>(pipeline);
        if (value != pipeline_) {
            vkCmdBindPipeline(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, value);
            pipeline_ = value;
        }
    }
    void set_view(Address view) noexcept {
        view_ = view;
        offsets_valid_ = offsets_valid_ && (view == bound_view_ || view == 0u);
    }
    void set_material(Address vertex_rows, Address pixel_rows) noexcept {
        vertex_rows_ = vertex_rows;
        pixel_rows_ = pixel_rows;
        offsets_valid_ = offsets_valid_ && (vertex_rows == bound_vertex_rows_ || vertex_rows == 0u) &&
                         (pixel_rows == bound_pixel_rows_ || pixel_rows == 0u);
    }
    void set_root(const DrawRoot& root) noexcept {
        if (!root_valid_ || root.first_instance != root_.first_instance || root.flags != root_.flags ||
            root.half_line != root_.half_line || root.half_point != root_.half_point || root.table != root_.table) {
            vkCmdPushConstants(commands_, layout_.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0u,
                               sizeof(DrawRoot), &root);
            root_ = root;
            root_valid_ = true;
        }
    }
    void set_tables(Table textures, Table samplers) noexcept {
        if (textures.ptr != textures_) {
            const VkDescriptorSet set = reinterpret_cast<VkDescriptorSet>(textures.ptr);
            vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_.layout, 1u, 1u, &set, 0u, nullptr);
            textures_ = textures.ptr;
        }
        if (samplers.ptr != samplers_) {
            const VkDescriptorSet set = reinterpret_cast<VkDescriptorSet>(samplers.ptr);
            vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_.layout, 2u, 1u, &set, 0u, nullptr);
            samplers_ = samplers.ptr;
        }
    }
    // Every texture and every sampler (the bindless sets, bound once per list of bindings) and the frame's texture lists,
    // read through each draw's DrawRoot::table (the lists join set 0).
    void set_texture_arrays(Table textures, Table samplers, Address lists) noexcept {
        set_tables(textures, samplers);
        lists_ = lists;
        offsets_valid_ = offsets_valid_ && lists == bound_lists_;
    }
    void set_rows(Address rows, Address instances) noexcept {
        rows_ = rows;
        instances_ = instances;
        offsets_valid_ = offsets_valid_ && rows == bound_rows_ && instances == bound_instances_;
    }
    void set_scissor(const Rect& rect) noexcept {
        if (!scissor_valid_ || rect.left != scissor_.left || rect.top != scissor_.top || rect.right != scissor_.right ||
            rect.bottom != scissor_.bottom) {
            const VkRect2D area{{rect.left, rect.top},
                                {static_cast<std::uint32_t>(rect.right - rect.left),
                                 static_cast<std::uint32_t>(rect.bottom - rect.top)}};
            vkCmdSetScissor(commands_, 0u, 1u, &area);
            scissor_ = rect;
            scissor_valid_ = true;
        }
    }
    void set_viewport(const ViewportRect& rect) noexcept {
        if (!viewport_valid_ || !(rect == viewport_)) {
            // y flipped by a negative height, so clip space matches D3D's.
            const VkViewport vp{rect.x, rect.y + rect.height, rect.width, -rect.height, rect.min_depth, rect.max_depth};
            vkCmdSetViewport(commands_, 0u, 1u, &vp);
            viewport_ = rect;
            viewport_valid_ = true;
        }
    }
    void set_stream(unsigned slot, const StreamView& view) noexcept;
    void set_indices(Address address, std::uint32_t bytes, std::uint32_t index_bytes) noexcept;
    void draw_indexed(std::uint32_t count, std::uint32_t instances, std::uint32_t first, std::int32_t base_vertex) noexcept {
        flush_constants();
        vkCmdDrawIndexed(commands_, count, instances, first, base_vertex, 0u);
    }
    void draw(std::uint32_t count, std::uint32_t instances, std::uint32_t first) noexcept {
        flush_constants();
        vkCmdDraw(commands_, count, instances, first, 0u);
    }

private:
    void flush_constants() noexcept;
    Layout layout_{};
    VkCommandBuffer commands_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorSet set0_ = VK_NULL_HANDLE;
    std::array<VkBuffer, 3> set_buffers_{};
    VkBuffer view_buffer_ = VK_NULL_HANDLE, vertex_buffer_ = VK_NULL_HANDLE, pixel_buffer_ = VK_NULL_HANDLE;
    std::array<std::uint32_t, 3> offsets_{};
    bool offsets_valid_ = false;
    Address view_ = 0u, vertex_rows_ = 0u, pixel_rows_ = 0u, rows_ = 0u, instances_ = 0u, lists_ = 0u;
    Address bound_view_ = 0u, bound_vertex_rows_ = 0u, bound_pixel_rows_ = 0u, bound_rows_ = 0u, bound_instances_ = 0u,
            bound_lists_ = 0u;
    std::uint64_t textures_ = 0u, samplers_ = 0u;
    DrawRoot root_{};
    Rect scissor_{};
    ViewportRect viewport_{};
    StreamView streams_[kStreamCount]{};
    Address index_address_ = 0u;
    std::uint32_t index_size_ = 0u, index_stride_ = 0u;
    bool root_valid_ = false, scissor_valid_ = false, viewport_valid_ = false, index_valid_ = false;
};

}  // namespace galaxy::render::gpu
#endif
