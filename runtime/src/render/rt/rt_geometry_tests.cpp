// SPDX-License-Identifier: GPL-3.0-only
// The tracer's bottom-level structures (rt_geometry.h) against a stand-in back end: ranges are taken best fit and
// merge when given back; a mesh is built once, named in the frame it is built, compacted when its size is known and
// released only when no frame can name it; a budget spreads builds over frames but never starves; a moving mesh is
// refitted, rebuilt now and then, and forgotten when no frame names it; and nothing a real back end would refuse is
// ever recorded. And the scene (rt_scene.h): its structure grows with its instances and the one it leaves goes only
// when no frame can read it.
#include "render/rt/rt_backend_fake.h"
#include "render/rt/rt_geometry.h"
#include "render/rt/rt_scene.h"

#include <iostream>

namespace {

using namespace galaxy::render::rt;
using galaxy::render::kFramesInFlight;

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

// A mesh of `triangles`, its vertices at a made-up address.
TriangleInput mesh(std::uint32_t triangles, std::uint64_t at = 0x1000u) {
    TriangleInput input;
    input.positions = at;
    input.vertex_count = triangles * 3u;
    input.indices = at + 0x800000u;
    input.index_count = triangles * 3u;
    return input;
}

struct Run {
    Backend backend;
    Geometry geometry{backend};
    std::uint64_t frame = 0u;
    Run() { (void)backend.start(nullptr); }
    void step(std::uint64_t budget = 0u) {
        ++frame;
        geometry.frame(nullptr, static_cast<unsigned>(frame % kFramesInFlight), frame, budget);
        geometry.refit();
    }
};

bool ranges_fit_and_merge() {
    RangePages pages;
    bool ok = expect(!pages.take(1000u), "no page, no range");
    pages.add_page(0u, 1u << 20u);
    const Range a = pages.take(1000u), b = pages.take(5000u), c = pages.take(300u);
    ok &= expect(a && b && c && a.size == 1024u && b.size == 5120u && c.size == 512u, "sizes are aligned to 256 bytes");
    ok &= expect(a.offset == 0u && b.offset == 1024u && c.offset == 6144u, "ranges follow one another");
    pages.give_back(b);
    const Range d = pages.take(4000u);
    ok &= expect(d.offset == 1024u, "a freed range is taken again before fresh memory (the smallest that fits)");
    pages.give_back(a);
    pages.give_back(d);
    pages.give_back(c);
    ok &= expect(pages.empty_page() == 0u && pages.used_bytes() == 0u, "everything given back leaves the page empty");
    const Range whole = pages.take(1u << 20u);
    ok &= expect(whole.size == (1u << 20u), "and its free ranges merged into one");
    pages.add_page(1u, 1u << 20u);
    pages.give_back(whole);
    const Range e = pages.take(2048u);
    const Range f = pages.take(2048u);
    ok &= expect(e.page == f.page, "the fuller page takes the next range, so the other can empty");
    return ok;
}

bool a_mesh_is_built_compacted_and_released() {
    Run run;
    const fake::Log& log = fake::log();
    run.geometry.want(1u, mesh(1000u, 0x1000u));
    run.geometry.want(2u, mesh(2000u, 0x2000u));
    run.geometry.want(2u, mesh(2000u, 0x2000u));
    bool ok = expect(run.geometry.name(1u) == 0u && run.geometry.stats().waiting == 2u, "a wanted mesh waits, once");
    run.step();
    ok &= expect(log.builds == 2u && log.barriers >= 1u && log.asks == 2u, "both are built and their sizes asked in one frame");
    const std::uint64_t first = run.geometry.name(1u);
    ok &= expect(first != 0u && run.geometry.name(2u) != 0u, "and can be named in that frame");
    const std::uint64_t built_bytes = run.geometry.stats().structure_bytes;
    for (unsigned i = 1; i < kFramesInFlight; ++i) {
        run.step();
    }
    ok &= expect(log.compactions == 0u, "no size is read before the GPU has written it");
    run.step();
    ok &= expect(log.compactions == 2u && run.geometry.name(1u) != first, "then each is copied into a smaller range and named there");
    ok &= expect(log.structures == 4u, "the built forms stay while a frame in flight may name them");
    for (std::uint64_t i = 0; i < kFramesToRelease; ++i) {
        run.step();
    }
    ok &= expect(log.structures == 2u && run.geometry.stats().structure_bytes < built_bytes / 2u, "and go after it, leaving the compacted ones");
    ok &= expect(run.geometry.stats().compacted == 2u && run.geometry.stats().triangles == 3000u, "the counts say so");
    run.geometry.forget(1u);
    ok &= expect(run.geometry.name(1u) == 0u && log.structures == 2u, "a forgotten mesh is not named again, and its structure waits");
    for (std::uint64_t i = 0; i < kFramesToRelease; ++i) {
        run.step();
    }
    ok &= expect(log.structures == 1u && run.geometry.stats().structures == 1u, "until no frame can name it");
    ok &= expect(log.builds == 2u && log.faults == 0u, "nothing was built twice, and nothing a back end refuses was recorded");
    return ok;
}

bool a_budget_spreads_builds() {
    Run run;
    const fake::Log& log = fake::log();
    for (std::uint64_t key = 1u; key <= 10u; ++key) {
        run.geometry.want(key, mesh(1000u, key * 0x10000u));
    }
    run.geometry.want(11u, mesh(50000u, 0x900000u));
    run.step(2500u);
    bool ok = expect(log.builds == 2u, "a frame builds what its budget holds");
    run.geometry.forget(3u);
    unsigned frames = 1u;
    while (run.geometry.stats().waiting != 0u && frames < 100u) {
        run.step(2500u);
        ++frames;
    }
    // 9 small ones left at two a frame, then the large one alone.
    ok &= expect(log.builds == 10u && frames == 6u, "the rest follow, oldest first, a forgotten one never built");
    ok &= expect(run.geometry.name(11u) != 0u, "a mesh larger than the budget is still built, alone");
    ok &= expect(run.geometry.stats().most_built_triangles == 50000u, "and the most a frame built is known");
    Run all;
    for (std::uint64_t key = 1u; key <= 10u; ++key) {
        all.geometry.want(key, mesh(1000u, key * 0x10000u));
    }
    all.step(0u);
    ok &= expect(fake::log().builds == 10u, "with no budget everything is built at once");
    ok &= expect(fake::log().faults == 0u, "and nothing a back end refuses was recorded");
    return ok;
}

bool what_is_drawn_is_built_first_the_nearest_first() {
    Run run;
    for (std::uint64_t key = 1u; key <= 6u; ++key) {
        run.geometry.want(key, mesh(1000u, key * 0x10000u));
    }
    run.geometry.drawn_at(5u, 100.0f);
    run.geometry.drawn_at(3u, 900.0f);
    run.geometry.drawn_at(5u, 400.0f);  // drawn twice: its nearer place counts
    run.geometry.drawn_at(42u, 1.0f);   // not waiting: no effect
    run.step(1500u);  // one a frame
    bool ok = expect(run.geometry.name(5u) != 0u && run.geometry.name(1u) == 0u, "the nearest drawn mesh is built first");
    run.step(1500u);
    ok &= expect(run.geometry.name(3u) != 0u && run.geometry.name(1u) == 0u, "then the next drawn one");
    run.step(1500u);
    run.step(1500u);
    ok &= expect(run.geometry.name(1u) != 0u && run.geometry.name(2u) != 0u && run.geometry.name(4u) == 0u,
                 "then the rest, oldest first");
    ok &= expect(fake::log().faults == 0u, "and nothing a back end refuses was recorded");
    return ok;
}

bool scratch_is_used_again_only_after_a_barrier() {
    Run run;
    // Builds whose scratch together is several pages of it.
    for (std::uint64_t key = 1u; key <= 12u; ++key) {
        run.geometry.want(key, mesh(100000u, key * 0x1000000u));
    }
    run.step();
    bool ok = expect(fake::log().builds == 12u && fake::log().barriers > 1u, "builds past the scratch page are parted by barriers");
    ok &= expect(fake::log().faults == 0u, "so no two share scratch");
    return ok;
}

bool a_moving_mesh_is_refitted_and_forgotten() {
    Run run;
    const fake::Log& log = fake::log();
    for (unsigned i = 0; i < Geometry::kRefitsToRebuild + 2u; ++i) {
        // Named after the frame began, as the scene's walk does.
        ++run.frame;
        run.geometry.frame(nullptr, static_cast<unsigned>(run.frame % kFramesInFlight), run.frame, 1u);
        TriangleInput moved = mesh(500u, i * 0x100u);
        moved.own_buffer = 0u;  // vertices a pass wrote, at byte 0 on the first frame
        run.geometry.move(7u, moved);
        run.geometry.refit();
    }
    bool ok = expect(log.builds == 2u && log.refits == Geometry::kRefitsToRebuild, "built, refitted each frame, and rebuilt after its refits");
    ok &= expect(log.structures == 1u && log.asks == 0u, "in one structure, never compacted");
    const std::uint64_t name = run.geometry.name(7u);
    run.geometry.move(7u, mesh(800u, 0x9000u));
    run.step();
    ok &= expect(log.builds == 3u && run.geometry.name(7u) != 0u && run.geometry.name(7u) != name, "other triangles are a new structure");
    for (std::uint64_t i = 0; i <= Geometry::kFramesToForget + kFramesToRelease; ++i) {
        run.step();
    }
    ok &= expect(run.geometry.name(7u) == 0u && log.structures == 0u, "one no frame names is forgotten and released");
    ok &= expect(log.faults == 0u, "and nothing a back end refuses was recorded");
    return ok;
}

bool an_answer_waits_for_its_frame_slot() {
    Run run;
    run.geometry.want(1u, mesh(1000u));
    run.step();
    // Frames without the tracer, then one in the other frame slot: the answer is not this slot's to read.
    run.frame += kFramesInFlight;
    run.step();
    bool ok = expect(fake::log().compactions == 0u && fake::log().asks == 1u, "a size is read only in the frame slot it was asked in");
    for (unsigned i = 1; i < kFramesInFlight; ++i) {
        run.step();
    }
    ok &= expect(fake::log().compactions == 1u && fake::log().asks == 1u && fake::log().faults == 0u,
                 "and the structure is compacted when that slot comes round, never asked twice");
    return ok;
}

bool a_scene_is_built_over_its_instances() {
    Run run;
    const fake::Log& log = fake::log();
    Scene scene{run.backend};
    const float rows[3][4] = {{1.0f, 0.0f, 0.0f, 5.0f}, {0.0f, 1.0f, 0.0f, 6.0f}, {0.0f, 0.0f, 1.0f, 7.0f}};
    scene.begin();
    bool ok = expect(!scene.build(0x4000u, 1u) && scene.name() == 0u && log.scenes == 0u, "no instance, no scene");
    scene.add(rows, 7u, kMaskOpaque, 0x1234u);
    scene.also(0u, kMaskBlended);
    const Instance& instance = scene.instances()[0];
    ok &= expect(instance.id_mask == (7u | 3u << 24u) && instance.structure == 0x1234u && instance.transform[1][3] == 6.0f,
                 "an instance carries its place, its id and what it is to a ray");
    // Faces: a draw's front is clockwise on screen, a ray's is the side its triangle's normal points to.
    const float mirrored[3][4] = {{-1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}};
    scene.add(rows, 8u, kMaskOpaque, 0x1234u, Faces::Front);
    scene.add(rows, 9u, kMaskOpaque, 0x1234u, Faces::Back);
    scene.add(mirrored, 10u, kMaskOpaque, 0x1234u, Faces::Front);
    ok &= expect(scene.instances()[0].flags == kInstanceNoCull << 24u, "a draw of both faces is met from both sides");
    ok &= expect(scene.instances()[1].flags == kInstanceFlip << 24u && scene.instances()[2].flags == 0u,
                 "a draw of its front faces turns the ray's facing over, one of its back faces does not");
    ok &= expect(scene.instances()[3].flags == 0u, "and a mirrored instance's facing is the other way");
    ok &= expect(scene.build(0x4000u, 1u) && scene.name() != 0u && log.scenes == 1u && log.scene_instances == 4u,
                 "the instances are built into a scene");
    ok &= expect(scene.capacity() == Scene::kLeastCapacity, "with room for the scenes after it");
    const std::uint64_t first = scene.name();
    scene.begin();
    ok &= expect(scene.name() == 0u, "a new frame has no scene until it is built");
    for (std::uint32_t i = 0; i < 300u; ++i) {
        scene.add(rows, i, kMaskOpaque, 0x1234u);
    }
    ok &= expect(scene.build(0x4000u, 2u) && scene.capacity() == 512u && scene.name() != first && log.scene_instances == 300u,
                 "more instances than its room take a larger structure");
    ok &= expect(log.structures == 2u && log.pages == 4u, "the one it leaves stays while a frame in flight may read it");
    scene.begin();
    scene.add(rows, 1u, kMaskOpaque, 0x1234u);
    ok &= expect(scene.build(0x4000u, 2u + kFramesToRelease) && log.structures == 1u && log.pages == 2u && scene.capacity() == 512u,
                 "and goes after, the larger one kept");
    ok &= expect(log.faults == 0u, "and nothing a back end refuses was recorded");
    scene.clear();
    ok &= expect(log.structures == 0u && log.pages == 0u && scene.name() == 0u, "cleared, it holds nothing");
    return ok;
}

// 5.3 G-4: past the ceiling the static structures no frame named for longest go; one named each frame stays, and a
// released mesh is built again when wanted again.
bool a_ceiling_releases_the_least_recently_named() {
    Run run;
    const fake::Log& log = fake::log();
    run.geometry.set_ceiling(fake::structure_bytes(1000u));
    for (std::uint64_t key = 1u; key <= 5u; ++key) {
        run.geometry.want(key, mesh(1000u, key * 0x10000u));
    }
    for (unsigned i = 0; i < 12u; ++i) {
        run.step();
        (void)run.geometry.name(5u);
    }
    bool ok = expect(run.geometry.stats().released >= 3u && run.geometry.name(1u) == 0u && run.geometry.name(5u) != 0u,
                     "past the ceiling the least recently named go, the one named each frame stays");
    const std::uint32_t builds = log.builds;
    run.geometry.want(1u, mesh(1000u, 0x10000u));
    run.step();
    ok &= expect(log.builds == builds + 1u && run.geometry.name(1u) != 0u, "a released mesh is built again when wanted");
    ok &= expect(log.faults == 0u, "and nothing a back end refuses was recorded");
    return ok;
}

// 5.3 G-2: a cutout mesh's micromap and the structure built again over it.
bool a_mesh_takes_a_micromap() {
    Run run;
    const fake::Log& log = fake::log();
    run.geometry.want(1u, mesh(100u));
    MicromapInput omm;
    omm.buffer = 3u;
    omm.triangles = 100u;
    omm.triangles_at = 6400u;
    omm.indices_at = 7424u;
    bool ok = expect(!run.geometry.give_micromap(1u, omm, 77u), "a mesh not built yet takes none");
    run.step();
    const std::uint64_t first = run.geometry.name(1u);
    ok &= expect(run.geometry.micromap_tag(1u) == 0u && run.geometry.give_micromap(1u, omm, 77u), "a built mesh takes a micromap's input");
    ok &= expect(!run.geometry.give_micromap(1u, omm, 77u) && !run.geometry.takes_micromap(1u), "once");
    ok &= expect(run.geometry.name(1u) == first && run.geometry.micromap_tag(1u) == 0u, "its structure stays named until the new one is built");
    run.step();
    ok &= expect(log.micromap_builds == 1u && log.micromaps == 1u && run.geometry.micromap_tag(1u) == 77u, "the micromap is built");
    ok &= expect(run.geometry.name(1u) != 0u && run.geometry.name(1u) != first && log.builds == 2u, "and the structure over it named in that frame");
    for (std::uint64_t i = 0; i < kFramesToRelease + kFramesInFlight + 2u; ++i) {
        run.step();
    }
    ok &= expect(log.micromaps == 1u && run.geometry.micromap_tag(1u) == 77u && run.geometry.stats().compacted == 1u,
                 "the structure is compacted and the micromap stays under it");
    ok &= expect(log.structures == 1u && run.geometry.stats().micromaps == 1u, "the old forms are gone");
    run.geometry.forget(1u);
    for (std::uint64_t i = 0; i < kFramesToRelease + 1u; ++i) {
        run.step();
    }
    ok &= expect(log.micromaps == 0u && log.structures == 0u && run.geometry.stats().micromaps == 0u, "a forgotten mesh's micromap goes with it");
    ok &= expect(log.faults == 0u, "and nothing a back end refuses was recorded");
    return ok;
}

}  // namespace

int main() {
    bool ok = ranges_fit_and_merge();
    ok &= a_ceiling_releases_the_least_recently_named();
    ok &= a_mesh_is_built_compacted_and_released();
    ok &= a_budget_spreads_builds();
    ok &= what_is_drawn_is_built_first_the_nearest_first();
    ok &= scratch_is_used_again_only_after_a_barrier();
    ok &= a_moving_mesh_is_refitted_and_forgotten();
    ok &= an_answer_waits_for_its_frame_slot();
    ok &= a_scene_is_built_over_its_instances();
    ok &= a_mesh_takes_a_micromap();
    if (ok) {
        std::cout << "rt geometry tests passed\n";
    }
    return ok ? 0 : 1;
}
