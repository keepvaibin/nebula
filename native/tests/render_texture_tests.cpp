// SPDX-License-Identifier: GPL-3.0-only
// RenderTexture (Game/Render/RenderTexture.cpp), the renderer's numbers for the game's textures by their image's address:
// each image keeps its number while the table grows past its first room, and a table the host's heap cannot hold stops
// the game with a named panic, the table as it was left whole (P-255), not a write through a null pointer.
#include "Game/Render/RenderTexture.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/System/NebulaArena.hpp"

#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
    int sFailures;

    void expect(bool ok, const char* pWhat) {
        if (!ok) {
            printf("FAIL %s\n", pWhat);
            sFailures++;
        }
    }

    NebulaHostServices sHost;
    bool sHeapEmpty;  // the host's heap has nothing left
    u32 sNextTarget = 1;

    void* heapAlloc(void*, size_t size) {
        return sHeapEmpty ? nullptr : malloc(size);
    }
    void heapFree(void*, void* p) {
        free(p);
    }
    // Images that are no texture record are keys the game draws into (targets): numbered in turn.
    uint32_t textureTarget(void*) {
        return sNextTarget++;
    }
    void textureFree(void*, uint32_t) {
    }

    // Addresses only: the table never reads an image that is no record past its header's first bytes, which are zero.
    const u32 cImages = 12000;
    u8 sImages[cImages][16];
    u32 sFirst[6000];  // the numbers the first 6000 images were given
}  // namespace

extern "C" const NebulaHostServices* nebula_host(void) {
    return &sHost;
}
// No arena holds these images: no list of an arena's images is made.
NebulaArena* NebulaArena::findFromRoot(void*) {
    return nullptr;
}
namespace GameAssets {
    NebulaArena* viewArena(const void*) {
        return nullptr;
    }
}  // namespace GameAssets
// A list of an arena's images is never made here (findFromRoot finds none); its base's members link and do nothing.
ArenaOwned::ArenaOwned() : mArena(nullptr) {
}
ArenaOwned::~ArenaOwned() {
    (void)mArena;
}
void* operator new(size_t size, NebulaArena*, int) {
    return ::operator new(size);
}
void* operator new[](size_t size, NebulaArena*, int) {
    return ::operator new[](size);
}

int main() {
    sHost.size = sizeof(sHost);
    sHost.version = NEBULA_GAME_API_VERSION;
    sHost.heap_alloc = heapAlloc;
    sHost.heap_free = heapFree;
    sHost.texture_target = textureTarget;
    sHost.texture_free = textureFree;

    // The first room (8192 images) doubles at half full: 6000 images take it to 16384, and the 8193rd asks for 32768.
    for (u32 i = 0; i < 6000; i++) {
        sFirst[i] = RenderTexture::idOf(sImages[i]);
    }
    bool same = true;
    bool distinct = true;
    for (u32 i = 0; i < 6000; i++) {
        same = same && RenderTexture::idOf(sImages[i]) == sFirst[i];
        distinct = distinct && sFirst[i] == i + 1;
    }
    expect(distinct, "each image its own number");
    expect(same, "each image keeps its number after the table grows");

    // The next growth with the heap empty: a named panic, and the table as it was.
    sHeapEmpty = true;
    u32 added = 6000;
    bool panicked = false;
    while (added < cImages && !panicked) {
        const u32 at = added;
        panicked = NEBULA_TEST_PANICS(RenderTexture::idOf(sImages[at]));
        if (!panicked) {
            added++;
        }
    }
    expect(panicked, "a table the heap cannot hold panics");
    expect(strstr(nebula_test_panic_text, "RenderTexture: no memory for a table of 32768 textures") != nullptr,
           "the panic names the table and its room");
    sHeapEmpty = false;
    same = true;
    for (u32 i = 0; i < added; i++) {
        same = same && RenderTexture::idOf(sImages[i]) == i + 1;
    }
    expect(same, "after the panic, the table as it was: every image keeps its number");

    if (sFailures == 0) {
        printf("render_texture_tests: all passed\n");
        return 0;
    }
    return 1;
}
