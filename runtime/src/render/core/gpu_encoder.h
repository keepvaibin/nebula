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

#if defined(NEBULA_GPU_METAL)
#include "galaxy/render/mtl/draw_types.h"

namespace galaxy::render::gpu {

// Implemented over Metal's render command encoder (mtl/encoder_mtl.mm). A draw's buffers sit at shader_msl.h's indices
// in the vertex and the fragment function: the view (0), the material's vertex rows (1) and pixel rows (2), the
// composed rows (3), the instances (4), the frame's texture lists (5), b3 as inline bytes (6), every texture and every
// sampler (7, 8) or the draw's own table of sixteen, and the vertex streams from 10. `Address` is the renderer's address
// space (mtl::Context::resolve). The comparisons against what is bound are here; Metal is called from encoder_mtl.mm.
class Encoder {
public:
    struct Layout {
        // Every texture and sampler is read from one array each (set_texture_arrays); else a draw binds its tables
        // (set_tables).
        bool texture_arrays = false;
    };
    void configure(const Layout& layout) noexcept { layout_ = layout; }
    // `encoder`: an id<MTLRenderCommandEncoder>, the caller's to end. `width`, `height`: its targets' size in pixels, which
    // a scissor is cut to (Metal takes no rectangle that leaves the target).
    void attach(mtl::Object encoder, std::uint32_t width, std::uint32_t height) noexcept {
        encoder_ = encoder;
        target_width_ = width;
        target_height_ = height;
        reset();
    }
    // Nothing is taken as bound any more. The rows and instances asked for stay: a draw pass names them once, and the
    // renderer's own work inside it resets the bindings.
    void reset() noexcept {
        pipeline_ = nullptr;
        for (Slot& slot : slots_) {
            slot.bound = 0u;
            slot.buffer = nullptr;
        }
        slots_[kSlotView].wanted = slots_[kSlotVertexRows].wanted = slots_[kSlotPixelRows].wanted = 0u;
        slots_[kSlotLists].wanted = 0u;
        constants_valid_ = false;
        textures_ = samplers_ = 0u;
        root_valid_ = scissor_valid_ = viewport_valid_ = index_valid_ = false;
        scissor_empty_ = false;
        for (StreamView& s : streams_) s = StreamView{};
        for (mtl::Object& buffer : stream_buffers_) buffer = nullptr;
    }
    // Other code set the viewport and scissor.
    void forget_rects() noexcept { scissor_valid_ = viewport_valid_ = false; }
    void set_pipeline(Pipeline* pipeline) noexcept {
        if (pipeline != pipeline_) {
            bind_pipeline(pipeline);
            pipeline_ = pipeline;
        }
    }
    void set_view(Address view) noexcept { want(kSlotView, view); }
    void set_material(Address vertex_rows, Address pixel_rows) noexcept {
        want(kSlotVertexRows, vertex_rows);
        want(kSlotPixelRows, pixel_rows);
    }
    void set_root(const DrawRoot& root) noexcept {
        if (!root_valid_ || root.first_instance != root_.first_instance || root.flags != root_.flags ||
            root.half_line != root_.half_line || root.half_point != root_.half_point || root.table != root_.table) {
            root_ = root;
            root_valid_ = true;
            push_root();
        }
    }
    // A draw's own tables: mtl::BoundTextures and mtl::BoundSamplers behind the tables' ptr.
    void set_tables(Table textures, Table samplers) noexcept {
        if (textures.ptr != textures_ || samplers.ptr != samplers_) {
            bind_tables(textures.ptr != textures_ ? textures.ptr : 0u, samplers.ptr != samplers_ ? samplers.ptr : 0u);
            textures_ = textures.ptr;
            samplers_ = samplers.ptr;
        }
    }
    // Every texture and every sampler (the arrays' buffers behind the tables' ptr, bound once per list of bindings) and
    // the frame's texture lists, read through each draw's DrawRoot::table.
    void set_texture_arrays(Table textures, Table samplers, Address lists) noexcept {
        if (textures.ptr != textures_ || samplers.ptr != samplers_) {
            bind_arrays(textures.ptr, samplers.ptr);
            textures_ = textures.ptr;
            samplers_ = samplers.ptr;
        }
        want(kSlotLists, lists);
    }
    void set_rows(Address rows, Address instances) noexcept {
        want(kSlotRows, rows);
        want(kSlotInstances, instances);
    }
    void set_scissor(const Rect& rect) noexcept {
        if (!scissor_valid_ || rect.left != scissor_.left || rect.top != scissor_.top || rect.right != scissor_.right ||
            rect.bottom != scissor_.bottom) {
            scissor_ = rect;
            scissor_valid_ = true;
            apply_scissor();
        }
    }
    void set_viewport(const ViewportRect& rect) noexcept {
        if (!viewport_valid_ || !(rect == viewport_)) {
            viewport_ = rect;
            viewport_valid_ = true;
            apply_viewport();
        }
    }
    void set_stream(unsigned slot, const StreamView& view) noexcept {
        if (view.address != streams_[slot].address) {
            bind_stream(slot, view);
            streams_[slot] = view;
        }
    }
    void set_indices(Address address, std::uint32_t bytes, std::uint32_t index_bytes) noexcept;
    void draw_indexed(std::uint32_t count, std::uint32_t instances, std::uint32_t first, std::int32_t base_vertex) noexcept;
    void draw(std::uint32_t count, std::uint32_t instances, std::uint32_t first) noexcept;

private:
    // The buffers a draw's constants come from, each bound to the functions that read it.
    enum : unsigned { kSlotView, kSlotVertexRows, kSlotPixelRows, kSlotRows, kSlotInstances, kSlotLists, kSlotCount };
    struct Slot {
        Address wanted = 0u, bound = 0u;
        mtl::Object buffer = nullptr;  // what `bound` resolved to
    };
    void want(unsigned slot, Address address) noexcept {
        slots_[slot].wanted = address;
        // An address of 0 asks for nothing: a program that reads no rows leaves it so, and what is bound stands in.
        constants_valid_ = constants_valid_ && (address == slots_[slot].bound || address == 0u);
    }
    void flush_constants() noexcept;
    void bind_pipeline(Pipeline* pipeline) noexcept;
    void push_root() noexcept;
    void bind_tables(std::uint64_t textures, std::uint64_t samplers) noexcept;
    void bind_arrays(std::uint64_t textures, std::uint64_t samplers) noexcept;
    void apply_scissor() noexcept;
    void apply_viewport() noexcept;
    void bind_stream(unsigned slot, const StreamView& view) noexcept;

    Layout layout_{};
    mtl::Object encoder_ = nullptr;
    std::uint32_t target_width_ = 0u, target_height_ = 0u;
    Pipeline* pipeline_ = nullptr;
    Slot slots_[kSlotCount]{};
    bool constants_valid_ = false;
    std::uint64_t textures_ = 0u, samplers_ = 0u;
    DrawRoot root_{};
    Rect scissor_{};
    ViewportRect viewport_{};
    StreamView streams_[kStreamCount]{};
    mtl::Object stream_buffers_[kStreamCount]{};
    mtl::Object index_buffer_ = nullptr;
    std::uint64_t index_offset_ = 0u;
    Address index_address_ = 0u;
    std::uint32_t index_size_ = 0u, index_stride_ = 0u;
    bool root_valid_ = false, scissor_valid_ = false, viewport_valid_ = false, index_valid_ = false;
    bool scissor_empty_ = false;  // the scissor lets nothing through: draws are left out
};

}  // namespace galaxy::render::gpu
#elif !defined(NEBULA_GPU_VULKAN)
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
