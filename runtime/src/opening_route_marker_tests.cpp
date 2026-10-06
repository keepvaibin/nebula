// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/opening_route_marker_guard.h"

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>

namespace {

constexpr std::uint32_t kSystem = 0x80F00000u;
constexpr std::uint32_t kController = 0x80F00100u;
constexpr std::uint32_t kControllerSpine = 0x80F00200u;
constexpr std::uint32_t kScene = 0x80F00300u;
constexpr std::uint32_t kVtable = 0x805BD054u;

struct PlainMemory {
    std::unordered_map<std::uint32_t, std::uint8_t> bytes;
    std::optional<std::uint32_t> blocked;
    std::size_t reads{};

    void word(std::uint32_t address, std::uint32_t value) {
        for (std::uint32_t index = 0u; index != 4u; ++index) {
            bytes[address + index] = static_cast<std::uint8_t>(
                value >> (24u - index * 8u));
        }
    }

    void name(std::uint32_t address, std::string_view value) {
        for (std::uint32_t index = 0u; index != 32u; ++index) {
            bytes[address + index] = index < value.size()
                ? static_cast<std::uint8_t>(value[index]) : 0u;
        }
    }

    bool operator()(std::uint32_t address, std::span<std::uint8_t> destination) {
        ++reads;
        for (std::size_t index = 0u; index != destination.size(); ++index) {
            const auto current = address + static_cast<std::uint32_t>(index);
            const auto found = bytes.find(current);
            if (blocked == current || found == bytes.end()) return false;
            destination[index] = found->second;
        }
        return true;
    }
};

PlainMemory eligible_memory() {
    using namespace galaxy::input::opening_route_marker_detail;
    PlainMemory memory;
    memory.word(kGameSystemPointer, kSystem);
    memory.word(kSystem + 0x24u, kController);
    memory.name(kController, "Game");
    memory.name(kController + 0x20u, "PeachCastleGardenGalaxy");
    memory.word(kController + 0x98u, kControllerSpine);
    memory.word(kControllerSpine + 4u, kControllerNormalNerve);
    memory.word(kControllerSpine + 8u, 0u);
    memory.word(kController + 0xACu, kScene);
    memory.word(kScene, kVtable);
    memory.word(kVtable + 0x14u, kGameSceneUpdate);
    return memory;
}

bool expect(bool condition, std::string_view message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

bool exact_current_scene_guard() {
    using galaxy::input::opening_route_marker_scene_eligible;
    auto memory = eligible_memory();
    const auto before = memory.bytes;
    bool passed = expect(opening_route_marker_scene_eligible(memory),
        "active GameScene / Game / PeachCastleGardenGalaxy qualifies");
    passed &= expect(memory.bytes == before && memory.reads == 10u,
        "guard is a bounded ten plain-read predicate with no memory mutation");

    memory = eligible_memory();
    memory.name(kController + 0x20u, "FileSelect");
    memory.name(kController + 0x4Cu, "Game");
    memory.name(kController + 0x6Cu, "PeachCastleGardenGalaxy");
    passed &= expect(!opening_route_marker_scene_eligible(memory),
        "FileSelect rejects even with GameScene and a requested Peach stage");
    memory = eligible_memory();
    memory.name(kController, "Title");
    passed &= expect(!opening_route_marker_scene_eligible(memory),
        "current Title scene cannot qualify through a Peach stage string");
    memory = eligible_memory();
    memory.name(kController + 0x20u, "HeavensDoorGalaxy");
    passed &= expect(!opening_route_marker_scene_eligible(memory),
        "later gameplay stage is outside this bounded opening-route anchor");
    memory = eligible_memory();
    memory.name(kController + 0x20u, "PeachCastleGardenGalaxyExtra");
    passed &= expect(!opening_route_marker_scene_eligible(memory),
        "stage-name prefix without its exact terminator rejects");
    memory = eligible_memory();
    memory.name(kController, "GameExtra");
    passed &= expect(!opening_route_marker_scene_eligible(memory),
        "scene-name prefix without its exact terminator rejects");
    memory = eligible_memory();
    for (std::uint32_t index = 0u; index != 32u; ++index) {
        memory.bytes[kController + 0x20u + index] = 'X';
    }
    passed &= expect(!opening_route_marker_scene_eligible(memory),
        "unterminated current stage rejects");
    return passed;
}

bool active_scene_selection_guard() {
    using namespace galaxy::input::opening_route_marker_detail;
    using galaxy::input::opening_route_marker_scene_eligible;
    auto memory = eligible_memory();
    memory.word(kControllerSpine + 8u, 0x806A24B8u);
    bool passed = expect(!opening_route_marker_scene_eligible(memory),
        "pending non-normal nerve selects Intermission and rejects stale Game");
    memory = eligible_memory();
    memory.word(kControllerSpine + 4u, 0x806A24B8u);
    memory.word(kControllerSpine + 8u, kControllerNormalNerve);
    passed &= expect(opening_route_marker_scene_eligible(memory),
        "effective pending normal nerve follows exact retail scene selection");
    memory = eligible_memory();
    memory.word(kVtable + 0x14u, 0x8039E000u);
    passed &= expect(!opening_route_marker_scene_eligible(memory),
        "non-GameScene update vtable rejects despite correct current names");
    memory = eligible_memory();
    memory.word(kVtable + 0x14u, 0x8033E7E0u);
    passed &= expect(!opening_route_marker_scene_eligible(memory),
        "draw slot/address cannot substitute for exact update identity");
    for (const std::uint32_t address : {kGameSystemPointer,
            kSystem + 0x24u, kController + 0x98u,
            kController + 0xACu, kScene}) {
        memory = eligible_memory();
        memory.word(address, 0u);
        passed &= expect(!opening_route_marker_scene_eligible(memory),
            "null chain owner rejects without accepting another scene");
    }
    memory = eligible_memory();
    memory.word(kSystem + 0x24u, 0xFFFFFFF0u);
    passed &= expect(!opening_route_marker_scene_eligible(memory),
        "malformed high owner cannot wrap into a readable low address");
    memory = eligible_memory();
    memory.word(kSystem + 0x24u, kController + 1u);
    passed &= expect(!opening_route_marker_scene_eligible(memory),
        "unaligned controller owner rejects before its fields are read");
    memory = eligible_memory();
    memory.word(kGameSystemPointer, 0x7FFFFFE0u);
    passed &= expect(!opening_route_marker_scene_eligible(memory),
        "owner below guest RAM cannot enter through a valid offset address");
    return passed;
}

bool missing_reads_reject_without_mutation() {
    using namespace galaxy::input::opening_route_marker_detail;
    using galaxy::input::opening_route_marker_scene_eligible;
    bool passed = true;
    for (const std::uint32_t missing : {kGameSystemPointer,
            kSystem + 0x24u, kController, kController + 0x20u,
            kController + 0x98u, kControllerSpine + 4u,
            kControllerSpine + 8u, kController + 0xACu,
            kScene, kVtable + 0x14u, kController + 31u,
            kController + 0x20u + 31u}) {
        auto memory = eligible_memory();
        memory.blocked = missing;
        const auto before = memory.bytes;
        passed &= expect(!opening_route_marker_scene_eligible(memory),
            "every required failed plain read rejects the candidate");
        passed &= expect(memory.bytes == before,
            "a rejected snapshot cannot mutate guest memory");
    }
    // Model the runtime's one-shot eligibility gate only. Publication remains
    // the unchanged GuestAddressSpace release/acquire API, tested separately.
    auto memory = eligible_memory();
    memory.name(kController + 0x20u, "FileSelect");
    bool seen = false;
    const auto first = [&]() {
        if (!seen && opening_route_marker_scene_eligible(memory)) {
            seen = true;
            return true;
        }
        return false;
    };
    passed &= expect(!first() && !seen,
        "FileSelect rejection leaves the first-marker gate armed");
    memory.name(kController + 0x20u, "PeachCastleGardenGalaxy");
    passed &= expect(first() && seen,
        "later qualifying active scene can publish its first marker");
    const auto reads = memory.reads;
    passed &= expect(!first() && memory.reads == reads,
        "seen gate prevents repeated snapshots or re-anchoring");
    return passed;
}

constexpr std::uint32_t kObjects = 0x80F00400u;
constexpr std::uint32_t kPrologueHolder = 0x80F00800u;
constexpr std::uint32_t kDirector = 0x80F00900u;
constexpr std::uint32_t kDirectorSpine = 0x80F00A00u;
constexpr std::uint32_t kMarioHolder = 0x80F00B00u;
constexpr std::uint32_t kMarioActor = 0x80F00C00u;
constexpr std::uint32_t kUpdateBehaviorStack = 0x817FFF00u;

PlainMemory completed_prologue_memory() {
    using namespace galaxy::input::opening_route_marker_detail;
    auto memory = eligible_memory();
    memory.word(kUpdateBehaviorStack + 0x24u, 0x802B0ABCu);
    memory.word(kScene + 0x10u, kObjects);
    memory.word(kObjects + 0x79u * 4u, kPrologueHolder);
    memory.word(kPrologueHolder, kPrologueHolderVtable);
    memory.word(kPrologueHolder + 0x0Cu, kDirector);
    memory.word(kDirector, kPrologueDirectorVtable);
    memory.bytes[kDirector + 0x68u] = 1u;
    memory.word(kDirector + 0x50u, kDirectorSpine);
    memory.word(kDirectorSpine, kDirector);
    memory.word(kDirectorSpine + 4u, kPrologueGameStartNerve);
    memory.word(kDirectorSpine + 8u, 0u);
    memory.word(kDirectorSpine + 0x0Cu, 16u);
    memory.word(kObjects + 0x14u * 4u, kMarioHolder);
    memory.word(kMarioHolder + 0x0Cu, kMarioActor);
    return memory;
}

bool exact_enabled_caller_guard() {
    using galaxy::input::opening_route_marker_boundary_eligible;
    bool passed = expect(opening_route_marker_boundary_eligible(
        true, 0x802B0D0Cu, 0x802B0D0Cu, 0x802B0BACu),
        "sole enabled updateBehavior call to updateBindRatio qualifies");
    passed &= expect(!opening_route_marker_boundary_eligible(
        false, 0x802B0D0Cu, 0x802B0D0Cu, 0x802B0BACu),
        "checkpoint with matching numeric PCs cannot pose as call boundary");
    passed &= expect(!opening_route_marker_boundary_eligible(
        true, 0x802B0D0Cu, 0x802B0D0Cu, 0x802B0BACu + 4u),
        "different call return identity rejects");
    passed &= expect(!opening_route_marker_boundary_eligible(
        true, 0x802B0D0Cu, 0x802B0D38u, 0x802B0BACu),
        "interior architectural pc cannot pose as entry");
    for (const std::uint32_t pc : {0x802B0D38u, 0x802B0E18u, 0x802B0E60u,
            0x802B0FE4u, 0x802B1004u, 0x802B0A88u, 0x802B0A8Cu}) {
        passed &= expect(!opening_route_marker_boundary_eligible(
            true, pc, pc, 0x802B0BACu),
            "broad route keys and unforced enable-return PCs do not anchor");
    }
    return passed;
}

bool completed_opening_prologue_guard() {
    using namespace galaxy::input::opening_route_marker_detail;
    using galaxy::input::opening_route_marker_after_prologue_eligible;
    auto memory = completed_prologue_memory();
    const auto before = memory.bytes;
    bool passed = expect(opening_route_marker_after_prologue_eligible(
        memory, kMarioActor, kUpdateBehaviorStack),
        "completed exact registered prologue with true-branch caller qualifies");
    passed &= expect(memory.bytes == before && memory.reads == 24u,
        "completed prologue guard uses 24 bounded plain reads without writes");
    // All eight earlier retail states reject even if their director is dead:
    // initially dead Wait must not be confused with a completed prologue.
    for (std::uint32_t nerve = 0x8069F0D0u;
         nerve != kPrologueGameStartNerve; nerve += 4u) {
        memory = completed_prologue_memory();
        memory.word(kDirectorSpine + 4u, nerve);
        passed &= expect(!opening_route_marker_after_prologue_eligible(
            memory, kMarioActor, kUpdateBehaviorStack),
            "story, letter, arrival and initially dead Wait do not qualify");
    }
    for (const std::uint32_t step : {0u, 14u, 15u, 17u, 0xFFFFFFFFu}) {
        memory = completed_prologue_memory();
        memory.word(kDirectorSpine + 0x0Cu, step);
        passed &= expect(!opening_route_marker_after_prologue_eligible(
            memory, kMarioActor, kUpdateBehaviorStack),
            "only completed frozen GameStart step16 qualifies");
    }
    for (const std::uint8_t dead : {std::uint8_t{0u}, std::uint8_t{2u}}) {
        memory = completed_prologue_memory();
        memory.bytes[kDirector + 0x68u] = dead;
        passed &= expect(!opening_route_marker_after_prologue_eligible(
            memory, kMarioActor, kUpdateBehaviorStack),
            "alive or malformed dead flag rejects");
    }
    for (const std::uint32_t bad_actor : {0u, kMarioActor + 4u,
            kMarioActor + 1u, 0x7FFFFF00u, 0x94000000u}) {
        memory = completed_prologue_memory();
        passed &= expect(!opening_route_marker_after_prologue_eligible(
            memory, bad_actor, kUpdateBehaviorStack),
            "only this active scene's registered Mario qualifies");
    }
    for (const std::uint32_t field : {kPrologueHolder, kDirector,
            kDirectorSpine, kMarioHolder + 0x0Cu}) {
        memory = completed_prologue_memory();
        memory.word(field, 0u);
        passed &= expect(!opening_route_marker_after_prologue_eligible(
            memory, kMarioActor, kUpdateBehaviorStack),
            "incorrect identity or spine owner rejects");
    }
    memory = completed_prologue_memory();
    memory.word(kDirectorSpine + 8u, kPrologueGameStartNerve);
    passed &= expect(!opening_route_marker_after_prologue_eligible(
        memory, kMarioActor, kUpdateBehaviorStack),
        "pending terminal nerve does not prove execution");
    memory = completed_prologue_memory();
    memory.name(kController + 0x20u, "FileSelect");
    passed &= expect(!opening_route_marker_after_prologue_eligible(
        memory, kMarioActor, kUpdateBehaviorStack),
        "completed-looking stale prologue cannot bypass scene guard");
    memory = completed_prologue_memory();
    memory.word(kUpdateBehaviorStack + 0x24u, 0x802B0ABCu + 4u);
    passed &= expect(!opening_route_marker_after_prologue_eligible(
        memory, kMarioActor, kUpdateBehaviorStack),
        "wrong saved caller return cannot prove controlMain's enabled branch");
    for (const std::uint32_t stack : {0u, kUpdateBehaviorStack + 1u,
            0xFFFFFFF0u, 0x7FFFFF00u, 0x93FFFFE0u}) {
        memory = completed_prologue_memory();
        passed &= expect(!opening_route_marker_after_prologue_eligible(
            memory, kMarioActor, stack),
            "null, unaligned, wrapped or truncated caller frame rejects");
    }
    return passed;
}

bool required_prologue_reads_and_one_shot() {
    using namespace galaxy::input::opening_route_marker_detail;
    using galaxy::input::opening_route_marker_after_prologue_eligible;
    bool passed = true;
    for (const std::uint32_t field : {kUpdateBehaviorStack + 0x24u,
            kScene + 0x10u,
            kObjects + 0x79u * 4u, kPrologueHolder,
            kPrologueHolder + 0x0Cu, kDirector, kDirector + 0x68u,
            kDirector + 0x50u, kDirectorSpine, kDirectorSpine + 4u,
            kDirectorSpine + 8u, kDirectorSpine + 0x0Cu,
            kObjects + 0x14u * 4u, kMarioHolder + 0x0Cu}) {
        auto memory = completed_prologue_memory();
        memory.blocked = field;
        const auto before = memory.bytes;
        passed &= expect(!opening_route_marker_after_prologue_eligible(
            memory, kMarioActor, kUpdateBehaviorStack),
            "each required caller/prologue/owner read must succeed");
        passed &= expect(memory.bytes == before,
            "rejected prologue snapshot cannot mutate guest state");
    }
    for (const std::uint32_t pointer_field : {kScene + 0x10u,
            kObjects + 0x79u * 4u, kPrologueHolder + 0x0Cu,
            kDirector + 0x50u, kObjects + 0x14u * 4u}) {
        for (const std::uint32_t invalid : {0u, 0x7FFFFF00u,
                0xFFFFFFF0u, 0x80F00001u}) {
            auto memory = completed_prologue_memory();
            memory.word(pointer_field, invalid);
            passed &= expect(!opening_route_marker_after_prologue_eligible(
                memory, kMarioActor, kUpdateBehaviorStack),
                "malformed new chain pointer fails closed");
        }
    }
    auto memory = completed_prologue_memory();
    bool seen = false;
    const auto first = [&]() {
        if (!seen && opening_route_marker_after_prologue_eligible(
                memory, kMarioActor, kUpdateBehaviorStack)) {
            seen = true;
            return true;
        }
        return false;
    };
    for (const std::uint32_t nerve : {0x8069F0D4u, 0x8069F0DCu,
            0x8069F0ECu, kPrologueGameStartNerve}) {
        memory.word(kDirectorSpine + 4u, nerve);
        memory.bytes[kDirector + 0x68u] = 0u;
        memory.word(kDirectorSpine + 0x0Cu, 15u);
        passed &= expect(!first() && !seen,
            "enabled Mario maintenance during opening leaves startup gate armed");
    }
    memory.bytes[kDirector + 0x68u] = 1u;
    passed &= expect(!first() && !seen,
        "kill has started but current spine execution is not yet complete");
    memory.word(kDirectorSpine + 0x0Cu, 16u);
    passed &= expect(first() && seen,
        "completed terminal update publishes the one-shot marker");
    const auto reads = memory.reads;
    passed &= expect(!first() && memory.reads == reads,
        "published marker never resnapshots or renews its anchor");
    return passed;
}

} // namespace

int main() {
    bool passed = exact_current_scene_guard();
    passed &= active_scene_selection_guard();
    passed &= missing_reads_reject_without_mutation();
    passed &= exact_enabled_caller_guard();
    passed &= completed_opening_prologue_guard();
    passed &= required_prologue_reads_and_one_shot();
    return passed ? 0 : 1;
}
