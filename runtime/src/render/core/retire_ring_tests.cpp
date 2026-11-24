// SPDX-License-Identifier: GPL-3.0-only
// The retire ring (retire_ring.h): an owner is destroyed only at its own slot's release, never in the frame that retired
// it, exactly once, oldest first; release_all destroys the rest; the bytes add up and fall. A fake renderer completes a
// slot's fence a chosen number of frames behind and releases the slot right after, as begin_frame does.

#include "render/core/retire_ring.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

using galaxy::render::core::RetireRing;

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAILED: %s\n", what);
        ++failures;
    }
}

// An owner that writes its name into `order` when it is destroyed; a moved-from one writes nothing.
struct Owner {
    std::vector<std::string>* order = nullptr;
    std::string name;
    Owner(std::vector<std::string>& o, std::string n) : order(&o), name(std::move(n)) {}
    Owner(Owner&& other) noexcept : order(other.order), name(std::move(other.name)) { other.order = nullptr; }
    Owner(const Owner&) = delete;
    ~Owner() {
        if (order != nullptr) {
            order->push_back(name);
        }
    }
};

void test_slot_and_order() {
    std::vector<std::string> order;
    RetireRing ring(2u);
    ring.retire(0u, Owner(order, "a"), 10u);
    ring.retire(0u, Owner(order, "b"), 5u);
    ring.retire(1u, Owner(order, "c"), 1u);
    check(order.empty() && ring.count() == 3u && ring.bytes() == 16u, "nothing is destroyed at retire; the bytes add up");
    ring.release(1u);
    check(order.size() == 1u && order[0] == "c" && ring.bytes() == 15u, "a release empties its own slot only");
    ring.release(0u);
    check(order.size() == 3u && order[1] == "a" && order[2] == "b", "oldest first");
    check(ring.bytes() == 0u && ring.count() == 0u, "the bytes fall as owners go");
    ring.release(0u);
    check(order.size() == 3u, "exactly once");
}

// The frame loop: frame n records in slot n % 2; before it, the slot's fence (frame n - 2) has passed and the slot is
// released. An owner retired in frame n lives through frame n + 1 and goes at the start of frame n + 2.
void test_frames() {
    std::vector<std::string> order;
    RetireRing ring(2u);
    std::uint64_t completed = 0u;  // the last frame the GPU finished
    const auto begin_frame = [&](std::uint64_t frame) {
        completed = frame >= 2u ? frame - 2u : 0u;  // the fence wait for this slot's last frame
        ring.release(static_cast<unsigned>(frame % 2u));
    };
    begin_frame(2u);
    ring.retire(0u, Owner(order, "heap of frame 2"));
    // The same frame, after the release: alive until the slot's next release.
    check(order.empty(), "not destroyed in the frame that retired it");
    begin_frame(3u);
    check(order.empty(), "the other slot's release leaves it");
    begin_frame(4u);
    check(order.size() == 1u && completed >= 2u, "destroyed at its slot's next release, once frame 2 is complete");
}

void test_functions_and_all() {
    int destroyed = 0;
    {
        RetireRing ring(3u);
        ring.retire(2u, [&destroyed] { ++destroyed; });
        auto shared = std::make_shared<int>(7);
        std::weak_ptr<int> weak = shared;
        ring.retire(1u, std::move(shared), 64u);
        check(destroyed == 0 && !weak.expired(), "functions and shared owners kept");
        ring.release_all();
        check(destroyed == 1 && weak.expired() && ring.bytes() == 0u, "release_all destroys every slot's owners");
        ring.retire(0u, [&destroyed] { ++destroyed; });
    }
    check(destroyed == 2, "the ring's end releases what is left");
}

}  // namespace

int main() {
    test_slot_and_order();
    test_frames();
    test_functions_and_all();
    if (failures != 0) {
        std::printf("%d failed\n", failures);
        return 1;
    }
    std::printf("retire ring: all passed\n");
    return 0;
}
