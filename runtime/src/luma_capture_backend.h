// SPDX-License-Identifier: GPL-3.0-only
// What LumaCore's capture services (luma_capture.cpp) share with a device backend in a file of its own
// (luma_capture_apple.mm): an open device's item, the lock its thread shares with the game thread, and the hand-offs.
// Each backend gives microphone_thread and camera_thread; each runs on a thread of its own until the item's stop.
#pragma once

#include <luma_capture.h>
#include <luma_service_provider.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace galaxy::luma_capture::backend {

constexpr std::size_t kBlocks = 8u;  // microphone blocks waiting: 170 ms at 48 kHz

struct Item;
struct Node {
    LumaEventNode event;  // first, and both plain members: the kernel's node is the Node
    Item* item;
};

// One open device. Members marked g_lock are shared with its thread.
struct Item {
    Node node{};
    const LumaMod* mod = nullptr;
    LumaHandle handle = 0;
    LumaEventFn fn = nullptr;
    void* user = nullptr;
    bool camera = false;
    std::atomic<bool> stop{false};  // closed, or its mod gone: the thread ends at its next look
    bool closed = false;            // g_lock: no event from now on
    bool posted = false;            // g_lock
    std::int32_t ended = LUMA_OK;   // g_lock: ENDED to give (its status), not LUMA_OK
    // Microphone (g_lock): blocks of LUMA_AUDIO_MICROPHONE_BLOCK_FRAMES × channels.
    std::uint32_t rate = 48000u;
    std::uint32_t channels = 1u;
    std::vector<float> blocks;
    std::size_t ready_first = 0;
    std::size_t ready_count = 0;
    bool lost = false;
    std::vector<float> handing;  // the game thread's copy
    // Camera (g_lock): the newest picture, swapped in by the thread, out by the delivery.
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t fps = 0;
    std::vector<std::uint8_t> ready;
    bool fresh = false;
    bool skipped = false;
    std::uint32_t ready_width = 0;
    std::uint32_t ready_height = 0;
    std::uint64_t ready_time = 0;
    std::vector<std::uint8_t> shown;  // the game thread's
};

extern std::mutex g_lock;

// With g_lock: queue the item's node (once). Any thread.
void post_locked(Item* item) noexcept;
// The device's thread, with g_lock: it ended on its own (status ABSENT or IO).
void end_locked(Item* item, std::int32_t status) noexcept;

// The steady clock in nanoseconds: the times FRAME gives.
[[nodiscard]] std::uint64_t steady_ns() noexcept;

// The device's thread: `picture` (RGBA rows top first, width × 4 bytes each) becomes the item's newest, taken at
// `time_ns` (steady_ns's clock); `picture` comes back with the one it replaced, to fill next.
void hand_picture(Item& item, std::vector<std::uint8_t>& picture, std::uint32_t width, std::uint32_t height,
                  std::uint64_t time_ns);

// Device frames (float, `in_channels` each, interleaved) to the mod's rate and channels, into blocks. One a device's
// thread.
struct Converter {
    double place = 0.0;  // between `last` and the next device frame
    float last[2] = {0.0f, 0.0f};
    bool started = false;
    std::vector<float> filling;
    std::size_t filled = 0;

    void push(Item& item, const float* in, std::size_t frames, std::uint32_t in_channels, std::uint32_t in_rate);

private:
    void hand(Item& item);
};

// The backend's: the device until the item's stop. Neither returns before its device is let go.
void microphone_thread(std::shared_ptr<Item> item);
void camera_thread(std::shared_ptr<Item> item);

}  // namespace galaxy::luma_capture::backend
