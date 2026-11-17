// Tests for the guard's need walk (native/include/luma_guard_walk.h: which mod a caught fault turns off) and its table
// of mods' code (luma_guard_spans.h: it grows past any count of libraries, M-001).
#include "luma_guard_spans.h"

#include <luma_guard_walk.h>

#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

// Mods by name; "" is code the kernel cannot name (a null mod). needs: direct (kind 6 imports).
struct World {
    std::map<std::string, std::set<std::string>> uses;
    [[nodiscard]] bool needs(const std::string& a, const std::string& b) const {
        const auto found = uses.find(a);
        return found != uses.end() && found->second.count(b) != 0;
    }
    [[nodiscard]] bool through(const std::string& a, const std::string& b, int depth = 0) const {
        if (depth > 64) return false;
        const auto found = uses.find(a);
        if (found == uses.end()) return false;
        for (const std::string& used : found->second) {
            if (used == b || through(used, b, depth + 1)) return true;
        }
        return false;
    }
};

// The walk over pointers, as the kernel runs it (a null pointer is a mod it cannot name).
std::string walk(const World& world, const std::vector<std::string>& chain, unsigned gap = 1000u) {
    // One pointer per mod, as the kernel has one LumaMod per mod.
    static std::set<std::string> names;
    std::vector<const std::string*> mods;
    for (const std::string& name : chain) mods.push_back(name.empty() ? nullptr : &*names.insert(name).first);
    const auto needs = [&](const std::string* a, const std::string* b) { return a != nullptr && b != nullptr && world.needs(*a, *b); };
    const auto through = [&](const std::string* a, const std::string* b) { return a != nullptr && b != nullptr && world.through(*a, *b); };
    const unsigned at = luma_guard_walk(mods.data(), static_cast<unsigned>(mods.size()), gap, needs, through);
    return mods[at] != nullptr ? *mods[at] : std::string("(none)");
}

bool walks() {
    bool ok = true;
    World world;
    world.uses["A"] = {"kit"};
    world.uses["B"] = {"kit"};
    world.uses["C"] = {"L1"};
    world.uses["L1"] = {"L2"};
    world.uses["D"] = {"kit", "B"};

    // The mod's own code faulted at its own boundary: itself.
    ok &= expect(walk(world, {"A"}) == "A", "a fault in A's own code turns A off");
    // A called the library it needs and the library's code faulted: A (the library stays).
    ok &= expect(walk(world, {"kit", "A"}) == "A", "A -> kit, kit faults: A off");
    // The library's own hook called B's callback (no boundary for it) and B's code faulted: B, not the library.
    ok &= expect(walk(world, {"B", "kit"}) == "B", "kit's hook -> B's callback, B faults: B off");
    // A -> L1 -> L2 by needs: the walk climbs to A.
    ok &= expect(walk(world, {"L2", "L1", "C"}) == "C", "C -> L1 -> L2, L2 faults: C off");
    // A -> kit -> B's callback, B faults: kit does not need B, so B.
    ok &= expect(walk(world, {"B", "kit", "A"}) == "B", "A -> kit -> B's callback, B faults: B off");
    // D needs B: D -> B -> kit -> fault in kit: B needs kit, D needs B: D.
    ok &= expect(walk(world, {"kit", "B", "D"}) == "D", "D -> B -> kit, kit faults: D off");
    // A frame of code no mod's (the kernel cannot name it): the walk stops before it.
    ok &= expect(walk(world, {"kit", "", "A"}) == "kit", "kit's code under code no mod's: kit");
    ok &= expect(walk(world, {"", "A"}) == "(none)", "a fault in code no mod's: none named (the kernel charges the boundary)");
    // The same mod twice in a row (its code called itself through code the chain does not name): one step.
    ok &= expect(walk(world, {"kit", "kit", "A"}) == "A", "kit twice, then A: A");
    // B called back into the library it needs (kit -> B -> kit): the fault in kit, B needs kit: B; kit does not need B.
    ok &= expect(walk(world, {"kit", "B", "kit"}) == "B", "kit -> B -> kit, kit faults: B off");
    // A gap (frames not followed) between L2 and C: C needs L2 through L1, so C.
    ok &= expect(walk(world, {"L2", "C"}, 0) == "C", "L2 .. C across a gap: C needs L2 through L1");
    ok &= expect(walk(world, {"L2", "C"}) == "L2", "L2 then C with no gap: C does not need L2 directly");
    // A gap later in the chain: the steps before it are direct.
    ok &= expect(walk(world, {"B", "kit", "A"}, 1) == "B", "a gap after kit does not change B's step");
    return ok;
}

bool spans() {
    bool ok = true;
    galaxy::luma_guard::Spans table;
    // More libraries than any fixed table held (512 before M-001): every one is found, none falls back.
    constexpr std::uintptr_t kBase = 0x100000000ull;
    constexpr std::uintptr_t kSize = 0x10000u;
    constexpr unsigned kCount = 2000u;
    for (unsigned i = 0; i < kCount; ++i) {
        ok &= expect(table.add(kBase + i * kSize * 2u, kBase + i * kSize * 2u + kSize), "add library " + std::to_string(i));
    }
    unsigned found = 0;
    for (unsigned i = 0; i < kCount; ++i) {
        const std::uintptr_t begin = kBase + i * kSize * 2u;
        if (table.span_of(begin + 0x10u) == begin && !table.has(begin + kSize + 0x10u)) ++found;
    }
    ok &= expect(found == kCount, "every one of " + std::to_string(kCount) + " libraries is found (" + std::to_string(found) + ")");
    ok &= expect(table.grown_chunks() == (kCount + galaxy::luma_guard::Spans::kChunkSlots - 1) / galaxy::luma_guard::Spans::kChunkSlots - 1,
                 "the table grew by whole chunks");
    // The same library twice is one span; a removed one is gone and its slot is used again.
    ok &= expect(table.add(kBase, kBase + kSize), "adding a library again");
    table.remove(kBase + 513u * kSize * 2u);
    ok &= expect(!table.has(kBase + 513u * kSize * 2u + 1u), "a removed library is gone");
    const std::size_t chunks = table.grown_chunks();
    ok &= expect(table.add(0x900000000ull, 0x900010000ull) && table.has(0x900000010ull), "a new library after a removal");
    ok &= expect(table.grown_chunks() == chunks, "it takes the freed slot, not a new chunk");
    ok &= expect(!table.add(0, 16) && !table.add(32, 16), "an empty or reversed span is refused");
    return ok;
}

}  // namespace

int main() {
    bool ok = walks();
    ok &= spans();
    if (!ok) return 1;
    std::cout << "luma_guard_walk_tests: the need walk and the growing table hold\n";
    return 0;
}
