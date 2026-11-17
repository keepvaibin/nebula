// SPDX-License-Identifier: GPL-3.0-only
// The mods' code spans the guard's handler reads (luma_guard.cpp): every mod library's executable part while it is
// loaded. No fixed limit: the table is a list of chunks of slots that grows by a chunk when every slot is taken; a
// chunk is never freed while the process runs (its slots are reused), so a fault handler walks it with no lock.
//
// A handler reads with acquire loads only: a slot's end is written before its begin and cleared after it, so a reader
// that sees a begin sees its end; a slot being cleared is seen whole or not at all. Writers (add, remove) take the
// writer flag; a new chunk is complete before the pointer to it is published. Allocation is the writer's (never the
// handler's).
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>

namespace galaxy::luma_guard {

class Spans {
public:
    static constexpr std::size_t kChunkSlots = 64;

    Spans() noexcept = default;
    Spans(const Spans&) = delete;
    Spans& operator=(const Spans&) = delete;
    // Chunks live as long as the process; a test's own table frees its chunks.
    ~Spans() {
        Chunk* chunk = first_.next.load(std::memory_order_acquire);
        while (chunk != nullptr) {
            Chunk* const next = chunk->next.load(std::memory_order_acquire);
            delete chunk;
            chunk = next;
        }
    }

    // The begin of the span holding `pc`, or 0. Any thread, a fault handler too.
    [[nodiscard]] std::uintptr_t span_of(std::uintptr_t pc) const noexcept {
        for (const Chunk* chunk = &first_; chunk != nullptr; chunk = chunk->next.load(std::memory_order_acquire)) {
            for (std::size_t i = 0; i < kChunkSlots; ++i) {
                const std::uintptr_t begin = chunk->begin[i].load(std::memory_order_acquire);
                if (begin != 0 && pc >= begin && pc < chunk->end[i].load(std::memory_order_relaxed)) return begin;
            }
        }
        return 0;
    }

    [[nodiscard]] bool has(std::uintptr_t pc) const noexcept { return span_of(pc) != 0; }

    // Adds [begin, end). A span already there (a library loaded twice) is one span. False: no memory for a new chunk
    // (the caller says so; that library's faults are then resumed only under its own boundary).
    bool add(std::uintptr_t begin, std::uintptr_t end) noexcept {
        if (begin == 0 || end <= begin) return false;
        const Writer writer(writer_);
        Chunk* last = &first_;
        std::atomic<std::uintptr_t>* free_begin = nullptr;
        std::atomic<std::uintptr_t>* free_end = nullptr;
        for (Chunk* chunk = &first_; chunk != nullptr; chunk = chunk->next.load(std::memory_order_relaxed)) {
            last = chunk;
            for (std::size_t i = 0; i < kChunkSlots; ++i) {
                const std::uintptr_t at = chunk->begin[i].load(std::memory_order_relaxed);
                if (at == begin) return true;
                if (at == 0 && free_begin == nullptr) {
                    free_begin = &chunk->begin[i];
                    free_end = &chunk->end[i];
                }
            }
        }
        if (free_begin == nullptr) {
            Chunk* const grown = new (std::nothrow) Chunk();
            if (grown == nullptr) return false;
            free_begin = &grown->begin[0];
            free_end = &grown->end[0];
            grown->end[0].store(end, std::memory_order_relaxed);
            grown->begin[0].store(begin, std::memory_order_relaxed);
            last->next.store(grown, std::memory_order_release);
            ++chunks_;
            return true;
        }
        free_end->store(end, std::memory_order_relaxed);
        free_begin->store(begin, std::memory_order_release);
        return true;
    }

    // Takes out the span that begins at `begin` (nothing when there is none).
    void remove(std::uintptr_t begin) noexcept {
        if (begin == 0) return;
        const Writer writer(writer_);
        for (Chunk* chunk = &first_; chunk != nullptr; chunk = chunk->next.load(std::memory_order_relaxed)) {
            for (std::size_t i = 0; i < kChunkSlots; ++i) {
                if (chunk->begin[i].load(std::memory_order_relaxed) == begin) {
                    chunk->begin[i].store(0, std::memory_order_release);
                    chunk->end[i].store(0, std::memory_order_relaxed);
                }
            }
        }
    }

    // Chunks added beyond the first (a test reads it).
    [[nodiscard]] std::size_t grown_chunks() const noexcept { return chunks_; }

private:
    struct Chunk {
        std::atomic<std::uintptr_t> begin[kChunkSlots] = {};
        std::atomic<std::uintptr_t> end[kChunkSlots] = {};
        std::atomic<Chunk*> next{nullptr};
    };
    static_assert(std::atomic<std::uintptr_t>::is_always_lock_free && std::atomic<Chunk*>::is_always_lock_free,
                  "the spans are read in a signal handler");

    struct Writer {
        explicit Writer(std::atomic<std::uint32_t>& flag) noexcept : flag_(flag) {
            std::uint32_t free = 0u;
            while (!flag_.compare_exchange_weak(free, 1u, std::memory_order_acquire)) free = 0u;
        }
        ~Writer() { flag_.store(0u, std::memory_order_release); }
        Writer(const Writer&) = delete;
        Writer& operator=(const Writer&) = delete;
        std::atomic<std::uint32_t>& flag_;
    };

    Chunk first_;
    std::atomic<std::uint32_t> writer_{0};
    std::size_t chunks_ = 0;
};

}  // namespace galaxy::luma_guard
