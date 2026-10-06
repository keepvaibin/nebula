// SPDX-License-Identifier: GPL-3.0-only
// Exact RMGE01 opening-route scene predicate; plain reads, no guest callbacks.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace galaxy::input {

namespace opening_route_marker_detail {

inline constexpr std::uint32_t kGameSystemPointer = 0x806A1228u;
inline constexpr std::uint32_t kControllerNormalNerve = 0x806A24B4u;
inline constexpr std::uint32_t kGameSceneUpdate = 0x8033E750u;
inline constexpr std::uint32_t kPrologueHolderVtable = 0x805635C0u;
inline constexpr std::uint32_t kPrologueDirectorVtable = 0x805635E4u;
inline constexpr std::uint32_t kPrologueGameStartNerve = 0x8069F0F0u;

// Reader copies a mapped plain byte span on the simulation thread. It must not
// issue architectural guest loads, callbacks, timers, writes or worker reads.
template <typename Reader>
bool read_word(Reader& read, std::uint32_t base, std::uint32_t offset,
               std::uint32_t& word) {
    const std::uint64_t address = std::uint64_t{base} + offset;
    if (base < 0x80000000u || base >= 0x94000000u || (base & 3u) != 0u ||
        address < 0x80000000u || address + 4u > 0x94000000u) {
        return false;
    }
    std::array<std::uint8_t, 4> bytes{};
    if (!read(static_cast<std::uint32_t>(address),
              std::span<std::uint8_t>{bytes})) {
        return false;
    }
    word = (std::uint32_t{bytes[0]} << 24u) |
           (std::uint32_t{bytes[1]} << 16u) |
           (std::uint32_t{bytes[2]} << 8u) | std::uint32_t{bytes[3]};
    return true;
}

template <typename Reader>
bool read_byte(Reader& read, std::uint32_t base, std::uint32_t offset,
               std::uint8_t& byte) {
    const std::uint64_t address = std::uint64_t{base} + offset;
    if (base < 0x80000000u || base >= 0x94000000u || (base & 3u) != 0u ||
        address < 0x80000000u || address + 1u > 0x94000000u) {
        return false;
    }
    std::array<std::uint8_t, 1> bytes{};
    if (!read(static_cast<std::uint32_t>(address),
              std::span<std::uint8_t>{bytes})) {
        return false;
    }
    byte = bytes[0];
    return true;
}

template <typename Reader>
bool current_name_matches(Reader& read, std::uint32_t controller,
                          std::uint32_t offset, std::string_view expected) {
    const std::uint64_t address = std::uint64_t{controller} + offset;
    std::array<std::uint8_t, 32> bytes{};
    if (expected.size() >= bytes.size() || controller < 0x80000000u ||
        controller >= 0x94000000u || (controller & 3u) != 0u ||
        address < 0x80000000u ||
        address + bytes.size() > 0x94000000u ||
        !read(static_cast<std::uint32_t>(address),
              std::span<std::uint8_t>{bytes})) {
        return false;
    }
    for (std::size_t index = 0u; index != expected.size(); ++index) {
        if (bytes[index] != static_cast<std::uint8_t>(expected[index])) {
            return false;
        }
    }
    // Require the exact terminated current name, never a prefix or next-stage
    // request. Remaining padding does not participate in the guest string.
    return bytes[expected.size()] == 0u;
}

} // namespace opening_route_marker_detail

// This is an opening-route input-script anchor, not proof of human controls or
// unlocked Mario actions. FileSelect is itself a GameScene with a Mario actor.
// The controller's effective next/current nerve selects its active scene exactly
// as RMGE01 getCurrentSceneForExecute@8039DBE4; non-normal uses Intermission and
// cannot qualify. Current inline scene/stage names are at +00/+20. The requested
// next scene/stage at +4C/+6C must never satisfy this predicate.
template <typename Reader>
[[nodiscard]] bool opening_route_marker_scene_eligible(
    Reader&& reader, std::uint32_t* selected_scene = nullptr) {
    using namespace opening_route_marker_detail;
    auto& read = reader;
    std::uint32_t game_system = 0u;
    std::uint32_t controller = 0u;
    std::uint32_t controller_spine = 0u;
    std::uint32_t current_nerve = 0u;
    std::uint32_t next_nerve = 0u;
    std::uint32_t scene = 0u;
    std::uint32_t vtable = 0u;
    std::uint32_t update = 0u;
    if (!read_word(read, kGameSystemPointer, 0u, game_system) ||
        game_system == 0u ||
        !read_word(read, game_system, 0x24u, controller) || controller == 0u ||
        !current_name_matches(read, controller, 0u, "Game") ||
        !current_name_matches(read, controller, 0x20u,
                              "PeachCastleGardenGalaxy") ||
        !read_word(read, controller, 0x98u, controller_spine) ||
        controller_spine == 0u ||
        !read_word(read, controller_spine, 4u, current_nerve) ||
        !read_word(read, controller_spine, 8u, next_nerve) ||
        (next_nerve != 0u ? next_nerve : current_nerve) !=
            kControllerNormalNerve ||
        !read_word(read, controller, 0xACu, scene) || scene == 0u ||
        !read_word(read, scene, 0u, vtable) || vtable == 0u ||
        !read_word(read, vtable, 0x14u, update)) {
        return false;
    }
    if (update != kGameSceneUpdate) return false;
    if (selected_scene != nullptr) *selected_scene = scene;
    return true;
}

// Exact updateBindRatio call from updateBehavior@802B0BA8. The sole emitted
// retail caller lies on controlMain's true isEnableMoveMario branch. That
// predicate only whitelists Mario nerves; the post-prologue snapshot below is
// independently required, together with updateBehavior's saved caller return.
// Checkpoints/interior PCs are broad route evidence,
// not candidates for this functional one-shot input anchor.
[[nodiscard]] inline constexpr bool opening_route_marker_boundary_eligible(
    bool call_boundary, std::uint32_t guest_address, std::uint32_t context_pc,
    std::uint32_t link_register) {
    return call_boundary && guest_address == 0x802B0D0Cu &&
           context_pc == 0x802B0D0Cu && link_register == 0x802B0BACu;
}

// Opening-prologue completion only: this is deliberately not a catch-all
// player-control predicate for arbitrary saves or stages. The registered
// director starts dead in Wait, so dead alone cannot qualify. Its GameStart
// step 15 ends the arrival demo, initializes Mario, then kills the director.
// Spine::update increments the completed step to 16; dead actors no longer
// update it. Exact current GameStart / next-null / step16 / dead1 rejects the
// story, letter, arrival and unfinished terminal step without guest writes.
template <typename Reader>
[[nodiscard]] bool opening_route_marker_after_prologue_eligible(
    Reader&& reader, std::uint32_t mario_actor, std::uint32_t stack_pointer) {
    using namespace opening_route_marker_detail;
    auto& read = reader;
    if (mario_actor < 0x80000000u || mario_actor >= 0x94000000u ||
        (mario_actor & 3u) != 0u) {
        return false;
    }
    std::uint32_t scene = 0u;
    std::uint32_t control_main_return = 0u;
    std::uint32_t objects = 0u;
    std::uint32_t prologue_holder = 0u;
    std::uint32_t holder_vtable = 0u;
    std::uint32_t director = 0u;
    std::uint32_t director_vtable = 0u;
    std::uint8_t dead = 0u;
    std::uint32_t spine = 0u;
    std::uint32_t spine_owner = 0u;
    std::uint32_t current_nerve = 0u;
    std::uint32_t next_nerve = 0u;
    std::uint32_t step = 0u;
    std::uint32_t mario_holder = 0u;
    std::uint32_t registered_mario = 0u;
    // At updateBindRatio entry, the active updateBehavior frame saves its
    // incoming LR at r1+24. Exact return0ABC proves call0AB8 on controlMain's
    // true enable-predicate branch; r3 is already the actor, not that boolean.
    return read_word(read, stack_pointer, 0x24u, control_main_return) &&
           control_main_return == 0x802B0ABCu &&
           opening_route_marker_scene_eligible(read, &scene) &&
           read_word(read, scene, 0x10u, objects) && objects != 0u &&
           read_word(read, objects, 0x79u * 4u, prologue_holder) &&
           prologue_holder != 0u &&
           read_word(read, prologue_holder, 0u, holder_vtable) &&
           holder_vtable == kPrologueHolderVtable &&
           read_word(read, prologue_holder, 0x0Cu, director) &&
           director != 0u && read_word(read, director, 0u, director_vtable) &&
           director_vtable == kPrologueDirectorVtable &&
           read_byte(read, director, 0x68u, dead) && dead == 1u &&
           read_word(read, director, 0x50u, spine) && spine != 0u &&
           read_word(read, spine, 0u, spine_owner) && spine_owner == director &&
           read_word(read, spine, 4u, current_nerve) &&
           current_nerve == kPrologueGameStartNerve &&
           read_word(read, spine, 8u, next_nerve) && next_nerve == 0u &&
           read_word(read, spine, 0x0Cu, step) && step == 16u &&
           read_word(read, objects, 0x14u * 4u, mario_holder) &&
           mario_holder != 0u &&
           read_word(read, mario_holder, 0x0Cu, registered_mario) &&
           registered_mario == mario_actor;
}

} // namespace galaxy::input
