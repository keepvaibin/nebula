// Tests for the guard's need walk (native/include/luma_guard_walk.h: which mod a caught fault turns off), its table
// of mods' code (luma_guard_spans.h: it grows past any count of libraries, M-001) and its reading of a mod library's
// unwind tables (luma_guard_unwind.h: hostile bytes give 0, never a read outside them), and the kernel's set of the mods
// a mod needs (LumaNeeds.hpp: the need walk's "needs through", one binary search whatever the graph's shape).
#include "Game/System/LumaNeeds.hpp"
#include "luma_guard_spans.h"
#include "luma_guard_unwind.h"

#include <luma_guard_walk.h>

#include <cstdint>
#include <cstring>
#include <memory>
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

// Bytes in a block of exactly their size (an address sanitizer sees any read past it).
std::unique_ptr<std::uint8_t[]> exact(const std::vector<std::uint8_t>& bytes) {
    auto block = std::make_unique<std::uint8_t[]>(bytes.size() == 0 ? 1 : bytes.size());
    if (!bytes.empty()) std::memcpy(block.get(), bytes.data(), bytes.size());
    return block;
}

void put32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t v) {
    if (bytes.size() < at + 4) bytes.resize(at + 4);
    std::memcpy(bytes.data() + at, &v, 4);
}

std::uintptr_t eh(const std::vector<std::uint8_t>& bytes, std::uintptr_t address, std::uintptr_t pc) {
    const auto block = exact(bytes);
    return galaxy::luma_guard::eh_frame_hdr_function(block.get(), bytes.size(), address, pc);
}

std::uint32_t pe_begin(const std::vector<std::uint8_t>& image, std::size_t entry) {
    const auto block = exact(image);
    return galaxy::luma_guard::pe_function_begin(block.get(), image.size(), entry);
}

int pe_register(const std::vector<std::uint8_t>& image, std::size_t entry) {
    const auto block = exact(image);
    return galaxy::luma_guard::pe_frame_register(block.get(), image.size(), entry);
}

struct Random {
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    std::uint32_t next() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<std::uint32_t>(state >> 16);
    }
};

bool unwind() {
    bool ok = true;
    constexpr std::uintptr_t kAt = 0x40000000u;
    // A header as lld writes it: version 1, eh_frame_ptr pcrel sdata4, fde_count udata4, the table datarel sdata4;
    // three functions at +0x100, +0x200, +0x300 from the header.
    std::vector<std::uint8_t> hdr = {1, 0x1B, 0x03, 0x3B};
    put32(hdr, 4, 0x40);
    put32(hdr, 8, 3);
    for (std::uint32_t i = 0; i < 3; ++i) {
        put32(hdr, 12 + i * 8, 0x100 * (i + 1));
        put32(hdr, 16 + i * 8, 0x1000 + i);
    }
    ok &= expect(eh(hdr, kAt, kAt + 0x250) == kAt + 0x200, "eh_frame_hdr: the start at or below pc");
    ok &= expect(eh(hdr, kAt, kAt + 0x100) == kAt + 0x100, "eh_frame_hdr: pc at a start");
    ok &= expect(eh(hdr, kAt, kAt + 0x3FF0) == kAt + 0x300, "eh_frame_hdr: past the last start");
    ok &= expect(eh(hdr, kAt, kAt + 0xFF) == 0, "eh_frame_hdr: before the first start");
    // Every shorter header (truncated anywhere) is read within its bytes; all but the whole one give 0.
    for (std::size_t n = 0; n < hdr.size(); ++n) {
        const std::vector<std::uint8_t> cut(hdr.begin(), hdr.begin() + static_cast<std::ptrdiff_t>(n));
        ok &= expect(eh(cut, kAt, kAt + 0x250) == 0, "eh_frame_hdr cut to " + std::to_string(n) + " bytes gives 0");
    }
    // A count beyond its bytes, a count that is not absolute, an unknown encoding, another version: 0.
    auto hostile = hdr;
    put32(hostile, 8, 0xFFFFFFFFu);
    ok &= expect(eh(hostile, kAt, kAt + 0x250) == 0, "eh_frame_hdr: a count past its bytes gives 0");
    put32(hostile, 8, 4);
    ok &= expect(eh(hostile, kAt, kAt + 0x250) == 0, "eh_frame_hdr: one entry too many gives 0");
    hostile = hdr;
    hostile[2] = 0x13;
    ok &= expect(eh(hostile, kAt, kAt + 0x250) == 0, "eh_frame_hdr: a pcrel count gives 0");
    hostile = hdr;
    hostile[1] = 0x0C;
    ok &= expect(eh(hostile, kAt, kAt + 0x250) == 0, "eh_frame_hdr: an 8-byte eh_frame_ptr (not read) gives 0");
    hostile = hdr;
    hostile[0] = 2;
    ok &= expect(eh(hostile, kAt, kAt + 0x250) == 0, "eh_frame_hdr: version 2 gives 0");
    hostile = hdr;
    hostile[3] = 0x1B;
    ok &= expect(eh(hostile, kAt, kAt + 0x250) == 0, "eh_frame_hdr: a pcrel table gives 0");
    ok &= expect(galaxy::luma_guard::eh_frame_hdr_function(nullptr, 64, kAt, kAt) == 0, "eh_frame_hdr: none");

    // Windows x64: RUNTIME_FUNCTION at 0x40 (0x80..0x90, unwind 0x60); UNWIND_INFO version 1 with frame register 5.
    std::vector<std::uint8_t> image(256, 0);
    put32(image, 0x40, 0x80);
    put32(image, 0x44, 0x90);
    put32(image, 0x48, 0x60);
    image[0x60] = 0x01;
    image[0x63] = 0x05;
    ok &= expect(pe_begin(image, 0x40) == 0x80, "pdata: the function's begin");
    ok &= expect(pe_register(image, 0x40) == 5, "pdata: the frame register");
    // A later part chained to the first: 0xA0 (0xA0..0xB0, unwind 0xC0, chained, 2 codes) -> 0xC8 (begin 0x80).
    put32(image, 0xA0, 0xA0);
    put32(image, 0xA4, 0xB0);
    put32(image, 0xA8, 0xC0);
    image[0xC0] = 0x21;  // version 1, UNW_FLAG_CHAININFO
    image[0xC2] = 2;     // two unwind codes, 4 bytes
    put32(image, 0xC8, 0x80);
    put32(image, 0xCC, 0x90);
    put32(image, 0xD0, 0x60);
    ok &= expect(pe_begin(image, 0xA0) == 0x80, "pdata: a chained part gives its function's begin");
    ok &= expect(pe_register(image, 0xA0) == 5, "pdata: a chained part's frame register is its function's");
    // Hostile: unwind data past the image, an entry past it, a chain that loops.
    auto bad = image;
    put32(bad, 0x48, 0x1000);
    ok &= expect(pe_begin(bad, 0x40) == 0 && pe_register(bad, 0x40) == -1, "pdata: unwind data past the image");
    ok &= expect(pe_begin(image, 250) == 0 && pe_register(image, 250) == -1, "pdata: an entry cut by the image's end");
    ok &= expect(pe_begin(image, ~std::size_t{0} - 4) == 0, "pdata: an entry far past the image");
    bad = image;
    put32(bad, 0xD0, 0xC0);  // the chained entry's unwind is the chaining one: a loop
    put32(bad, 0xC8, 0x80);
    ok &= expect(pe_begin(bad, 0xA0) == 0 && pe_register(bad, 0xA0) == -1, "pdata: a chain that loops stops");
    image.resize(0xCA);  // the chained RUNTIME_FUNCTION cut by the image's end
    ok &= expect(pe_begin(image, 0xA0) == 0, "pdata: a chained entry cut by the image's end");

    // Random bytes, from a fixed seed: whatever they say, each reader stays within them.
    Random random;
    unsigned answered = 0;
    for (unsigned round = 0; round < 20000; ++round) {
        std::vector<std::uint8_t> bytes(random.next() % 96);
        for (auto& b : bytes) b = static_cast<std::uint8_t>(random.next());
        if (bytes.size() >= 4 && (round & 1u) != 0) {
            bytes[0] = 1;
            bytes[3] = 0x3B;
            bytes[2] = (round & 2u) != 0 ? 0x03 : 0x0B;
            if (bytes.size() >= 12) put32(bytes, 8, random.next() % 12);
        }
        if (eh(bytes, kAt, kAt + random.next() % 0x400) != 0) ++answered;
        const std::size_t entry = bytes.empty() ? 0 : random.next() % (bytes.size() + 8);
        if (pe_begin(bytes, entry) != 0) ++answered;
        (void)pe_register(bytes, entry);
    }
    ok &= expect(answered != 0, "the random rounds reach the tables (some give a function)");
    return ok;
}

// A mod as the kernel keeps its needs: the mods it imports from, and the set made at its bind.
struct NeedMod {
    std::vector<NeedMod*> uses;
    std::vector<NeedMod*> set;
};

void bind(NeedMod& mod) {
    std::size_t room = mod.uses.size();
    for (NeedMod* use : mod.uses) room += use->set.size();
    mod.set.assign(room, nullptr);
    const auto count = luma::needs::close(
        mod.uses.data(), static_cast<std::uint32_t>(mod.uses.size()),
        [](NeedMod* use, NeedMod* const** set, std::uint32_t* n) {
            *set = use->set.data();
            *n = static_cast<std::uint32_t>(use->set.size());
        },
        mod.set.data(), static_cast<std::uint32_t>(room));
    mod.set.resize(count);
}

bool has(const NeedMod& user, const NeedMod* used) {
    return luma::needs::has(user.set.data(), static_cast<std::uint32_t>(user.set.size()), used);
}

bool needs() {
    bool ok = true;
    // Forty layers of two mods, each needing both of the layer below: 2^39 paths from the top to the bottom, and a
    // set of 2k mods at layer k.
    constexpr unsigned kLayers = 40;
    std::vector<NeedMod> mods(kLayers * 2);
    for (unsigned layer = 0; layer < kLayers; ++layer) {
        for (unsigned side = 0; side < 2; ++side) {
            NeedMod& mod = mods[layer * 2 + side];
            if (layer != 0) mod.uses = {&mods[(layer - 1) * 2], &mods[(layer - 1) * 2 + 1]};
            bind(mod);
            ok &= expect(mod.set.size() == layer * 2u, "layer " + std::to_string(layer) + "'s set has its " +
                                                          std::to_string(layer * 2u) + " mods (" + std::to_string(mod.set.size()) + ")");
        }
    }
    const NeedMod& top = mods[kLayers * 2 - 1];
    unsigned found = 0;
    for (unsigned i = 0; i < (kLayers - 1) * 2; ++i) found += has(top, &mods[i]) ? 1u : 0u;
    ok &= expect(found == (kLayers - 1) * 2, "the top needs every mod below it");
    ok &= expect(!has(top, &mods[kLayers * 2 - 2]) && !has(top, &top), "the top needs not its own layer nor itself");
    ok &= expect(!has(mods[0], &mods[1]) && !has(top, nullptr), "the bottom needs nothing; null is never needed");
    // A use taken out of a set (a needed mod went before a waiting one started).
    NeedMod spare = mods[6];
    spare.set.resize(luma::needs::drop(spare.set.data(), static_cast<std::uint32_t>(spare.set.size()), &mods[0]));
    ok &= expect(!has(spare, &mods[0]) && has(spare, &mods[1]) && spare.set.size() == 5u, "drop takes one mod out");

    // Random graphs, each mod needing up to four earlier ones (a mod loads after what it needs): the set answers as a
    // search of the graph does, for every pair.
    std::uint64_t state = 0x2545F4914F6CDD1Dull;
    const auto next = [&state] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<unsigned>(state >> 20);
    };
    unsigned wrong = 0;
    for (unsigned round = 0; round < 8; ++round) {
        constexpr unsigned kMods = 200;
        std::vector<NeedMod> graph(kMods);
        for (unsigned i = 0; i < kMods; ++i) {
            const unsigned n = i == 0 ? 0 : next() % 5;
            for (unsigned k = 0; k < n; ++k) {
                NeedMod* const use = &graph[next() % i];
                bool known = false;
                for (NeedMod* u : graph[i].uses) known = known || u == use;
                if (!known) graph[i].uses.push_back(use);
            }
            bind(graph[i]);
        }
        for (unsigned a = 0; a < kMods; ++a) {
            std::vector<bool> seen(kMods, false);
            std::vector<unsigned> todo;
            for (NeedMod* u : graph[a].uses) todo.push_back(static_cast<unsigned>(u - graph.data()));
            while (!todo.empty()) {
                const unsigned at = todo.back();
                todo.pop_back();
                if (seen[at]) continue;
                seen[at] = true;
                for (NeedMod* u : graph[at].uses) todo.push_back(static_cast<unsigned>(u - graph.data()));
            }
            for (unsigned b = 0; b < kMods; ++b) wrong += has(graph[a], &graph[b]) != seen[b] ? 1u : 0u;
        }
    }
    ok &= expect(wrong == 0, "the sets answer as a search of the graph (" + std::to_string(wrong) + " wrong)");
    return ok;
}

}  // namespace

int main() {
    bool ok = walks();
    ok &= spans();
    ok &= unwind();
    ok &= needs();
    if (!ok) return 1;
    std::cout << "luma_guard_walk_tests: the need walk and the growing table hold\n";
    return 0;
}
