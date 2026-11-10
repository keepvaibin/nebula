// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Parallel recording of a big draw pass. The walker still resolves every draw on the render thread, in order (views, materials,
// pipelines, texture lists: state many draws share), but a pass over the threshold hands its encoder calls to a DrawRecorder
// instead of the encoder: a stream of plain operations cut into chunks (a chunk never spans a change of the draw targets, so one
// chunk is one run of draws into one set of targets). The chunks are then replayed through an encoder each, on worker threads, into
// a command list (Direct3D 12) or a secondary command buffer (Vulkan) of their own; the renderer runs them in order after the
// commands already recorded. replay_ops is also how the render thread replays a recorded pass itself (a small one, or one thread).

#include "render/core/gpu_encoder.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace galaxy::render::gpu {

// What a draw binds for its textures when the renderer holds all of them in one array: the tables (Vulkan: the descriptor sets;
// Direct3D 12: the GPU handles into `heap`, the heap itself for a list that has not got it) and the frame's texture lists.
struct TextureArrays {
    Table textures{};
    Table samplers{};
    Address lists = 0u;
    const void* heap = nullptr;
};

// One encoder call, as plain words (the payload of each kind is in the comment).
struct DrawOp {
    enum class Kind : std::uint8_t {
        Pipeline,    // a[0] the pipeline
        View,        // a[0] the view rows
        Material,    // a[0] vertex rows, a[1] pixel rows
        Root,        // w: a DrawRoot
        Arrays,      // a[0] textures, a[1] samplers, a[2] lists, a[3] heap
        Tables,      // a[0] textures, a[1] samplers (Vulkan without descriptor indexing)
        Rows,        // a[0] rows, a[1] instances
        Scissor,     // w: a Rect
        Viewport,    // w: a ViewportRect
        Stream,      // slot, a[0] address, w[0] bytes, w[1] stride
        Indices,     // a[0] address, w[0] bytes, w[1] bytes per index
        DrawIndexed, // w[0] count, w[1] instances, w[2] first, w[3] base vertex
        Draw,        // w[0] count, w[1] instances, w[2] first
    };
    Kind kind = Kind::Draw;
    std::uint8_t slot = 0u;
    std::uint32_t w[6]{};
    std::uint64_t a[4]{};
};

static_assert(sizeof(DrawRoot) <= sizeof(DrawOp::w), "a DrawRoot travels in an op's words");
static_assert(sizeof(Rect) <= sizeof(DrawOp::w), "a Rect travels in an op's words");
static_assert(sizeof(ViewportRect) <= sizeof(DrawOp::w), "a ViewportRect travels in an op's words");
static_assert(std::is_trivially_copyable_v<DrawRoot> && std::is_trivially_copyable_v<Rect> &&
              std::is_trivially_copyable_v<ViewportRect>);

// The encoder's interface, recording. Nothing is elided here (the encoder that replays a chunk elides what it has bound), so a
// chunk replays from a fresh encoder. The first call of a draw is set_draw_aux: the place a chunk is cut.
class DrawRecorder {
public:
    struct Chunk {
        std::uint32_t first = 0u;   // its first op
        std::uint32_t count = 0u;
        std::uint32_t draws = 0u;
        std::uint8_t aux = 0u;      // the draw targets of its draws (RenderCore: set_draw_aux)
    };

    // A new pass: chunks of about `chunk_draws` draws.
    void begin(std::uint32_t chunk_draws) {
        ops_.clear();
        chunks_.clear();
        chunk_draws_ = std::max(1u, chunk_draws);
        draws_ = 0u;
        rows_ = instances_ = 0u;
    }
    // The last chunk is closed.
    void finish() {
        if (!chunks_.empty()) {
            chunks_.back().count = static_cast<std::uint32_t>(ops_.size()) - chunks_.back().first;
        }
    }
    [[nodiscard]] const std::vector<DrawOp>& ops() const noexcept { return ops_; }
    [[nodiscard]] const std::vector<Chunk>& chunks() const noexcept { return chunks_; }
    [[nodiscard]] std::uint32_t draws() const noexcept { return draws_; }

    void set_draw_aux(unsigned aux) {
        if (chunks_.empty() || chunks_.back().aux != aux || chunks_.back().draws >= chunk_draws_) {
            cut(static_cast<std::uint8_t>(aux));
        }
    }
    // Recorded at the start of every chunk (a pass names its rows once).
    void set_rows(Address rows, Address instances) noexcept {
        rows_ = rows;
        instances_ = instances;
    }
    void set_pipeline(Pipeline* pipeline) {
        DrawOp& op = next(DrawOp::Kind::Pipeline);
        op.a[0] = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(pipeline));
    }
    void set_view(Address view) { next(DrawOp::Kind::View).a[0] = view; }
    void set_material(Address vertex_rows, Address pixel_rows) {
        DrawOp& op = next(DrawOp::Kind::Material);
        op.a[0] = vertex_rows;
        op.a[1] = pixel_rows;
    }
    void set_root(const DrawRoot& root) { std::memcpy(next(DrawOp::Kind::Root).w, &root, sizeof(root)); }
    void set_texture_arrays(const TextureArrays& arrays) {
        DrawOp& op = next(DrawOp::Kind::Arrays);
        op.a[0] = textures_ptr(arrays.textures);
        op.a[1] = textures_ptr(arrays.samplers);
        op.a[2] = arrays.lists;
        op.a[3] = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(arrays.heap));
    }
#if defined(NEBULA_GPU_VULKAN) || defined(NEBULA_GPU_METAL)
    void set_tables(Table textures, Table samplers) {
        DrawOp& op = next(DrawOp::Kind::Tables);
        op.a[0] = textures.ptr;
        op.a[1] = samplers.ptr;
    }
#endif
    void set_scissor(const Rect& rect) { std::memcpy(next(DrawOp::Kind::Scissor).w, &rect, sizeof(rect)); }
    void set_viewport(const ViewportRect& rect) { std::memcpy(next(DrawOp::Kind::Viewport).w, &rect, sizeof(rect)); }
    void set_stream(unsigned slot, const StreamView& view) {
        DrawOp& op = next(DrawOp::Kind::Stream);
        op.slot = static_cast<std::uint8_t>(slot);
        op.a[0] = view.address;
        op.w[0] = view.bytes;
        op.w[1] = view.stride;
    }
    void set_indices(Address address, std::uint32_t bytes, std::uint32_t index_bytes) {
        DrawOp& op = next(DrawOp::Kind::Indices);
        op.a[0] = address;
        op.w[0] = bytes;
        op.w[1] = index_bytes;
    }
    void draw_indexed(std::uint32_t count, std::uint32_t instances, std::uint32_t first, std::int32_t base_vertex) {
        DrawOp& op = next(DrawOp::Kind::DrawIndexed);
        op.w[0] = count;
        op.w[1] = instances;
        op.w[2] = first;
        op.w[3] = static_cast<std::uint32_t>(base_vertex);
        note_draw();
    }
    void draw(std::uint32_t count, std::uint32_t instances, std::uint32_t first) {
        DrawOp& op = next(DrawOp::Kind::Draw);
        op.w[0] = count;
        op.w[1] = instances;
        op.w[2] = first;
        note_draw();
    }

private:
    [[nodiscard]] static std::uint64_t textures_ptr(const Table& table) noexcept { return static_cast<std::uint64_t>(table.ptr); }
    void cut(std::uint8_t aux) {
        if (!chunks_.empty()) {
            chunks_.back().count = static_cast<std::uint32_t>(ops_.size()) - chunks_.back().first;
        }
        Chunk chunk;
        chunk.first = static_cast<std::uint32_t>(ops_.size());
        chunk.aux = aux;
        chunks_.push_back(chunk);
        DrawOp& rows = ops_.emplace_back();
        rows.kind = DrawOp::Kind::Rows;
        rows.a[0] = rows_;
        rows.a[1] = instances_;
    }
    DrawOp& next(DrawOp::Kind kind) {
        if (chunks_.empty()) {
            cut(0u);
        }
        DrawOp& op = ops_.emplace_back();
        op.kind = kind;
        return op;
    }
    void note_draw() noexcept {
        ++draws_;
        ++chunks_.back().draws;
    }

    std::vector<DrawOp> ops_;
    std::vector<Chunk> chunks_;
    std::uint32_t chunk_draws_ = 1u;
    std::uint32_t draws_ = 0u;
    Address rows_ = 0u, instances_ = 0u;
};

// Replays recorded ops through `encoder`. `on_arrays(const TextureArrays&)` binds a texture array op where the backend's
// own state needs it (Direct3D 12: the heap on the list); it ends with encoder.set_texture_arrays.
template <class E, class OnArrays>
void replay_ops(E& encoder, const DrawOp* ops, std::size_t count, OnArrays&& on_arrays) {
    for (std::size_t n = 0; n < count; ++n) {
        const DrawOp& op = ops[n];
        switch (op.kind) {
        case DrawOp::Kind::Pipeline:
            encoder.set_pipeline(reinterpret_cast<Pipeline*>(static_cast<std::uintptr_t>(op.a[0])));
            break;
        case DrawOp::Kind::View:
            encoder.set_view(op.a[0]);
            break;
        case DrawOp::Kind::Material:
            encoder.set_material(op.a[0], op.a[1]);
            break;
        case DrawOp::Kind::Root: {
            DrawRoot root;
            std::memcpy(&root, op.w, sizeof(root));
            encoder.set_root(root);
            break;
        }
        case DrawOp::Kind::Arrays: {
            TextureArrays arrays;
            arrays.textures.ptr = op.a[0];
            arrays.samplers.ptr = op.a[1];
            arrays.lists = op.a[2];
            arrays.heap = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(op.a[3]));
            on_arrays(arrays);
            break;
        }
        case DrawOp::Kind::Tables:
#if defined(NEBULA_GPU_VULKAN) || defined(NEBULA_GPU_METAL)
        {
            Table textures, samplers;
            textures.ptr = op.a[0];
            samplers.ptr = op.a[1];
            encoder.set_tables(textures, samplers);
        }
#endif
            break;
        case DrawOp::Kind::Rows:
            encoder.set_rows(op.a[0], op.a[1]);
            break;
        case DrawOp::Kind::Scissor: {
            Rect rect;
            std::memcpy(&rect, op.w, sizeof(rect));
            encoder.set_scissor(rect);
            break;
        }
        case DrawOp::Kind::Viewport: {
            ViewportRect rect;
            std::memcpy(&rect, op.w, sizeof(rect));
            encoder.set_viewport(rect);
            break;
        }
        case DrawOp::Kind::Stream:
            encoder.set_stream(op.slot, StreamView{op.a[0], op.w[0], op.w[1]});
            break;
        case DrawOp::Kind::Indices:
            encoder.set_indices(op.a[0], op.w[0], op.w[1]);
            break;
        case DrawOp::Kind::DrawIndexed:
            encoder.draw_indexed(op.w[0], op.w[1], op.w[2], static_cast<std::int32_t>(op.w[3]));
            break;
        case DrawOp::Kind::Draw:
            encoder.draw(op.w[0], op.w[1], op.w[2]);
            break;
        }
    }
}

}  // namespace galaxy::render::gpu

namespace galaxy::render::core {

// Threads that record chunks beside the render thread, which takes a share itself. Idle between passes.
class RecordPool {
public:
    explicit RecordPool(unsigned extra_threads) {
        threads_.reserve(extra_threads);
        for (unsigned i = 0; i < extra_threads; ++i) {
            threads_.emplace_back([this] { loop(); });
        }
    }
    RecordPool(const RecordPool&) = delete;
    RecordPool& operator=(const RecordPool&) = delete;
    ~RecordPool() {
        {
            const std::lock_guard lock(mutex_);
            quit_ = true;
        }
        wake_.notify_all();
        for (std::thread& thread : threads_) {
            thread.join();
        }
    }
    [[nodiscard]] unsigned extra_threads() const noexcept { return static_cast<unsigned>(threads_.size()); }

    // Runs task(0) to task(count - 1), the pool's threads and the caller taking them as they come, and returns when all are
    // done. The first exception a task throws is thrown here once the rest have finished.
    void run(unsigned count, const std::function<void(unsigned)>& task) {
        if (count == 0u) {
            return;
        }
        if (threads_.empty() || count == 1u) {
            for (unsigned i = 0; i < count; ++i) {
                task(i);
            }
            return;
        }
        std::uint64_t generation = 0u;
        {
            const std::lock_guard lock(mutex_);
            task_ = &task;
            count_ = count;
            next_ = 0u;
            finished_ = 0u;
            error_ = nullptr;
            generation = ++generation_;
        }
        wake_.notify_all();
        work(generation, &task);
        std::exception_ptr error;
        {
            std::unique_lock lock(mutex_);
            done_.wait(lock, [this] { return finished_ == count_; });
            task_ = nullptr;
            error = std::exchange(error_, nullptr);
        }
        if (error) {
            std::rethrow_exception(error);
        }
    }

private:
    bool claim(std::uint64_t generation, unsigned& index) {
        const std::lock_guard lock(mutex_);
        if (generation != generation_ || next_ >= count_) {
            return false;
        }
        index = next_++;
        return true;
    }
    void work(std::uint64_t generation, const std::function<void(unsigned)>* task) {
        unsigned index = 0u;
        while (claim(generation, index)) {
            std::exception_ptr error;
            try {
                (*task)(index);
            } catch (...) {
                error = std::current_exception();
            }
            const std::lock_guard lock(mutex_);
            if (error && !error_) {
                error_ = error;
            }
            if (++finished_ == count_) {
                done_.notify_all();
            }
        }
    }
    void loop() {
        std::uint64_t seen = 0u;
        for (;;) {
            std::uint64_t generation = 0u;
            const std::function<void(unsigned)>* task = nullptr;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [&] { return quit_ || generation_ != seen; });
                if (quit_) {
                    return;
                }
                seen = generation_;
                generation = generation_;
                task = task_;
            }
            if (task != nullptr) {
                work(generation, task);
            }
        }
    }

    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable wake_, done_;
    const std::function<void(unsigned)>* task_ = nullptr;
    std::uint64_t generation_ = 0u;
    unsigned count_ = 0u, next_ = 0u, finished_ = 0u;
    std::exception_ptr error_;
    bool quit_ = false;
};

}  // namespace galaxy::render::core
