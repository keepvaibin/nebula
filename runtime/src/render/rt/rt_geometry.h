// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The tracer's bottom-level structures: one for each mesh that asks, built in the frame's commands within a budget,
// compacted once its size is known, and released with its mesh. All policy is here and none of the API: the back end
// (rt_backend.h) only builds, copies and holds memory, so this is tested offline against a stand-in.

#include "galaxy/render/rt/rt_backend.h"

#include <cstdint>
#include <deque>
#include <map>
#include <unordered_map>
#include <vector>

namespace galaxy::render::rt {

// Free ranges of pages of memory, best fit, aligned to kRangeAlign. A page is the back end's, named by its number there.
class RangePages {
public:
    // `bytes` in the fullest page with room; none (size 0) when no page has it.
    [[nodiscard]] Range take(std::uint64_t bytes);
    void give_back(const Range& range);
    void add_page(std::uint32_t page, std::uint64_t bytes);
    // A page nothing lies in, or ~0u; `drop_page` forgets it once the back end has freed it.
    [[nodiscard]] std::uint32_t empty_page() const noexcept;
    void drop_page(std::uint32_t page);
    [[nodiscard]] std::uint64_t page_bytes() const noexcept;  // of every page
    [[nodiscard]] std::uint64_t used_bytes() const noexcept;  // of them, in ranges
    [[nodiscard]] std::uint32_t pages() const noexcept;

private:
    struct Page {
        std::uint64_t bytes = 0u, free_bytes = 0u;
        std::map<std::uint64_t, std::uint64_t> free;  // offset -> size
        bool here = false;
    };
    std::vector<Page> pages_;
};

// What the structures hold and what a frame did.
struct GeometryStats {
    std::uint32_t structures = 0u;         // built, static and moving
    std::uint32_t waiting = 0u;            // asked for, not built yet
    std::uint64_t triangles = 0u;          // under the built ones
    std::uint64_t structure_bytes = 0u;    // in ranges
    std::uint64_t page_bytes = 0u;         // the pages those lie in, and the scratch
    std::uint32_t compacted = 0u;          // structures in their compacted form
    std::uint64_t built_triangles = 0u;    // by this frame's builds
    std::uint32_t builds = 0u, compactions = 0u;  // this frame
    std::uint64_t refit_triangles = 0u;    // under the moving meshes of the last refit()
    std::uint32_t refits = 0u, rebuilds = 0u;  // of the last refit(): refitted, and built anew
    std::uint64_t most_built_triangles = 0u;  // the most one budgeted frame built, since the start
    std::uint64_t timed_triangles = 0u;       // the builds of the frame slot's last frame, timed: their triangles
    std::uint64_t timed_ticks = 0u;           // and their GPU time (0: none timed)
    std::uint32_t released = 0u;              // static structures released past the ceiling, since the start
    std::uint32_t micromaps = 0u;             // opacity micromaps under built structures (5.3 G-2)
    std::uint64_t micromap_bytes = 0u;        // in ranges
    std::uint32_t micromaps_built = 0u;       // since the start
};

class Geometry {
public:
    explicit Geometry(Backend& backend) noexcept : backend_(backend) {}
    Geometry(const Geometry&) = delete;
    Geometry& operator=(const Geometry&) = delete;
    ~Geometry() { clear(); }

    // A mesh that does not move wants a structure of its vertices as they are: queued once. `key`: the caller's name
    // for it, kept until forget.
    void want(std::uint64_t key, const TriangleInput& input);
    // A waiting mesh the scene draws this frame, `distance_sq` from the eye (squared): the next frame builds such meshes
    // before the rest, the nearest first (plan 26: what comes into view casts first).
    void drawn_at(std::uint64_t key, float distance_sq);
    // A mesh whose vertices move: built by the first refit() after it is named, refitted from then on, rebuilt every
    // `kRefitsToRebuild` refits or when its triangles change. Named each frame it is to be traced; one not named for
    // kFramesToForget frames is forgotten.
    // `rebuild`: its triangles are not last frame's moved (a mesh made anew each frame): built, never refitted.
    void move(std::uint64_t key, const TriangleInput& input, bool rebuild = false);
    // The moving meshes named since the last call are built or refitted from their vertices as they lie now, ending
    // with a barrier: after frame(), once the frame's vertices are written, before anything names them.
    void refit();
    // The mesh is gone: its structure is released once no frame can name it.
    void forget(std::uint64_t key);
    // 5.3 G-2: an opacity micromap for a built static mesh, from `input` (its buffer is the geometry's from here: freed
    // once no frame can read it), named by `tag` (what it was made from: the draw's map). In a coming frame, within
    // the budget, the micromap is built and the mesh's structure again over it, and swapped in; until then the old one
    // is named. False (the buffer stays the caller's) when the mesh is not a built static one or has or awaits one.
    bool give_micromap(std::uint64_t key, const MicromapInput& input, std::uint32_t tag);
    [[nodiscard]] bool takes_micromap(std::uint64_t key) const noexcept;
    // What the micromap under the mesh's structure was made from; 0 when it has none.
    [[nodiscard]] std::uint32_t micromap_tag(std::uint64_t key) const noexcept;
    // Everything goes now, before the back end stops (the device is lost, or ray tracing is turned off and the GPU has
    // finished what named the structures): nothing is recorded.
    void clear() noexcept;

    // Once a frame, before anything names a structure: the builds and compactions the budget allows, into `commands`,
    // ending with a barrier, so a structure built here can be named in this frame. `budget_triangles`: 0 for no limit
    // (nothing is on screen); else at least one build happens however large.
    void frame(void* commands, unsigned frame_slot, std::uint64_t frame_index, std::uint64_t budget_triangles);

    // What an instance names the mesh by, or 0 while it has no structure.
    [[nodiscard]] std::uint64_t name(std::uint64_t key) const noexcept;
    [[nodiscard]] const GeometryStats& stats() const noexcept { return stats_; }

    static constexpr std::uint64_t kStructurePage = 16ull << 20u;
    static constexpr std::uint64_t kScratchPage = 4ull << 20u;
    static constexpr std::uint32_t kRefitsToRebuild = 30u;
    static constexpr std::uint64_t kFramesToForget = 4u;
    // 5.3 G-4: past this much structure memory the least recently named static structures go (the whole game compacts
    // to about 320 MB); one named in the last kFramesToForget frames stays.
    static constexpr std::uint64_t kStructureCeiling = 512ull << 20u;
    void set_ceiling(std::uint64_t bytes) noexcept { ceiling_ = bytes; }

private:
    enum class State : std::uint8_t { Waiting, Built, SizeAsked, Compacted };
    struct Entry {
        TriangleInput input{};
        BuildKind kind = BuildKind::Static;
        State state = State::Waiting;
        Structure structure{};
        std::uint64_t built_bytes = 0u;        // the range a refit and a rebuild of the same shape fit
        std::uint64_t asked_frame = 0u;       // Static: the frame its compacted size was asked in
        std::uint32_t refits = 0u;
        std::uint64_t named_frame = 0u;        // Moving: the last frame move() named it
        bool moved = false;                    // Moving: named this frame, to refit
        bool queued = false;
        mutable std::uint64_t used_frame = 0u;  // Static: the last frame it was built or named in (the ceiling's order)
        MicromapInput omm{};                    // Static: a micromap's input, waiting or under its structure
        std::uint32_t omm_tag = 0u;             // what that input was made from
        Structure micromap{};                   // the micromap under the structure
    };
    struct Retired {
        std::uint64_t frame = 0u;
        Structure structure{};
        bool micromap = false;
    };
    // The compacted sizes asked in one frame: slot n is keys[n]'s, read in the next frame of the same frame slot.
    struct Asked {
        std::uint64_t frame = 0u;
        unsigned frame_slot = 0u;
        std::vector<std::uint64_t> keys;
    };

    [[nodiscard]] Range take_structure(std::uint64_t bytes);
    [[nodiscard]] Range take_scratch(std::uint64_t bytes);
    void retire(Entry& entry);
    void release_retired();
    bool build(Entry& entry, bool refit);
    void order_queue();
    bool build_with_micromap(Entry& entry);

    Backend& backend_;
    std::unordered_map<std::uint64_t, Entry> entries_;
    std::deque<std::uint64_t> queue_;        // Static meshes waiting, oldest first
    std::vector<std::pair<float, std::uint64_t>> drawn_waiting_;  // drawn_at(): this frame's, by distance squared
    std::vector<std::uint64_t> moving_;      // keys of Moving entries
    std::vector<Retired> retired_;
    std::deque<std::uint64_t> to_ask_;       // built, their compacted size not asked yet
    std::deque<Asked> asked_;
    RangePages pages_;
    std::uint32_t scratch_page_ = ~0u;
    std::uint64_t scratch_bytes_ = 0u, scratch_used_ = 0u;
    std::vector<std::pair<std::uint64_t, std::uint32_t>> retired_scratch_;  // frame, page
    std::deque<std::uint64_t> micromap_queue_;  // static meshes whose micromap input waits, oldest first
    std::vector<std::pair<std::uint64_t, std::uint32_t>> retired_buffers_;  // frame, a micromap input's buffer
    std::uint64_t frame_index_ = 0u;
    bool built_since_barrier_ = false;
    std::uint64_t ceiling_ = kStructureCeiling;
    std::uint64_t timed_triangles_[kFramesInFlight]{};  // each frame slot's timed builds
    GeometryStats stats_{};
    void keep_under_ceiling();
};

}  // namespace galaxy::render::rt
