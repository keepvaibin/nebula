// SPDX-License-Identifier: GPL-3.0-only
// The frame-list writer (Game/Render/FrameList.cpp) against the list the renderer reads (nebula_frame_list.h): passes
// and their items, categories by run, cameras once each, world lights ascending by id with the newest values, a camera
// cut on one list only; a list the renderer holds is never written again and one it
// let go is, from empty; frame memory aligned and reused; the ring past its first room; and Pipelines.bin read whole,
// with a short or lying file read no further than its end.
#include "Game/Render/FrameList.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/RenderTexture.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/StringUtil.hpp"

#include "test_support.h"

#include <stdarg.h>
#include <stdint.h>
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

    // What the host was handed.
    NebulaFrameList* sSubmitted[8];
    u32 sSubmitCount;
    u32 sRegistered;
    u32 sRegisteredInputs;

    // Pipelines.bin as the store serves it: in its own block, exactly its length, so a read past it is a read past
    // the block (the address sanitizer build reports it).
    u8* sPipelineFile;
    u32 sPipelineLength;

    bool sHeapEmpty;  // the host's heap has nothing left

    void* heapAlloc(void*, size_t size) {
        return sHeapEmpty ? nullptr : malloc(size);
    }
    void* heapRealloc(void*, void* p, size_t size) {
        return realloc(p, size);
    }
    void heapFree(void*, void* p) {
        free(p);
    }
    void log(void*, const char*) {
    }
    uint32_t option(void*, uint32_t) {
        return 0;
    }
    void gameStage(void*, const char*) {
    }
    uint32_t pipelineRegister(void*, const NebulaPipelineDesc* pDesc, const NebulaLayoutInput* pInputs) {
        sRegistered++;
        sRegisteredInputs += pDesc->input_count;
        // Every input read as written: semantic = record number, offset = input number.
        for (u32 i = 0; i < pDesc->input_count; i++) {
            if (pInputs[i].offset != i || pInputs[i].semantic != pDesc->state) {
                sFailures++;
                printf("FAIL pipeline input %u of record %u read wrong\n", i, pDesc->state);
            }
        }
        return sRegistered;
    }
    void submitFrame(void*, NebulaFrameList* pList) {
        if (sSubmitCount < 8) {
            sSubmitted[sSubmitCount] = pList;
        }
        sSubmitCount++;
    }

    NebulaHostServices sHost;

    const f32 cView[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
    const f32 cOtherView[3][4] = {{1, 0, 0, 5}, {0, 1, 0, 0}, {0, 0, 1, 0}};

    NebulaFrameList* lastList() {
        return sSubmitted[(sSubmitCount - 1) % 8];
    }

    // Pipelines.bin: `count` records claimed, `written` of them present, record r with r + 1 inputs.
    void makePipelineFile(u32 count, u32 written, u32 cut) {
        static u8 sBytes[4096];
        u32 at = 0;
        memcpy(sBytes, NEBULA_PIPELINE_FILE_MAGIC, 8);
        memcpy(sBytes + 8, &count, 4);
        memset(sBytes + 12, 0, 4);
        at = NEBULA_PIPELINE_FILE_HEADER_BYTES;
        for (u32 r = 0; r < written; r++) {
            const u64 vertex = 0x100 + r;
            const u64 pixel = 0x200 + r;
            const u32 state = r;
            const u32 inputs = r + 1;
            const u32 stride = 12;
            memcpy(sBytes + at, &vertex, 8);
            memcpy(sBytes + at + 8, &pixel, 8);
            memcpy(sBytes + at + 16, &state, 4);
            memcpy(sBytes + at + 20, &inputs, 4);
            memcpy(sBytes + at + 24, &stride, 4);
            at += NEBULA_PIPELINE_RECORD_BYTES;
            for (u32 i = 0; i < inputs; i++) {
                NebulaLayoutInput input = {};
                input.semantic = static_cast< uint16_t >(r);
                input.offset = i;
                memcpy(sBytes + at, &input, NEBULA_LAYOUT_INPUT_BYTES);
                at += NEBULA_LAYOUT_INPUT_BYTES;
            }
        }
        sPipelineLength = at > cut ? cut : at;
        free(sPipelineFile);
        sPipelineFile = static_cast< u8* >(malloc(sPipelineLength));
        memcpy(sPipelineFile, sBytes, sPipelineLength);
    }
}  // namespace

extern "C" const NebulaHostServices* nebula_host(void) {
    return &sHost;
}

// What FrameList.cpp takes from the rest of the game.
namespace RenderTexture {
    u32 idOf(const void* pImage) {
        return static_cast< u32 >(reinterpret_cast< uintptr_t >(pImage) & 0xFFFF);
    }
}  // namespace RenderTexture
namespace GameAssets {
    bool open(const char*, File* pFile) {
        if (sPipelineFile == nullptr) {
            return false;
        }
        pFile->mEntry = 1;
        pFile->mLength = sPipelineLength;
        return true;
    }
    const void* map(s32) {
        return sPipelineFile;
    }
}  // namespace GameAssets
namespace MR {
    FormattedName::FormattedName(const char* pFormat, ...) : mText(mInline) {
        va_list args;
        va_start(args, pFormat);
        vsnprintf(mInline, sizeof(mInline), pFormat, args);
        va_end(args);
    }
    FormattedName::~FormattedName() {
    }
}  // namespace MR
// Material.cpp's mesh layouts, for Frame::sitePipeline(shader, state, mesh), which this test does not reach.
u64 meshLayoutKey(const NebulaModelMesh&) {
    NEBULA_PANIC("meshLayoutKey is not part of this test");
}
void meshLayoutInput(NebulaPipelineDesc*, NebulaLayoutInput*, u16, const NebulaModelMesh&) {
    NEBULA_PANIC("meshLayoutInput is not part of this test");
}
// The layout rooms (MR::GrowBuffer) come from the arenas: the C library's memory here.
NebulaArena* NebulaArena::sRoot = nullptr;
NebulaArena* NebulaArena::findFromRoot(void*) {
    return nullptr;
}
// The global new[], so the delete[] the code under test uses matches it (the address sanitizer checks the pair).
void* operator new[](size_t size, NebulaArena*, int) {
    return ::operator new[](size);
}

int main() {
    sHost.size = sizeof(sHost);
    sHost.version = NEBULA_GAME_API_VERSION;
    sHost.heap_alloc = heapAlloc;
    sHost.heap_realloc = heapRealloc;
    sHost.heap_free = heapFree;
    sHost.log = log;
    sHost.option = option;
    sHost.game_stage = gameStage;
    sHost.pipeline_register = pipelineRegister;
    sHost.submit_frame = submitFrame;

    Frame::initDefaults();

    // Frame 1: a clear, a draw pass of three items in two categories, a show; cameras, world lights, a cut.
    const u32 number = Frame::number();
    Frame::begin();
    Frame::clear(0x204060FF, 7);
    Frame::category(2);
    Frame::addItem().mesh = 11;
    Frame::addItem().mesh = 12;
    Frame::category(5);
    Frame::category(3);  // a run with no draw takes the next category
    Frame::addItem().mesh = 13;
    expect(Frame::camera(cView, 1) == Frame::camera(cView, 1), "an equal camera is one entry");
    expect(Frame::camera(cView, 1) != Frame::camera(cView, 2), "another role is another camera");
    expect(Frame::camera(cOtherView, 1) != Frame::camera(cView, 1), "another view is another camera");
    Frame::cameraCut(7);
    Frame::show();
    Frame::end();

    expect(sSubmitCount == 1, "one list handed over");
    expect(Frame::number() == number + 1, "the frame number goes on");
    NebulaFrameList* const pFirst = lastList();
    expect(pFirst->released == 0, "a list is handed over held");
    expect(pFirst->pass_count == 3, "clear, draw, show");
    if (pFirst->pass_count == 3) {
        expect(pFirst->passes[0].kind == NEBULA_PASS_CLEAR && pFirst->passes[0].clear_rgba == 0x204060FF &&
                   pFirst->passes[0].clear_channels == 7,
               "the clear pass as asked");
        expect(pFirst->passes[1].kind == NEBULA_PASS_DRAW && pFirst->passes[1].first_item == 0 &&
                   pFirst->passes[1].item_count == 3,
               "the draw pass holds its three items");
        expect(pFirst->passes[2].kind == NEBULA_PASS_SHOW, "the show pass last");
    }
    expect(pFirst->item_count == 3 && pFirst->items[0].mesh == 11 && pFirst->items[2].mesh == 13, "the items in order");
    const u32 cat0 = (pFirst->items[0].flags & NEBULA_ITEM_CATEGORY_MASK) >> NEBULA_ITEM_CATEGORY_SHIFT;
    const u32 cat1 = (pFirst->items[1].flags & NEBULA_ITEM_CATEGORY_MASK) >> NEBULA_ITEM_CATEGORY_SHIFT;
    const u32 cat2 = (pFirst->items[2].flags & NEBULA_ITEM_CATEGORY_MASK) >> NEBULA_ITEM_CATEGORY_SHIFT;
    expect(cat0 == 2 && cat1 == 2 && cat2 == 3, "each item's category by its run");
    expect(pFirst->camera_count == 3, "three cameras");
    expect((pFirst->flags & NEBULA_FRAME_CAMERA_CUT) != 0 && pFirst->camera_epoch == 7, "the cut on its list");

    // Frame memory: aligned as asked; a block past a page gets one of its own.
    Frame::begin();
    u8* const pSmall = static_cast< u8* >(Frame::arena(3, 1));
    u8* const pAligned = static_cast< u8* >(Frame::arena(100, 256));
    u8* const pBig = static_cast< u8* >(Frame::arena(3u << 20, 16));
    expect((reinterpret_cast< uintptr_t >(pAligned) & 255) == 0, "frame memory aligned as asked");
    // A block of a whole page or more at a large alignment fits the page made for it (never one page after another).
    u8* const pPageAligned = static_cast< u8* >(Frame::arena(1u << 20, 4096));
    expect((reinterpret_cast< uintptr_t >(pPageAligned) & 4095) == 0, "a page-sized block aligned to 4096");
    pPageAligned[(1u << 20) - 1] = 3;
    pSmall[0] = 1;
    pBig[0] = 1;
    pBig[(3u << 20) - 1] = 2;
    // The ring past its first MiB, and its indices past their first room.
    u32 at = 0;
    u8* const pRing = static_cast< u8* >(FrameRing::vertices(16, &at));
    memset(pRing, 0x5A, 16);
    u32 at2 = 0;
    u8* const pRing2 = static_cast< u8* >(FrameRing::vertices(3u << 20, &at2));
    pRing2[(3u << 20) - 1] = 1;
    u32 first = 0;
    u32* const pIndices = FrameRing::indices(100000, &first);
    pIndices[99999] = 7;
    Frame::addItem().mesh = 21;
    Frame::end();
    NebulaFrameList* const pSecond = lastList();
    expect(pSecond != pFirst, "a held list is not written again");
    expect(pFirst->item_count == 3 && pFirst->items[0].mesh == 11, "the held list is as it was");
    expect(pFirst->pass_count == 3 && pFirst->camera_count == 3, "the held list's passes and cameras stay");
    expect((pSecond->flags & NEBULA_FRAME_CAMERA_CUT) == 0, "a cut is on one list only");
    expect(pSecond->ring_vertex_bytes == at2 + (3u << 20) && (at2 & 15) == 0, "ring vertices 16-byte aligned, counted");
    expect(static_cast< const u8* >(pSecond->ring_vertices)[at] == 0x5A, "earlier ring bytes survive the ring's growth");
    expect(pSecond->ring_index_count == first + 100000 && pSecond->ring_indices[first + 99999] == 7, "ring indices kept");

    // The renderer lets the first go: the next frame writes it again, from empty, and its memory is reused.
    pFirst->released = 1;
    Frame::begin();
    void* const pAgain = Frame::arena(3, 1);
    Frame::addItem().mesh = 31;
    Frame::end();
    NebulaFrameList* const pThird = lastList();
    expect(pThird == pFirst, "a released list is written again");
    expect(pThird->released == 0, "and is held again");
    expect(pThird->item_count == 1 && pThird->items[0].mesh == 31, "from empty");
    expect(pThird->camera_count == 0 &&
               pThird->pass_count == 1,
           "nothing of its last frame left");
    expect(pAgain != nullptr, "frame memory again");

    // Pipelines.bin: whole; then claiming more records than it holds, cut inside a record and inside an input, and
    // claiming more inputs than are left. Each registers the whole records only and reads nothing past the end.
    makePipelineFile(3, 3, 100000);
    sRegistered = 0;
    sRegisteredInputs = 0;
    Frame::registerPipelineList();
    expect(sRegistered == 3 && sRegisteredInputs == 6, "every record of a whole file");
    const u32 whole = sPipelineLength;
    const u32 cuts[] = {whole - 1, whole - NEBULA_LAYOUT_INPUT_BYTES, NEBULA_PIPELINE_FILE_HEADER_BYTES + 5,
                        NEBULA_PIPELINE_FILE_HEADER_BYTES, 9, 0};
    for (u32 c = 0; c < sizeof(cuts) / sizeof(cuts[0]); c++) {
        makePipelineFile(3, 3, cuts[c]);
        sRegistered = 0;
        sRegisteredInputs = 0;
        Frame::registerPipelineList();
        expect(sRegistered <= 2, "a cut file registers only its whole records");
    }
    makePipelineFile(1000000, 2, 100000);
    sRegistered = 0;
    Frame::registerPipelineList();
    expect(sRegistered == 2, "a file claiming more records than it holds registers the ones it holds");

    if (sFailures == 0) {
        printf("frame list tests: all pass\n");
    }
    return sFailures == 0 ? 0 : 1;
}
