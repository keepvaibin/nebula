// Tests for the kernel's weak references (native/petari/src/Game/System/LumaWeak.cpp, LumaCore.weak and weak_get,
// N-4003), compiled here against a kernel the test provides: objects are cells of the test's own arrays, NameObj the
// class every cell of `gObjects` is. A model of which object each handle names is checked against the table through
// random makes, destructors, frees and arena ends (the hash's removal moves cells back: a wrong move loses an object
// or keeps a gone one), then the same from several threads at once.
#include "Game/System/LumaWeak.hpp"

#include "Game/System/LumaLoader.hpp"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

constexpr int32_t kNameObj = 7;
constexpr size_t kObjects = 6000;
constexpr size_t kObjectBytes = 64;

alignas(16) unsigned char gObjects[kObjects][kObjectBytes];
alignas(16) unsigned char gNotObjects[16][kObjectBytes];
std::atomic<int> gLogs{0};
std::atomic<int> gNameObjAsked{0};

int32_t class_of(const char* name) {
    gNameObjAsked++;
    return std::strcmp(name, "NameObj") == 0 ? kNameObj : -1;
}

// A cell of gObjects (or any byte of one) is a NameObj whose start is the cell's.
const void* as_class(const LumaMod*, const void* object, int32_t number) {
    const auto* p = static_cast<const unsigned char*>(object);
    const auto* first = &gObjects[0][0];
    if (number != kNameObj || p < first || p >= first + sizeof(gObjects)) {
        return nullptr;
    }
    return first + static_cast<size_t>(p - first) / kObjectBytes * kObjectBytes;
}

void log(const LumaMod*, const char*) {
    gLogs++;
}

LumaCore make_core() {
    LumaCore core{};
    core.class_of = class_of;
    core.as_class = as_class;
    core.log = log;
    return core;
}

const LumaCore gCore = make_core();
const LumaMod* const gModA = reinterpret_cast<const LumaMod*>(&gNotObjects[14]);
const LumaMod* const gModB = reinterpret_cast<const LumaMod*>(&gNotObjects[15]);

int gFailures = 0;

bool expect(bool condition, const char* message) {
    if (!condition) {
        if (gFailures++ < 20) {
            std::cerr << "FAILED: " << message << '\n';
        }
    }
    return condition;
}

const void* obj(size_t i) {
    return gObjects[i];
}

// Hostile and empty inputs: null, an object that is no NameObj, handle 0, handles weak never gave.
void noneAndHostile() {
    expect(luma::weak::make(gModA, nullptr) == 0, "make(null) is 0");
    expect(luma::weak::make(gModA, gNotObjects[0]) == 0, "make(an object that is no NameObj) is 0");
    expect(luma::weak::get(0) == nullptr, "get(0) is null");
    expect(luma::weak::get(1) == nullptr, "get of a slot never given is null");
    expect(luma::weak::get(0xffffffffull) == nullptr, "get of the last slot number is null");
    expect(luma::weak::get(~0ull) == nullptr, "get(all ones) is null");
    expect(luma::weak::get(0xffffffff00000000ull) == nullptr, "get(slot 0, any generation) is null");
    luma::weak::gone(gNotObjects[1]);
    luma::weak::freed(nullptr);
    luma::weak::freed(gNotObjects[2]);
    luma::weak::released(gNotObjects[3], gNotObjects[3]);
    luma::weak::released(gNotObjects[4], gNotObjects[3]);
    expect(gLogs == 0, "nothing said for null or no NameObj");
}

void sameForEveryMod() {
    const uint64_t a = luma::weak::make(gModA, obj(0));
    const uint64_t b = luma::weak::make(gModB, obj(0));
    const uint64_t inner = luma::weak::make(gModB, static_cast<const unsigned char*>(obj(0)) + 24);
    expect(a != 0, "a handle for a NameObj");
    expect(a == b, "every mod gets the same handle while the object lives");
    expect(a == inner, "a subobject's handle is its NameObj's");
    expect(luma::weak::get(a) == obj(0), "the handle reads the object");
    expect(luma::weak::get(a ^ (1ull << 32)) == nullptr, "another generation of the slot reads null");
    luma::weak::gone(obj(0));
    expect(luma::weak::get(a) == nullptr, "the handle reads null once the destructor ran");
    const uint64_t again = luma::weak::make(gModA, obj(0));
    expect(again != 0 && again != a, "an object at the same address later gets a new handle");
    expect(luma::weak::get(a) == nullptr, "the old handle stays null");
    expect(luma::weak::get(again) == obj(0), "the new one reads it");
    luma::weak::freed(obj(0));
    expect(luma::weak::get(again) == nullptr, "NebulaArena::free of its block: null");
    expect(gNameObjAsked == 1, "NameObj's class number is asked once");
}

void arenaRange() {
    std::vector<uint64_t> handles;
    for (size_t i = 100; i < 200; i++) {
        handles.push_back(luma::weak::make(gModA, obj(i)));
    }
    const uint64_t outside = luma::weak::make(gModA, obj(200));
    luma::weak::released(obj(100), obj(200));
    bool allNull = true;
    for (const uint64_t h : handles) {
        allNull &= luma::weak::get(h) == nullptr;
    }
    expect(allNull, "an arena's end empties every handle to an object in it");
    expect(luma::weak::get(outside) == obj(200), "an object just past the arena's end stays");
    luma::weak::gone(obj(200));
}

// A model: the handle each object has now, and handles that went (each must read null forever).
void randomAgainstModel(uint32_t seed, int steps) {
    std::mt19937 rng(seed);
    std::unordered_map<size_t, uint64_t> live;
    std::vector<uint64_t> went;
    for (int step = 0; step < steps; step++) {
        const uint32_t what = rng() % 100;
        const size_t i = rng() % kObjects;
        if (what < 55) {
            const uint64_t h = luma::weak::make((rng() & 1) ? gModA : gModB, obj(i));
            const auto found = live.find(i);
            if (found != live.end()) {
                expect(h == found->second, "a live object keeps its handle");
            } else {
                expect(h != 0, "a new handle");
                live.emplace(i, h);
            }
        } else if (what < 75) {
            luma::weak::gone(obj(i));
            const auto found = live.find(i);
            if (found != live.end()) {
                went.push_back(found->second);
                live.erase(found);
            }
        } else if (what < 90) {
            luma::weak::freed(obj(i));
            const auto found = live.find(i);
            if (found != live.end()) {
                went.push_back(found->second);
                live.erase(found);
            }
        } else if (what < 92) {
            const size_t end = i + rng() % 300 + 1 > kObjects ? kObjects : i + rng() % 300 + 1;
            luma::weak::released(obj(i), end == kObjects ? static_cast<const void*>(gObjects + kObjects) : obj(end));
            for (size_t k = i; k < end; k++) {
                const auto found = live.find(k);
                if (found != live.end()) {
                    went.push_back(found->second);
                    live.erase(found);
                }
            }
        } else {
            // Read a few handles of each kind.
            for (int k = 0; k < 8 && !live.empty(); k++) {
                const size_t j = rng() % kObjects;
                const auto found = live.find(j);
                if (found != live.end()) {
                    expect(luma::weak::get(found->second) == obj(j), "a live object's handle reads it");
                }
            }
            for (int k = 0; k < 8 && !went.empty(); k++) {
                expect(luma::weak::get(went[rng() % went.size()]) == nullptr, "a gone object's handle reads null");
            }
        }
        if (step % 50000 == 49999) {
            for (const auto& [j, h] : live) {
                expect(luma::weak::get(h) == obj(j), "every live handle reads its object");
            }
        }
    }
    for (const auto& [j, h] : live) {
        expect(luma::weak::get(h) == obj(j), "every live handle reads its object at the end");
        luma::weak::gone(obj(j));
        expect(luma::weak::get(h) == nullptr, "and null once it went");
    }
    for (const uint64_t h : went) {
        expect(luma::weak::get(h) == nullptr, "every gone handle reads null at the end");
    }
}

// Threads: each owns a band of objects (makes, reads, ends them, checking its own model), while one more thread
// ends whole ranges in a band no other thread uses and every thread reads handles of all bands.
void threads() {
    constexpr int kThreads = 4;
    constexpr size_t kBand = kObjects / (kThreads + 1);
    std::vector<std::atomic<uint64_t>> shared(kObjects);
    std::atomic<bool> stop{false};
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; t++) {
        workers.emplace_back([t, &shared] {
            std::mt19937 rng(1000u + static_cast<uint32_t>(t));
            std::vector<uint64_t> mine(kBand, 0);
            for (int step = 0; step < 300000; step++) {
                const size_t k = rng() % kBand;
                const size_t i = static_cast<size_t>(t) * kBand + k;
                switch (rng() % 4) {
                case 0:
                case 1: {
                    const uint64_t h = luma::weak::make(gModA, obj(i));
                    expect(h != 0 && (mine[k] == 0 || mine[k] == h), "a thread's object keeps its handle");
                    mine[k] = h;
                    shared[i].store(h, std::memory_order_relaxed);
                    break;
                }
                case 2:
                    if (mine[k] != 0) {
                        expect(luma::weak::get(mine[k]) == obj(i), "a thread's live object reads");
                        ((rng() & 1) ? luma::weak::gone : luma::weak::freed)(obj(i));
                        expect(luma::weak::get(mine[k]) == nullptr, "a thread's gone object reads null");
                        mine[k] = 0;
                    }
                    break;
                default: {
                    const uint64_t h = shared[rng() % kObjects].load(std::memory_order_relaxed);
                    const void* const o = luma::weak::get(h);
                    expect(o == nullptr || as_class(gModA, o, kNameObj) == o, "any handle reads null or an object");
                    break;
                }
                }
            }
            for (size_t k = 0; k < kBand; k++) {
                luma::weak::gone(obj(static_cast<size_t>(t) * kBand + k));
            }
        });
    }
    std::thread ender([&stop] {
        std::mt19937 rng(77u);
        const size_t first = kThreads * kBand;
        while (!stop.load()) {
            const size_t a = first + rng() % (kObjects - first);
            const size_t b = a + 1 + rng() % 64;
            std::vector<uint64_t> handles;
            for (size_t i = a; i < b && i < kObjects; i++) {
                handles.push_back(luma::weak::make(gModB, obj(i)));
            }
            luma::weak::released(obj(a), b >= kObjects ? static_cast<const void*>(gObjects + kObjects) : obj(b));
            for (const uint64_t h : handles) {
                expect(h != 0 && luma::weak::get(h) == nullptr, "an ended range's handles read null");
            }
        }
    });
    for (auto& w : workers) {
        w.join();
    }
    stop = true;
    ender.join();
}

}  // namespace

namespace luma {
    const LumaCore* core() {
        return &gCore;
    }
}  // namespace luma

extern "C" int nebula_snprintf(char* pDst, size_t length, const char* pFormat, ...) {
    va_list args;
    va_start(args, pFormat);
    const int n = std::vsnprintf(pDst, length, pFormat, args);
    va_end(args);
    return n;
}

int main() {
    noneAndHostile();
    sameForEveryMod();
    arenaRange();
    randomAgainstModel(1u, 400000);
    randomAgainstModel(2u, 400000);
    threads();
    // After everything went, the table works as new.
    const uint64_t h = luma::weak::make(gModA, obj(5));
    expect(h != 0 && luma::weak::get(h) == obj(5), "the table works after it emptied");
    luma::weak::gone(obj(5));
    expect(gLogs == 0, "no memory was never said");
    if (gFailures != 0) {
        std::cerr << gFailures << " failed\n";
        return 1;
    }
    std::cout << "luma_weak: all passed\n";
    return 0;
}
