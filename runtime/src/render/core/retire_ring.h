// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// What a renderer gives up while frames in flight may still read it, kept for each frame slot until that slot's fence
// has passed (FRAME-MODEL 2.1 rule 1: nothing waits for the GPU inside a frame). An owner is any movable object whose
// destruction frees the resource: a ComPtr, a shared_ptr, a function that destroys a Vulkan handle. retire(slot, ...)
// keeps it with the slot the renderer is recording; release(slot) destroys that slot's owners, oldest first, and the
// renderer calls it right after its wait for the slot's fence, before anything of the new frame is retired.
// release_all() is for a whole-GPU wait (a device restart, shutdown). The ring holds no GPU type and never looks at a
// fence itself. Render thread only. Header-only: retire_ring_tests.cpp.

#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace galaxy::render::core {

class RetireRing {
public:
    explicit RetireRing(unsigned slots) : slots_(slots) {}
    RetireRing(const RetireRing&) = delete;
    RetireRing& operator=(const RetireRing&) = delete;
    ~RetireRing() { release_all(); }

    // Keeps `owner` until slot `slot` is next released; `bytes` is what it holds (the video-memory log adds it up).
    template <class Owner>
    void retire(unsigned slot, Owner&& owner, std::uint64_t bytes = 0u) {
        using Held = Holder<std::decay_t<Owner>>;
        slots_[slot].push_back(Entry{std::make_unique<Held>(std::forward<Owner>(owner)), bytes});
        bytes_ += bytes;
    }

    // Destroys slot `slot`'s owners, oldest first.
    void release(unsigned slot) {
        std::vector<Entry>& list = slots_[slot];
        // Taken out first: an owner's destruction may retire another, which waits for the slot's next release.
        std::vector<Entry> taken;
        taken.swap(list);
        for (Entry& entry : taken) {
            bytes_ -= entry.bytes;
            entry.owner.reset();
        }
    }

    void release_all() {
        for (unsigned slot = 0; slot < slots_.size(); ++slot) {
            release(slot);
        }
    }

    [[nodiscard]] std::uint64_t bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::size_t count() const noexcept {
        std::size_t total = 0u;
        for (const std::vector<Entry>& list : slots_) {
            total += list.size();
        }
        return total;
    }

private:
    struct Base {
        virtual ~Base() = default;
    };
    template <class T>
    struct Holder final : Base {
        explicit Holder(T&& value) : owner(std::move(value)) {}
        explicit Holder(const T& value) : owner(value) {}
        ~Holder() override {
            if constexpr (std::is_invocable_v<T&>) {
                owner();  // a function that frees what it names
            }
        }
        T owner;
    };
    struct Entry {
        std::unique_ptr<Base> owner;
        std::uint64_t bytes = 0u;
    };
    std::vector<std::vector<Entry>> slots_;
    std::uint64_t bytes_ = 0u;
};

}  // namespace galaxy::render::core
