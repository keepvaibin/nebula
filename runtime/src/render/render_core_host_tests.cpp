// SPDX-License-Identifier: GPL-3.0-only
// The host's frame core over synthetic frames: compose's rows, pairing by identity, joints between two frames. No GPU.
#include "render/core/compose.h"
#include "render/core/frame_core.h"

#include <nebula_frame_list.h>
#include <nebula_model.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace galaxy::render;
using namespace galaxy::render::core;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool close_to(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps * (1.0f + std::fabs(a) + std::fabs(b));
}

bool same_rows(const float (*a)[4], const float (*b)[4], unsigned rows, float eps = 1e-4f) {
    for (unsigned r = 0; r < rows; ++r) {
        for (unsigned c = 0; c < 4u; ++c) {
            if (!close_to(a[r][c], b[r][c], eps)) {
                return false;
            }
        }
    }
    return true;
}

using Rows = std::array<std::array<float, 4>, 3>;

Rows rotation_y(float radians, float tx, float ty, float tz, float scale = 1.0f) {
    const float c = std::cos(radians) * scale, s = std::sin(radians) * scale;
    return {{{c, 0.0f, s, tx}, {0.0f, scale, 0.0f, ty}, {-s, 0.0f, c, tz}}};
}

const float (*as_rows(const Rows& r))[4] {
    return reinterpret_cast<const float (*)[4]>(r.data());
}

void test_compose_joints_under_camera() {
    const std::array<Rows, 2> joints{rotation_y(0.3f, 10.0f, 20.0f, 30.0f), rotation_y(-0.7f, 1.0f, 2.0f, 3.0f, 2.0f)};
    const Rows view = rotation_y(0.5f, -100.0f, 0.0f, -500.0f);
    const std::uint32_t scale_free = 1u;  // joint 0 without scale, joint 1 scaled

    ComposeInput in;
    in.joints = reinterpret_cast<const float (*)[4]>(joints.data());
    in.scale_flags = &scale_free;
    in.joint_count = 2u;
    in.view = as_rows(view);

    std::array<std::array<float, 4>, 2u * kRowsPerMatrix> out{};
    compose_rows(in, reinterpret_cast<float (*)[4]>(out.data()));

    float expected[3][4];
    concat34(as_rows(view), as_rows(joints[0]), expected);
    check(same_rows(reinterpret_cast<const float (*)[4]>(out.data()), expected, 3u), "a joint's place is camera * joint");
    for (unsigned r = 0; r < 3u; ++r) {
        for (unsigned c = 0; c < 3u; ++c) {
            check(out[3u + r][c] == expected[r][c], "a scale-free joint's normal rows are its place");
        }
    }
    // A scaled joint's normal rows are the inverse transpose: normal rows dot place rows give the identity.
    const auto* place = out.data() + kRowsPerMatrix;
    const auto* normal = place + 3u;
    for (unsigned a = 0; a < 3u; ++a) {
        for (unsigned b = 0; b < 3u; ++b) {
            const float d = place[a][0] * normal[b][0] + place[a][1] * normal[b][1] + place[a][2] * normal[b][2];
            check(close_to(d, a == b ? 1.0f : 0.0f), "a scaled joint's normal rows invert its place");
        }
    }

    std::array<std::array<float, 4>, 2u * kRowsPerMatrix> world{};
    compose_world_rows(in, reinterpret_cast<float (*)[4]>(world.data()));
    check(same_rows(reinterpret_cast<const float (*)[4]>(world.data()), as_rows(joints[0]), 3u),
          "world rows have no camera");
}

void test_compose_envelope_and_billboard() {
    const std::array<Rows, 2> joints{rotation_y(0.0f, 10.0f, 0.0f, 0.0f), rotation_y(0.0f, 30.0f, 0.0f, 0.0f)};
    const std::array<Rows, 2> inverse{rotation_y(0.0f, 0.0f, 0.0f, 0.0f), rotation_y(0.0f, 0.0f, 0.0f, 0.0f)};
    const std::uint32_t scale_free = 3u;
    const NebulaModelDrawMatrix matrices[2]{{1u, 0u}, {0u, 1u}};
    const NebulaModelEnvelope envelopes[1]{{0u, 2u}};
    const NebulaModelMix mixes[2]{{0u, 0.25f}, {1u, 0.75f}};
    std::array<NebulaModelJoint, 2> model_joints{};
    model_joints[1].billboard = 1u;

    ModelTables tables;
    tables.joints = model_joints.data();
    tables.draw_matrices = matrices;
    tables.envelopes = envelopes;
    tables.mixes = mixes;
    tables.inverse_binds = reinterpret_cast<const float (*)[4]>(inverse.data());
    tables.draw_matrix_count = 2u;
    tables.joint_count = 2u;
    tables.has_billboard = true;

    ComposeInput in;
    in.joints = reinterpret_cast<const float (*)[4]>(joints.data());
    in.scale_flags = &scale_free;
    in.model = &tables;
    in.joint_count = 2u;
    check(draw_matrices_of(in) == 2u, "a model's draw matrices count");

    std::array<std::array<float, 4>, 2u * kRowsPerMatrix> out{};
    compose_rows(in, reinterpret_cast<float (*)[4]>(out.data()));
    check(close_to(out[0][3], 25.0f), "an envelope mixes its joints by weight");
    check(close_to(out[0][0], 1.0f), "an envelope of equal rotations keeps it");

    const auto* board = out.data() + kRowsPerMatrix;
    check(close_to(board[0][0], 1.0f) && board[0][1] == 0.0f && board[0][2] == 0.0f && board[1][0] == 0.0f &&
              close_to(board[1][1], 1.0f) && board[2][0] == 0.0f && close_to(board[2][2], 1.0f),
          "a billboard's place faces the eye, keeping its scale");
    check(close_to(board[0][3], 30.0f), "a billboard keeps its position");
}

// Envelopes sharing joints read each joint's product from the scratch: the same bits as making it at every mix, for the
// eye rows and then the world rows on the same scratch (as the walker calls them).
void test_compose_envelope_products_shared() {
    const std::array<Rows, 3> joints{rotation_y(0.3f, 10.0f, -4.0f, 7.0f), rotation_y(-1.1f, 3.0f, 2.0f, -9.0f, 1.7f),
                                     rotation_y(2.4f, -6.0f, 5.0f, 1.0f, 0.6f)};
    const std::array<Rows, 3> inverse{rotation_y(-0.2f, 1.0f, 0.5f, -2.0f), rotation_y(0.9f, -3.0f, 0.0f, 4.0f, 0.8f),
                                      rotation_y(-2.0f, 0.25f, -1.5f, 3.0f, 1.3f)};
    const Rows view = rotation_y(0.7f, -40.0f, 12.0f, -300.0f);
    const std::uint32_t scale_free = 1u;
    const NebulaModelDrawMatrix matrices[4]{{1u, 0u}, {1u, 1u}, {0u, 2u}, {1u, 2u}};
    const NebulaModelEnvelope envelopes[3]{{0u, 2u}, {2u, 3u}, {5u, 2u}};
    const NebulaModelMix mixes[7]{{0u, 0.375f}, {1u, 0.625f}, {1u, 0.2f}, {2u, 0.3f}, {0u, 0.5f}, {2u, 0.9f}, {1u, 0.1f}};
    std::array<NebulaModelJoint, 3> model_joints{};

    ModelTables tables;
    tables.joints = model_joints.data();
    tables.draw_matrices = matrices;
    tables.envelopes = envelopes;
    tables.mixes = mixes;
    tables.inverse_binds = reinterpret_cast<const float (*)[4]>(inverse.data());
    tables.draw_matrix_count = 4u;
    tables.joint_count = 3u;
    tables.has_weighted = true;

    ComposeInput plain;
    plain.joints = reinterpret_cast<const float (*)[4]>(joints.data());
    plain.scale_flags = &scale_free;
    plain.model = &tables;
    plain.joint_count = 3u;
    plain.view = as_rows(view);
    ComposeInput shared = plain;
    std::array<float, 3u * 12u> skin{};
    std::array<std::uint8_t, 3u> made{};
    shared.skin = reinterpret_cast<float (*)[4]>(skin.data());
    shared.skin_made = made.data();

    std::array<std::array<float, 4>, 4u * kRowsPerMatrix> a{}, b{};
    compose_rows(plain, reinterpret_cast<float (*)[4]>(a.data()));
    compose_rows(shared, reinterpret_cast<float (*)[4]>(b.data()));
    check(std::memcmp(a.data(), b.data(), sizeof(a)) == 0, "shared joint products give an envelope's eye rows bit for bit");
    check(made[0] == 1u && made[1] == 1u && made[2] == 1u, "every mixed joint's product is made");
    compose_world_rows(plain, reinterpret_cast<float (*)[4]>(a.data()));
    compose_world_rows(shared, reinterpret_cast<float (*)[4]>(b.data()));
    check(std::memcmp(a.data(), b.data(), sizeof(a)) == 0, "the world rows read the eye rows' products bit for bit");
}

struct ListStore {
    std::vector<Rows> joints;
    std::uint32_t scale_free = 0xFFFFFFFFu;
    std::vector<NebulaFrameBlock> blocks;
    std::vector<NebulaFrameItem> items;
    std::vector<NebulaFrameCamera> cameras;
    std::array<NebulaFrameView, 1> views{};
    std::array<NebulaFrameConstants, 1> constants{};
    NebulaFrameList list{};
    ListData data;

    void finish() {
        list.blocks = blocks.data();
        list.block_count = static_cast<std::uint32_t>(blocks.size());
        list.items = items.data();
        list.item_count = static_cast<std::uint32_t>(items.size());
        list.cameras = cameras.data();
        list.camera_count = static_cast<std::uint32_t>(cameras.size());
        list.views = views.data();
        list.view_count = 1u;
        list.constants = constants.data();
        list.constants_count = 1u;
        data.list = &list;
    }
};

NebulaFrameBlock block_of(const Rows& joint, const std::uint32_t* scale, std::uint32_t slot, std::uint32_t generation) {
    NebulaFrameBlock block{};
    block.joints = as_rows(joint);
    block.scale_flags = scale;
    block.joint_count = 1u;
    block.slot = slot;
    block.generation = generation;
    block.camera = 0u;
    block.normals_from = NEBULA_NONE32;
    return block;
}

NebulaFrameItem item_of(std::uint32_t block, std::uint32_t slot, std::uint32_t generation, std::uint32_t part) {
    NebulaFrameItem item{};
    item.block = block;
    item.slot = slot;
    item.generation = generation;
    item.part = part;
    item.light_set = NEBULA_NONE32;
    return item;
}

void fill_camera(ListStore& store, const Rows& view) {
    NebulaFrameCamera camera{};
    std::memcpy(camera.view, view.data(), sizeof(camera.view));
    camera.role = NEBULA_CAMERA_SCENE;
    store.cameras.push_back(camera);
}

void test_pairing_by_identity() {
    ListStore before, now;
    before.joints = {rotation_y(0.0f, 0.0f, 0.0f, 0.0f), rotation_y(0.0f, 100.0f, 0.0f, 0.0f)};
    now.joints = {rotation_y(0.0f, 500.0f, 0.0f, 0.0f), rotation_y(0.0f, 10.0f, 0.0f, 0.0f), rotation_y(0.0f, 900.0f, 0.0f, 0.0f)};
    const Rows view = rotation_y(0.0f, 0.0f, 0.0f, -1000.0f);
    fill_camera(before, view);
    fill_camera(now, view);

    before.blocks = {block_of(before.joints[0], &before.scale_free, 7u, 1u), block_of(before.joints[1], &before.scale_free, 9u, 1u)};
    // A different order; slot 7 came back as a new life, so it isn't the same actor.
    now.blocks = {block_of(now.joints[0], &now.scale_free, 9u, 1u), block_of(now.joints[1], &now.scale_free, 7u, 1u),
                  block_of(now.joints[2], &now.scale_free, 7u, 2u)};
    before.items = {item_of(0u, 7u, 1u, 0u), item_of(1u, 9u, 1u, 0u), item_of(1u, 9u, 1u, 0u)};
    now.items = {item_of(1u, 7u, 1u, 0u), item_of(0u, 9u, 1u, 0u), item_of(0u, 9u, 1u, 0u), item_of(2u, 7u, 2u, 0u)};
    before.finish();
    now.finish();
    before.data.serial = 1u;
    now.data.serial = 2u;

    Pairing pairing;
    pairing.pair(before.data, nullptr);
    pairing.pair(now.data, &before.data);

    check(now.data.block_before[0] == 1u, "a block pairs by slot, wherever it is in the list");
    check(now.data.block_before[1] == 0u, "a block pairs by slot and generation");
    check(now.data.block_before[2] == NEBULA_NONE32, "a new generation has no pair");
    check(now.data.item_before[0] == 0u, "an item pairs by identity");
    check(now.data.item_before[1] == 1u && now.data.item_before[2] == 2u, "same identities pair nth with nth");
    check(now.data.item_before[3] == NEBULA_NONE32, "an item of a new generation has no pair");
    check(now.data.camera_before[0] == 0u, "cameras pair by role");
}

void test_wide_identity_parts_do_not_alias_generations() {
    ListStore before, now;
    before.joints = {rotation_y(0.0f,0.0f,0.0f,0.0f)};now.joints=before.joints;
    before.blocks={block_of(before.joints[0],&before.scale_free,1u,1u)};
    now.blocks={block_of(now.joints[0],&now.scale_free,1u,1u)};
    before.blocks[0].camera=now.blocks[0].camera=NEBULA_NONE32;
    before.items = {item_of(0u, 5u, 1u, 0x10000u), item_of(0u, 5u, 0x10001u, 0u)};
    now.items = {before.items[1], before.items[0], item_of(0u, 5u, 1u, 0u)};
    before.finish(); now.finish();
    before.data.serial = 10; now.data.serial = 11;
    Pairing pairing;
    pairing.pair(now.data, &before.data);
    check(now.data.item_before[0] == 1u && now.data.item_before[1] == 0u && now.data.item_before[2] == NEBULA_NONE32,
          "all generation and part bits participate in identity pairing");
}

void test_joints_between_frames() {
    ListStore before, now;
    before.joints = {rotation_y(0.0f, 0.0f, 0.0f, 0.0f)};
    now.joints = {rotation_y(0.2f, 40.0f, 0.0f, 0.0f)};
    const Rows view = rotation_y(0.0f, 0.0f, 0.0f, -1000.0f);
    fill_camera(before, view);
    fill_camera(now, view);
    before.blocks = {block_of(before.joints[0], &before.scale_free, 3u, 1u)};
    now.blocks = {block_of(now.joints[0], &now.scale_free, 3u, 1u)};
    before.items = {item_of(0u, 3u, 1u, 0u)};
    now.items = {item_of(0u, 3u, 1u, 0u)};
    before.finish();
    now.finish();
    before.data.block_camera = {0u};
    now.data.block_camera = {0u};
    before.data.serial = 1u;
    now.data.serial = 2u;

    Pairing pairing;
    pairing.pair(before.data, nullptr);
    pairing.pair(now.data, &before.data);
    pairing.prepare_motion(now.data, before.data);
    check(now.data.motion_first[0] != NEBULA_NONE32, "a moving joint has a motion");
    if (now.data.motion_first[0] == NEBULA_NONE32) {
        return;
    }

    float rows[3][4];
    joints_at(now.data, now.blocks[0], 0u, 1.0f, nullptr, rows);
    check(same_rows(rows, as_rows(now.joints[0]), 3u), "at t = 1 a joint is this frame's");
    joints_at(now.data, now.blocks[0], 0u, 0.0f, nullptr, rows);
    check(same_rows(rows, as_rows(before.joints[0]), 3u), "at t = 0 a joint is the frame before's");
    joints_at(now.data, now.blocks[0], 0u, 0.5f, nullptr, rows);
    check(close_to(rows[0][3], 20.0f), "half way a joint's position is half way");
    check(close_to(rows[0][0], std::cos(0.1f)) && close_to(rows[0][2], std::sin(0.1f)), "half way a joint's rotation is half way");
}

void test_teleport_shows_this_frame() {
    ListStore before, now;
    before.joints = {rotation_y(0.0f, 0.0f, 0.0f, 0.0f)};
    now.joints = {rotation_y(0.0f, 90000.0f, 0.0f, 0.0f)};
    const Rows view = rotation_y(0.0f, 0.0f, 0.0f, -1000.0f);
    fill_camera(before, view);
    fill_camera(now, view);
    before.blocks = {block_of(before.joints[0], &before.scale_free, 3u, 1u)};
    now.blocks = {block_of(now.joints[0], &now.scale_free, 3u, 1u)};
    before.finish();
    now.finish();
    before.data.block_camera = {0u};
    now.data.block_camera = {0u};
    before.data.serial = 1u;
    now.data.serial = 2u;

    Pairing pairing;
    pairing.pair(before.data, nullptr);
    pairing.pair(now.data, &before.data);
    pairing.prepare_motion(now.data, before.data);
    check(now.data.motion_first[0] == NEBULA_NONE32, "a teleport has no motion: the block shows this frame at every t");
    check(pairing.teleports == 1u, "a teleport is counted");
}

void test_eye_space_world_rows() {
    const Affine34 world={1,0,0,500,0,1,0,200,0,0,1,-10000};
    for (const float yaw : {0.0f, .1f, 1.0f}) {
        const float c=std::cos(yaw), s=std::sin(yaw);
        const Affine34 view={c,0,s,50,0,1,0,-10,-s,0,c,30};
        const Affine34 eye=affine_multiply(view,world);
        Affine34 inverse;check(affine_inverse(view,inverse),"camera is invertible");
        std::uint32_t flags=1u;
        ComposeInput in;in.joints=reinterpret_cast<const float(*)[4]>(eye.data());in.scale_flags=&flags;in.joint_count=1;
        in.world_view=reinterpret_cast<const float(*)[4]>(inverse.data());
        float rows[6][4]{};compose_world_rows(in,rows);
        check(same_rows(rows,reinterpret_cast<const float(*)[4]>(world.data()),3u),"eye-space model world texture coordinates stay fixed as the camera rotates");
    }
}

// A list asks its screen probes once, on its real walk: frames generated between two lists see the scene at t < 1.
void test_probes_asked_on_the_real_walk() {
    ListData list;
    check(!list.probes_done, "a new list has asked no probes");
    check(!list.probes_due(true), "a generated frame asks no probes");
    check(list.probes_due(false), "the real walk asks the list's probes");
    list.probes_done = true;
    check(!list.probes_due(false), "a list's probes are asked once");
    check(!list.probes_due(true), "a generated frame after the real walk asks none");
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // Exhaust the comparison/logic combinations and representative reference extremes. A proof must never
    // label a potentially rejecting alpha test as uniformly passing, for any input alpha in the byte range.
    const auto compare = [](unsigned op, unsigned a, unsigned r) {
        switch (op) {
        case 0: return false; case 1: return a < r; case 2: return a == r; case 3: return a <= r;
        case 4: return a > r; case 5: return a != r; case 6: return a >= r; default: return true;
        }
    };
    unsigned proved = 0;
    for (unsigned c0=0; c0<8; ++c0) for (unsigned c1=0; c1<8; ++c1) for (unsigned op=0; op<4; ++op) {
        if (!nebula_alpha_test_always(c0,c1,op)) continue;
        ++proved;
        for (unsigned a=0; a<256; ++a) for (unsigned r0 : {0u,127u,255u}) for (unsigned r1 : {0u,127u,255u}) {
            const bool x=compare(c0,a,r0), y=compare(c1,a,r1);
            check(op==0 ? x&&y : op==1 ? x||y : op==2 ? x!=y : x==y,
                  "pixel shading proof preserves every passing alpha sample");
        }
    }
    check(proved == 20u, "all constant truth-table cases are recognized");
    test_eye_space_world_rows();
    test_compose_joints_under_camera();
    test_compose_envelope_and_billboard();
    test_compose_envelope_products_shared();
    test_pairing_by_identity();
    test_wide_identity_parts_do_not_alias_generations();
    test_joints_between_frames();
    test_teleport_shows_this_frame();
    test_probes_asked_on_the_real_walk();
    if (failures != 0) {
        std::printf("%d failed\n", failures);
        return 1;
    }
    return 0;
}
