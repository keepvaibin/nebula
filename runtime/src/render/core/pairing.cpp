// SPDX-License-Identifier: GPL-3.0-only
#include "render/core/frame_core.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace galaxy::render::core {

namespace {

Affine34 affine_of(const float (*rows)[4]) noexcept {
    Affine34 out;
    std::memcpy(out.data(), rows, 12u * sizeof(float));
    return out;
}

}  // namespace

void Pairing::pair(ListData& now, const ListData* before) {
    if (now.paired) {
        return;
    }
    now.paired = true;
    const NebulaFrameList& list = *now.list;
    now.block_before.assign(list.block_count, NEBULA_NONE32);
    now.camera_before.assign(list.camera_count, NEBULA_NONE32);
    now.constants_before.assign(list.constants_count, NEBULA_NONE32);
    now.view_before.assign(list.view_count, NEBULA_NONE32);
    now.light_set_before.assign(list.light_set_count, NEBULA_NONE32);
    now.item_before.assign(list.item_count, NEBULA_NONE32);
    now.motion_ready = false;
    if (before == nullptr) {
        return;
    }
    const NebulaFrameList& last = *before->list;
    now.cut = (list.flags & NEBULA_FRAME_CAMERA_CUT) != 0u;

    // Cameras pair by what they are for, in order.
    {
        std::array<std::uint32_t, NEBULA_CAMERA_ROLE_COUNT> next{};
        for (std::uint32_t c = 0; c < list.camera_count; ++c) {
            const std::uint32_t role = std::min<std::uint32_t>(list.cameras[c].role, NEBULA_CAMERA_ROLE_COUNT - 1u);
            std::uint32_t seen = 0u;
            for (std::uint32_t p = 0; p < last.camera_count; ++p) {
                if (std::min<std::uint32_t>(last.cameras[p].role, NEBULA_CAMERA_ROLE_COUNT - 1u) == role && seen++ == next[role]) {
                    now.camera_before[c] = p;
                    break;
                }
            }
            ++next[role];
        }
    }

    // Blocks by slot: a direct table, stamped by list.
    const std::uint32_t stamp = before->serial + 1u;
    next_in_slot_.resize(last.block_count);
    used_.assign(last.block_count, 0u);
    for (std::uint32_t i = last.block_count; i != 0u; --i) {
        const std::uint32_t b = i - 1u;
        const std::uint32_t slot = last.blocks[b].slot;
        if (slot >= slots_.size()) {
            slots_.resize(std::max<std::size_t>(slot + 1u, slots_.size() * 2u));
        }
        SlotEntry& entry = slots_[slot];
        next_in_slot_[b] = entry.list == stamp ? entry.first : NEBULA_NONE32;
        entry.first = b;
        entry.list = stamp;
    }
    for (std::uint32_t b = 0; b < list.block_count; ++b) {
        const NebulaFrameBlock& block = list.blocks[b];
        if (block.slot >= slots_.size() || slots_[block.slot].list != stamp) {
            continue;
        }
        const std::uint32_t camera_before = block.camera != NEBULA_NONE32 ? now.camera_before[block.camera] : NEBULA_NONE32;
        for (std::uint32_t p = slots_[block.slot].first; p != NEBULA_NONE32; p = next_in_slot_[p]) {
            const NebulaFrameBlock& other = last.blocks[p];
            if (used_[p] != 0u || other.generation != block.generation || other.joint_count != block.joint_count ||
                other.model != block.model || other.calc_mode != block.calc_mode) {
                continue;
            }
            const std::uint32_t other_camera = other.camera != NEBULA_NONE32 ? other.camera : NEBULA_NONE32;
            if (other_camera != camera_before) {
                continue;
            }
            used_[p] = 1u;
            now.block_before[b] = p;
            break;
        }
    }

    // Items pair by identity, nth with nth: the rest of an item's data pairs through its pair.
    {
        struct ItemKey {
            std::uint32_t slot, generation, part;
            bool operator==(const ItemKey&) const = default;
        };
        struct ItemHash {
            std::size_t operator()(const ItemKey& key) const noexcept {
                std::uint64_t h = key.slot;
                h = (h ^ key.generation) * 0x100000001B3ull;
                return static_cast<std::size_t>((h ^ key.part) * 0x100000001B3ull);
            }
        };
        const auto item_key = [](const NebulaFrameItem& item) {
            return ItemKey{item.slot, item.generation, item.part};
        };
        std::unordered_map<ItemKey, std::uint32_t, ItemHash> items;
        items.reserve(last.item_count);
        next_item_.resize(last.item_count);
        for (std::uint32_t i = last.item_count; i != 0u; --i) {
            const auto [found, inserted] = items.try_emplace(item_key(last.items[i - 1u]), i - 1u);
            next_item_[i - 1u] = inserted ? NEBULA_NONE32 : found->second;
            found->second = i - 1u;
        }
        for (std::uint32_t i = 0; i < list.item_count; ++i) {
            const NebulaFrameItem& item = list.items[i];
            const auto found = items.find(item_key(item));
            if (found == items.end()) {
                continue;
            }
            const std::uint32_t pair = found->second;
            if (next_item_[pair] != NEBULA_NONE32) found->second = next_item_[pair];
            else items.erase(found);  // a previous draw can be consumed only once
            const NebulaFrameItem& other = last.items[pair];
            if (item.pipeline != other.pipeline || item.mesh != other.mesh || item.textures != other.textures ||
                item.matrix != other.matrix || item.flags != other.flags || item.source_hash != other.source_hash ||
                now.block_before[item.block] != other.block) {
                continue;
            }
            if (((item.flags | other.flags) & NEBULA_ITEM_DISCRETE_VERTICES) != 0u) {
                continue;
            }
            now.item_before[i] = pair;
            if (now.constants_before[item.constants] == NEBULA_NONE32) {
                now.constants_before[item.constants] = other.constants;
            }
            if (now.view_before[item.view] == NEBULA_NONE32 &&
                list.views[item.view].orthographic == last.views[other.view].orthographic) {
                now.view_before[item.view] = other.view;
            }
            if (item.light_set != NEBULA_NONE32 && other.light_set != NEBULA_NONE32 &&
                now.light_set_before[item.light_set] == NEBULA_NONE32 &&
                list.light_sets[item.light_set].follows_camera == last.light_sets[other.light_set].follows_camera) {
                const auto camera = list.light_sets[item.light_set].camera_plus_one;
                const auto previous_camera = last.light_sets[other.light_set].camera_plus_one;
                const bool same_camera = camera == 0u ? previous_camera == 0u
                    : camera <= list.camera_count && now.camera_before[camera - 1u] != NEBULA_NONE32 &&
                      now.camera_before[camera - 1u] + 1u == previous_camera;
                if (same_camera) now.light_set_before[item.light_set] = other.light_set;
            }
        }
    }
}

void Pairing::prepare_motion(ListData& now, const ListData& before) {
    if (now.motion_ready) {
        return;
    }
    now.motion_ready = true;
    const NebulaFrameList& list = *now.list;
    const NebulaFrameList& last = *before.list;

    static const CameraCutLimits limits{};
    now.camera_motion.assign(list.camera_count, CameraMotion{});
    for (std::uint32_t c = 0; c < list.camera_count; ++c) {
        const Affine34 b = affine_of(list.cameras[c].view);
        if (now.camera_before[c] == NEBULA_NONE32) {
            camera_motion(nullptr, &b, 0.0f, limits, now.camera_motion[c]);
            continue;
        }
        const Affine34 a = affine_of(last.cameras[now.camera_before[c]].view);
        const bool scene = list.cameras[c].role == NEBULA_CAMERA_SCENE;
        camera_motion(&a, &b, scene ? camera_move_ : 0.0f, limits, now.camera_motion[c]);
        if (now.cut && now.camera_motion[c].known) {
            now.camera_motion[c].cut = true;
        }
        if (scene) {
            camera_move_ = now.camera_motion[c].known && !now.cut ? now.camera_motion[c].moved : 0.0f;
            if (now.camera_motion[c].cut) {
                ++camera_cuts;
                game_cuts += now.cut ? 1u : 0u;
            }
        }
    }

    now.motion_first.assign(list.block_count, NEBULA_NONE32);
    now.motions.clear();
    now.eye_motions.clear();
    now.joint_motion.clear();
    for (std::uint32_t b = 0; b < list.block_count; ++b) {
        const NebulaFrameBlock& block = list.blocks[b];
        if ((block.flags & NEBULA_BLOCK_NO_MOTION) != 0u) {
            continue;
        }
        const std::uint32_t p = now.block_before[b];
        const NebulaFrameBlock* other = p != NEBULA_NONE32 ? &last.blocks[p] : nullptr;
        const auto first = static_cast<std::uint32_t>(now.joint_motion.size());

        if (block.camera == NEBULA_NONE32 || block.calc_mode == 1u) {
            // Eye-space joints: taken apart under the camera their draws are for; no pair holds still in the world.
            const std::uint32_t camera = now.block_camera[b];
            if (camera == NEBULA_NONE32 && other == nullptr) {
                continue;
            }
            static const CameraMotion kNoCamera{};
            const CameraMotion& motion = camera != NEBULA_NONE32 ? now.camera_motion[camera] : kNoCamera;
            for (std::uint32_t j = 0; j < block.joint_count; ++j) {
                const Affine34 bb = affine_of(block.joints + 3u * j);
                const Affine34 a = other != nullptr ? affine_of(other->joints + 3u * j) : bb;
                EyeMotion eye;
                eye_motion(other != nullptr ? &a : nullptr, bb, motion, camera == NEBULA_NONE32, eye);
                teleports += eye.snapped ? 1u : 0u;
                flips += eye.flipped ? 1u : 0u;
                now.joint_motion.push_back(static_cast<std::uint32_t>(now.eye_motions.size()));
                now.eye_motions.push_back(eye);
            }
            now.motion_first[b] = first;
            continue;
        }

        if (other == nullptr || (now.camera_motion[block.camera].known && now.camera_motion[block.camera].cut)) {
            continue;
        }
        bool any = false;
        for (std::uint32_t j = 0; j < block.joint_count; ++j) {
            const float (*jb)[4] = block.joints + 3u * j;
            const float (*ja)[4] = other->joints + 3u * j;
            if (std::memcmp(ja, jb, 12u * sizeof(float)) == 0) {
                now.joint_motion.push_back(NEBULA_NONE32);
                continue;
            }
            const Affine34 a = affine_of(ja);
            const Affine34 bb = affine_of(jb);
            AffineMotion motion;
            affine_motion(a, bb, motion);
            const float (*v)[4] = list.cameras[block.camera].view;
            const float x = v[0][0] * jb[0][3] + v[0][1] * jb[1][3] + v[0][2] * jb[2][3] + v[0][3];
            const float y = v[1][0] * jb[0][3] + v[1][1] * jb[1][3] + v[1][2] * jb[2][3] + v[1][3];
            const float z = v[2][0] * jb[0][3] + v[2][1] * jb[1][3] + v[2][2] * jb[2][3] + v[2][3];
            const Plausibility plausible = plausibility(motion, std::sqrt(x * x + y * y + z * z));
            if (plausible == Plausibility::teleport) {
                ++teleports;
                now.joint_motion.push_back(NEBULA_NONE32);
                continue;
            }
            if (plausible == Plausibility::flip) {
                ++flips;
                take_rotation_of_b(motion);
            }
            now.joint_motion.push_back(static_cast<std::uint32_t>(now.motions.size()));
            now.motions.push_back(motion);
            any = true;
        }
        if (any) {
            now.motion_first[b] = first;
        } else {
            now.joint_motion.resize(first);
        }
    }
}

void joints_at(const ListData& now, const NebulaFrameBlock& block, std::uint32_t block_index, float t, const Affine34* camera_t,
               float (*out)[4]) noexcept {
    const std::uint32_t first = now.motion_first[block_index];
    if (block.camera == NEBULA_NONE32 || block.calc_mode == 1u) {
        static const CameraMotion kNoCamera{};
        const std::uint32_t camera = now.block_camera[block_index];
        const CameraMotion& motion = camera != NEBULA_NONE32 ? now.camera_motion[camera] : kNoCamera;
        const Affine34 at_camera = camera_t != nullptr ? *camera_t : Affine34{};
        for (std::uint32_t j = 0; j < block.joint_count; ++j) {
            const Affine34 at = eye_at(now.eye_motions[now.joint_motion[first + j]], motion, at_camera, t);
            std::memcpy(out + 3u * j, at.data(), 12u * sizeof(float));
        }
        return;
    }
    for (std::uint32_t j = 0; j < block.joint_count; ++j) {
        const std::uint32_t motion = now.joint_motion[first + j];
        if (motion == NEBULA_NONE32) {
            std::memcpy(out + 3u * j, block.joints + 3u * j, 12u * sizeof(float));
        } else {
            const Affine34 at = affine_motion_at(now.motions[motion], t);
            std::memcpy(out + 3u * j, at.data(), 12u * sizeof(float));
        }
    }
}

}  // namespace galaxy::render::core
