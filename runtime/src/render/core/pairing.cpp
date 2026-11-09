// SPDX-License-Identifier: GPL-3.0-only
#include "render/core/frame_core.h"

#include "galaxy/run_options.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace galaxy::render::core {

namespace {

Affine34 affine_of(const float (*rows)[4]) noexcept {
    Affine34 out;
    std::memcpy(out.data(), rows, 12u * sizeof(float));
    return out;
}

// Whether the lights two sets share (the first of each, as many as the smaller set holds) follow the camera alike.
bool same_follow(const NebulaFrameList& a, const NebulaFrameLightSet& set_a, const NebulaFrameList& b,
                 const NebulaFrameLightSet& set_b) noexcept {
    const std::uint32_t common = std::min(set_a.count, set_b.count);
    for (std::uint32_t l = 0u; l < common; ++l) {
        const std::uint32_t bit_a = a.light_follow[set_a.follow_first + l / 32u] >> (l % 32u) & 1u;
        const std::uint32_t bit_b = b.light_follow[set_b.follow_first + l / 32u] >> (l % 32u) & 1u;
        if (bit_a != bit_b) {
            return false;
        }
    }
    return true;
}

}  // namespace

void Pairing::pair(ListData& now, const ListData* before) {
    if (now.paired) {
        return;
    }
    now.paired = true;
#if defined(NEBULA_DEV)
    if (::galaxy::run_option_flag(::galaxy::run_options().test_flicker_log, false)) {
        log_flicker(now);
    }
#endif
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

    // Cameras pair by what they are for. The game's own (scene, mirror, shadow) pair in order. The OTHER ones are views a
    // draw named (2D model views, capture views, a world site drawn in a 2D phase), listed in the order a frame first used
    // them, which can change from frame to frame: those pair by the nearest view matrix, the closest pairs first.
    {
        std::array<std::uint32_t, NEBULA_CAMERA_ROLE_COUNT> next{};
        const auto role_of = [](const NebulaFrameCamera& camera) {
            return std::min<std::uint32_t>(camera.role, NEBULA_CAMERA_ROLE_COUNT - 1u);
        };
        std::uint64_t other_now = 0u, other_last = 0u;
        for (std::uint32_t c = 0; c < list.camera_count; ++c) {
            other_now += role_of(list.cameras[c]) == NEBULA_CAMERA_OTHER ? 1u : 0u;
        }
        for (std::uint32_t p = 0; p < last.camera_count; ++p) {
            other_last += role_of(last.cameras[p]) == NEBULA_CAMERA_OTHER ? 1u : 0u;
        }
        // A list with very many views (the candidates are every pair) keeps the order rule instead of costing a sort.
        const bool nearest = other_now * other_last <= kNearestCameraPairs;
        camera_pairs_.clear();
        for (std::uint32_t c = 0; nearest && c < list.camera_count; ++c) {
            if (role_of(list.cameras[c]) != NEBULA_CAMERA_OTHER) {
                continue;
            }
            for (std::uint32_t p = 0; p < last.camera_count; ++p) {
                if (role_of(last.cameras[p]) != NEBULA_CAMERA_OTHER) {
                    continue;
                }
                float distance = 0.0f;
                for (std::uint32_t row = 0; row < 3u; ++row) {
                    for (std::uint32_t column = 0; column < 4u; ++column) {
                        const float d = list.cameras[c].view[row][column] - last.cameras[p].view[row][column];
                        distance += d * d;
                    }
                }
                // An unreadable matrix is the farthest candidate, not an unordered one.
                if (!(distance <= std::numeric_limits<float>::max())) {
                    distance = std::numeric_limits<float>::max();
                }
                camera_pairs_.push_back({distance, c, p});
            }
        }
        // Equal distances (the same views in both lists) keep the list order, so identical cameras pair nth with nth.
        std::sort(camera_pairs_.begin(), camera_pairs_.end(), [](const CameraCandidate& a, const CameraCandidate& b) {
            if (a.distance != b.distance) return a.distance < b.distance;
            if (a.now != b.now) return a.now < b.now;
            return a.before < b.before;
        });
        camera_used_.assign(last.camera_count, 0u);
        for (const CameraCandidate& candidate : camera_pairs_) {
            if (now.camera_before[candidate.now] == NEBULA_NONE32 && camera_used_[candidate.before] == 0u) {
                now.camera_before[candidate.now] = candidate.before;
                camera_used_[candidate.before] = 1u;
            }
        }
        for (std::uint32_t c = 0; c < list.camera_count; ++c) {
            const std::uint32_t role = role_of(list.cameras[c]);
            if (nearest && role == NEBULA_CAMERA_OTHER) {
                continue;
            }
            std::uint32_t seen = 0u;
            for (std::uint32_t p = 0; p < last.camera_count; ++p) {
                if (role_of(last.cameras[p]) == role && seen++ == next[role]) {
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
                same_follow(list, list.light_sets[item.light_set], last, last.light_sets[other.light_set])) {
                const auto camera = list.light_sets[item.light_set].camera_plus_one;
                const auto previous_camera = last.light_sets[other.light_set].camera_plus_one;
                const bool same_camera = camera == 0u ? previous_camera == 0u
                    : camera <= list.camera_count && now.camera_before[camera - 1u] != NEBULA_NONE32 &&
                      now.camera_before[camera - 1u] + 1u == previous_camera;
                if (same_camera) now.light_set_before[item.light_set] = other.light_set;
            }
        }
    }
#if defined(NEBULA_DEV)
    if (::galaxy::run_option_flag(::galaxy::run_options().test_pair_log, false)) {
        log_lost_ring_pairs(now, last);
    }
#endif
}

namespace {

std::uint64_t fnv(const void* data, std::size_t bytes, std::uint64_t h = 0xCBF29CE484222325ull) noexcept {
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < bytes; ++i) {
        h = (h ^ p[i]) * 0x100000001B3ull;
    }
    return h;
}

// A light as shading resolves it: whole steps of its 0-255 colour, its direction at 1e-4, its eye-space position as a
// direction at 1e-4 and a distance, and the distance and attenuation terms to 13 mantissa bits (about 1e-4 relative).
// Bit noise below that (a far sun's position through a view matrix that moves in its last bits) is the same light.
std::uint64_t shading_hash(const NebulaFrameLight& light) noexcept {
    const auto steps = [](double v, double per_unit) { return static_cast<std::int64_t>(std::llround(v * per_unit)); };
    const auto relative = [](float v) {
        std::uint32_t bits = 0u;
        std::memcpy(&bits, &v, sizeof(bits));
        return (bits + 0x200u) & ~0x3FFu;
    };
    const double length = std::sqrt(static_cast<double>(light.position[0]) * light.position[0] +
                                    static_cast<double>(light.position[1]) * light.position[1] +
                                    static_cast<double>(light.position[2]) * light.position[2]);
    std::int64_t fixed[10]{};
    for (unsigned c = 0; c < 4u; ++c) fixed[c] = steps(light.color[c], 1.0);
    for (unsigned d = 0; d < 3u; ++d) {
        fixed[4u + d] = steps(light.direction[d], 1e4);
        fixed[7u + d] = length > 0.0 ? steps(light.position[d] / length, 1e4) : 0;
    }
    std::uint32_t terms[7]{relative(static_cast<float>(length))};
    for (unsigned t = 0; t < 3u; ++t) {
        terms[1u + t] = relative(light.angle[t]);
        terms[4u + t] = relative(light.distance[t]);
    }
    return fnv(terms, sizeof(terms), fnv(fixed, sizeof(fixed)));
}

// A colour row in whole 1/255 steps.
std::uint64_t row_hash(const float* row) noexcept {
    std::int64_t fixed[4]{};
    for (unsigned c = 0; c < 4u; ++c) fixed[c] = static_cast<std::int64_t>(std::llround(static_cast<double>(row[c]) * 255.0));
    return fnv(fixed, sizeof(fixed));
}

// The largest change of a colour row's channels, in 1/255 steps.
float row_change(const float* a, const float* b) noexcept {
    float most = 0.0f;
    for (unsigned c = 0; c < 4u; ++c) most = std::max(most, std::fabs(a[c] - b[c]) * 255.0f);
    return most;
}

}  // namespace

// Flicker the game makes, before any picture: per draw identity (slot, generation, part, material, pipeline, and its
// number among draws of that identity in the list), across game frames. Three kinds, each A, B, A over three lists:
// presence (drawn, gone for one list, drawn again, having been drawn the list before that too), a light of its set (the
// light's whole record), a pixel constant row of its material (colour registers). Each material and kind is logged
// once with the light or row that moved; totals every 600 lists, the top materials first.
void Pairing::log_flicker(const ListData& now) {
    const NebulaFrameList& list = *now.list;
    const std::uint32_t serial = now.serial;
    static const char* const kKind[3] = {"drawn, gone one frame, drawn again", "a light went A, B, A",
                                         "a colour row went A, B, A"};
    flicker_nth_.clear();
    for (std::uint32_t i = 0; i < list.item_count; ++i) {
        const NebulaFrameItem& item = list.items[i];
        const std::uint32_t ident[5] = {item.slot, item.generation, item.part, item.source_hash, item.pipeline};
        const std::uint64_t base = fnv(ident, sizeof(ident));
        const std::uint32_t nth = flicker_nth_[base]++;
        const std::uint64_t key = fnv(&nth, sizeof(nth), base);

        std::uint32_t light_count = 0u;
        std::uint64_t light[kFlickerSlots]{};
        const NebulaFrameLight* light_record = nullptr;
        if (item.light_set != NEBULA_NONE32) {
            const NebulaFrameLightSet& set = list.light_sets[item.light_set];
            light_count = set.count;
            light_record = list.lights + set.first;
            for (std::uint32_t l = 0; l < set.count && l < kFlickerSlots; ++l) {
                light[l] = shading_hash(list.lights[set.first + l]);
            }
        }
        std::uint32_t row_count = 0u;
        std::uint64_t row[kFlickerSlots]{};
        const NebulaFrameConstants& constants = list.constants[item.constants];
        if (constants.rows != nullptr) {
            row_count = constants.pixel_rows;
            for (std::uint32_t r = 0; r < constants.pixel_rows && r < kFlickerSlots; ++r) {
                row[r] = row_hash(constants.rows[constants.vertex_rows + r]);
            }
        }

        FlickerEntry& entry = flicker_[key];
        const std::uint32_t gap = entry.last != 0u ? serial - entry.last : 32u;
        const std::uint32_t bits = (gap >= 32u ? 0u : entry.bits << gap) | 1u;
        int kind = -1;
        std::uint32_t which = 0u;
        if ((bits & 0xFu) == 0xDu) {
            kind = 0;
        } else if ((bits & 7u) == 7u) {
            for (std::uint32_t l = 0; kind < 0 && l < kFlickerSlots && l < light_count; ++l) {
                if (entry.light_count[1] == light_count && entry.light_count[0] == light_count &&
                    light[l] == entry.light[1][l] && light[l] != entry.light[0][l]) {
                    kind = 1;
                    which = l;
                }
            }
            for (std::uint32_t r = 0; kind < 0 && r < kFlickerSlots && r < row_count; ++r) {
                // A step of 1/255 is an animated colour's rounding; two or more can be seen.
                if (entry.row_count[1] == row_count && entry.row_count[0] == row_count && row[r] == entry.row[1][r] &&
                    row_change(constants.rows[constants.vertex_rows + r], entry.row_before[r]) >= 2.0f) {
                    kind = 2;
                    which = r;
                }
            }
        }
        if (kind >= 0) {
            // Of a draw with no material name, the slot (bit 33 set): "[names] slot" lines name it.
            const std::uint64_t who = item.source_hash != 0u ? item.source_hash : (1ull << 32u | item.slot);
            const std::uint64_t count_key = who << 2u | static_cast<std::uint32_t>(kind);
            if (flicker_counts_[count_key]++ == 0u) {
                if (kind == 1) {
                    const NebulaFrameLight& l = list.lights[list.light_sets[item.light_set].first + which];
                    std::fprintf(stderr,
                                 "[flicker] %s: material %08x slot %u part %u light %u of %u colour %.3f %.3f %.3f %.3f "
                                 "position %.1f %.1f %.1f direction %.3f %.3f %.3f\n",
                                 kKind[kind], item.source_hash, item.slot, item.part, which, light_count, l.color[0],
                                 l.color[1], l.color[2], l.color[3], l.position[0], l.position[1], l.position[2],
                                 l.direction[0], l.direction[1], l.direction[2]);
                    // A (this list and the one two before) and B (the list before), every field of the light.
                    const auto print_light = [](const char* pWhich, std::uint32_t at, const NebulaFrameLight& r) {
                        std::fprintf(stderr,
                                     "[flicker]   %s list %u: colour %.3f %.3f %.3f %.3f angle %.4f %.4f %.4f distance %.4f %.4f %.4f "
                                     "position %.1f %.1f %.1f direction %.4f %.4f %.4f\n",
                                     pWhich, at, r.color[0], r.color[1], r.color[2], r.color[3], r.angle[0], r.angle[1],
                                     r.angle[2], r.distance[0], r.distance[1], r.distance[2], r.position[0], r.position[1],
                                     r.position[2], r.direction[0], r.direction[1], r.direction[2]);
                    };
                    print_light("A", serial, l);
                    print_light("B", serial - 1u, entry.light_before[which]);
                } else if (kind == 2) {
                    const float* v = constants.rows[constants.vertex_rows + which];
                    std::fprintf(stderr, "[flicker] %s: material %08x slot %u part %u row %u of %u now %.4f %.4f %.4f %.4f\n",
                                 kKind[kind], item.source_hash, item.slot, item.part, which, row_count, v[0], v[1], v[2],
                                 v[3]);
                    const float* b = entry.row_before[which];
                    std::fprintf(stderr, "[flicker]   B list %u: row %u %.4f %.4f %.4f %.4f\n", serial - 1u, which, b[0], b[1],
                                 b[2], b[3]);
                } else {
                    std::fprintf(stderr, "[flicker] %s: material %08x slot %u part %u flags %x pipeline %u\n", kKind[kind],
                                 item.source_hash, item.slot, item.part, item.flags, item.pipeline);
                }
            }
        }
        entry.last = serial;
        entry.bits = bits;
        entry.light_count[1] = entry.light_count[0];
        entry.light_count[0] = light_count;
        entry.row_count[1] = entry.row_count[0];
        entry.row_count[0] = row_count;
        std::memcpy(entry.light[1], entry.light[0], sizeof(entry.light[0]));
        std::memcpy(entry.light[0], light, sizeof(light));
        std::memcpy(entry.row[1], entry.row[0], sizeof(entry.row[0]));
        std::memcpy(entry.row[0], row, sizeof(row));
        for (std::uint32_t l = 0; l < kFlickerSlots; ++l) {
            entry.light_before[l] = light_record != nullptr && l < light_count ? light_record[l] : NebulaFrameLight{};
        }
        for (std::uint32_t r = 0; r < kFlickerSlots; ++r) {
            const bool has = constants.rows != nullptr && r < row_count;
            for (unsigned c = 0; c < 4u; ++c) {
                entry.row_before[r][c] = has ? constants.rows[constants.vertex_rows + r][c] : 0.0f;
            }
        }
    }
    if (++flicker_lists_ % 600u != 0u) {
        return;
    }
    for (auto it = flicker_.begin(); it != flicker_.end();) {
        it = serial - it->second.last > 64u ? flicker_.erase(it) : std::next(it);
    }
    std::uint32_t totals[3]{};
    std::vector<std::pair<std::uint32_t, std::uint64_t>> ranked;
    for (const auto& [count_key, count] : flicker_counts_) {
        totals[count_key & 3u] += count;
        ranked.emplace_back(count, count_key);
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::fprintf(stderr, "[flicker] lists %u: presence %u light %u colour %u\n", flicker_lists_, totals[0], totals[1], totals[2]);
    for (std::size_t r = 0; r < ranked.size() && r < 12u; ++r) {
        const std::uint64_t who = ranked[r].second >> 2u;
        std::fprintf(stderr, "[flicker]   %s %08x %s: %u\n", (who >> 32u) != 0u ? "slot" : "material",
                     static_cast<std::uint32_t>(who), kKind[ranked[r].second & 3u], ranked[r].first);
    }
}

// A ring draw shown at its newest vertices while the rest of its object blends between frames looks like a jump under
// interpolation (vines, shadow volumes). Logged: a ring item with no pair whose object (slot and generation) has another
// item that paired, and a paired ring item whose vertex count changed (ring_item_before then uses its own vertices),
// with the first field that differs from the last list's item of the same identity.
void Pairing::log_lost_ring_pairs(const ListData& now, const NebulaFrameList& last) {
    const NebulaFrameList& list = *now.list;
    const auto object = [](const NebulaFrameItem& item) {
        return static_cast<std::uint64_t>(item.slot) << 32u | item.generation;
    };
    std::unordered_map<std::uint64_t, std::uint32_t> paired_objects;
    for (std::uint32_t i = 0; i < list.item_count; ++i) {
        if (now.item_before[i] != NEBULA_NONE32) ++paired_objects[object(list.items[i])];
    }
    std::unordered_map<std::uint64_t, std::uint32_t> last_by_part;  // (slot, generation, part) -> its first item
    for (std::uint32_t i = last.item_count; i != 0u; --i) {
        const NebulaFrameItem& item = last.items[i - 1u];
        last_by_part[object(item) * 0x9E3779B97F4A7C15ull ^ item.part] = i - 1u;
    }
    for (std::uint32_t i = 0; i < list.item_count; ++i) {
        const NebulaFrameItem& item = list.items[i];
        if ((item.flags & NEBULA_ITEM_RING) == 0u || (item.flags & NEBULA_ITEM_DISCRETE_VERTICES) != 0u) continue;
        const std::uint32_t pair = now.item_before[i];
        const char* why = nullptr;
        std::uint32_t was = 0u;
        if (pair != NEBULA_NONE32) {
            const NebulaFrameItem& other = last.items[pair];
            if (!ring_shape_matches(item, other)) {
                why = "vertex or index count changed";
                was = other.ring_count;
            }
        } else if (paired_objects.count(object(item)) != 0u) {
            const auto found = last_by_part.find(object(item) * 0x9E3779B97F4A7C15ull ^ item.part);
            if (found == last_by_part.end()) {
                why = "no part of this number last frame (parts shifted)";
            } else {
                const NebulaFrameItem& other = last.items[found->second];
                was = other.ring_count;
                why = other.pipeline != item.pipeline       ? "pipeline changed"
                      : other.mesh != item.mesh             ? "vertex layout changed"
                      : other.textures != item.textures     ? "textures changed"
                      : other.matrix != item.matrix         ? "matrix changed"
                      : other.flags != item.flags           ? "flags changed"
                      : other.source_hash != item.source_hash ? "material changed"
                      : now.block_before[item.block] != other.block ? "block did not pair"
                                                                    : "consumed by an earlier item of the same identity";
            }
        }
        if (why == nullptr) continue;
        const std::uint64_t who = item.source_hash != 0u ? item.source_hash : (1ull << 32u | item.slot);
        const std::uint64_t key = who * 0x9E3779B97F4A7C15ull ^ reinterpret_cast<std::uintptr_t>(why);
        if (!pair_logged_.insert(key).second) continue;
        std::fprintf(stderr, "[pair] ring item lost its history: %s (material %08x, slot %u, part %u, vertices %u, before %u)\n",
                     why, item.source_hash, item.slot, item.part, item.ring_count, was);
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
