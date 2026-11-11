#include "Game/Render/FrameList.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/RenderTexture.hpp"
#include <nebula_format.h>
#include <string.h>
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_specularDirHA_f1, "specularDirHA.f1", 1.0e18f);
NEBULA_TUNABLE_NAMED(f32, cNum_distAttn_f1, "distAttn.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_distAttn_f2, "distAttn.f2", 0.5f);

extern "C" const NebulaHostServices* nebula_host(void);

u32 PassState::word() const {
    u32 state = NEBULA_STATE_BLEND(blend_op, blend_src, blend_dst);
    state |= NEBULA_STATE_DEPTH(depth_test != 0, depth_func, depth_write != 0);
    state |= NEBULA_STATE_CULL(cull);
    state |= color_write != 0 ? NEBULA_STATE_COLOR_WRITE : 0u;
    state |= alpha_write == 2 ? NEBULA_STATE_ALPHA_PASS : alpha_write != 0 ? NEBULA_STATE_ALPHA_WRITE : 0u;
    state |= pixel_shading ? NEBULA_STATE_PIXEL_SHADING : 0u;
    if (stencil_func != 0xFF) {
        state |= NEBULA_STATE_STENCIL(stencil_func, stencil_op) | NEBULA_STATE_STENCIL_OWN;
    }
    return state;
}

namespace LightValues {
    void color(NebulaLight* pLight, NebulaColor8 color) {
        pLight->color[0] = color.r;
        pLight->color[1] = color.g;
        pLight->color[2] = color.b;
        pLight->color[3] = color.a;
    }

    void pos(NebulaLight* pLight, f32 x, f32 y, f32 z) {
        pLight->position[0] = x;
        pLight->position[1] = y;
        pLight->position[2] = z;
    }

    void dir(NebulaLight* pLight, f32 nx, f32 ny, f32 nz) {
        pLight->direction[0] = -nx;
        pLight->direction[1] = -ny;
        pLight->direction[2] = -nz;
    }

    void specularDir(NebulaLight* pLight, f32 nx, f32 ny, f32 nz) {
        const f32 hx = -nx;
        const f32 hy = -ny;
        const f32 hz = 1.0f - nz;
        f32 mag = hx * hx + hy * hy + hz * hz;
        if (mag != 0.0f) {
            mag = 1.0f / __builtin_sqrtf(mag);
        }
        specularDirHA(pLight, nx, ny, nz, hx * mag, hy * mag, hz * mag);
    }

    void specularDirHA(NebulaLight* pLight, f32 nx, f32 ny, f32 nz, f32 hx, f32 hy, f32 hz) {
        const f32 cInfiniteLightDistance = cNum_specularDirHA_f1;
        pLight->direction[0] = hx;
        pLight->direction[1] = hy;
        pLight->direction[2] = hz;
        pLight->position[0] = nx * -cInfiniteLightDistance;
        pLight->position[1] = ny * -cInfiniteLightDistance;
        pLight->position[2] = nz * -cInfiniteLightDistance;
    }

    void attn(NebulaLight* pLight, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2) {
        pLight->angle[0] = a0;
        pLight->angle[1] = a1;
        pLight->angle[2] = a2;
        pLight->distance[0] = k0;
        pLight->distance[1] = k1;
        pLight->distance[2] = k2;
    }

    void noSpot(NebulaLight* pLight) {
        pLight->angle[0] = 1.0f;
        pLight->angle[1] = 0.0f;
        pLight->angle[2] = 0.0f;
    }

    void distAttn(NebulaLight* pLight, f32 refDistance, f32 refBrightness, LightFalloff falloff) {
        if (refDistance < 0.0f || refBrightness <= 0.0f || refBrightness >= 1.0f) {
            falloff = LIGHT_FALLOFF_NONE;
        }
        f32 k1 = 0.0f;
        f32 k2 = 0.0f;
        switch (falloff) {
        case LIGHT_FALLOFF_GENTLE:
            k1 = (1.0f - refBrightness) / (refBrightness * refDistance);
            break;
        case LIGHT_FALLOFF_MEDIUM:
            k1 = cNum_distAttn_f1 * (1.0f - refBrightness) / (refBrightness * refDistance);
            k2 = cNum_distAttn_f2 * (1.0f - refBrightness) / (refBrightness * refDistance * refDistance);
            break;
        case LIGHT_FALLOFF_STEEP:
            k2 = (1.0f - refBrightness) / (refBrightness * refDistance * refDistance);
            break;
        default:
            break;
        }
        pLight->distance[0] = 1.0f;
        pLight->distance[1] = k1;
        pLight->distance[2] = k2;
    }
}  // namespace LightValues

namespace PassStates {
    PassState use(u8 src, u8 dst, bool depthTest, u8 depthFunc, bool depthWrite, u8 cull, bool colorWrite, u8 alphaWrite) {
        PassState state = plain();
        setBlend(state, src, dst);
        setDepth(state, depthTest, depthFunc, depthWrite);
        state.cull = cull;
        state.color_write = colorWrite ? 1 : 0;
        state.alpha_write = alphaWrite;
        return state;
    }

    PassState plain() {
        PassState state;
        memset(&state, 0, sizeof(state));
        state.blend_op = NEBULA_BLEND_REPLACE;
        state.blend_src = NEBULA_FACTOR_ONE;
        state.blend_dst = NEBULA_FACTOR_ZERO;
        state.depth_func = NEBULA_COMPARE_ALWAYS;
        state.cull = NEBULA_CULL_NONE;
        state.color_write = 1;
        state.alpha_write = 2;
        state.stencil_func = 0xFF;
        state.line_width = 1.0f;
        state.point_size = 1.0f;
        return state;
    }

    PassState fromWord(u32 state) {
        PassState out = plain();
        out.blend_op = static_cast< u8 >(state & 3);
        out.blend_src = static_cast< u8 >(state >> 2 & 15);
        out.blend_dst = static_cast< u8 >(state >> 6 & 15);
        out.depth_test = (state & 1u << 10) != 0 ? 1 : 0;
        out.depth_func = static_cast< u8 >(state >> 11 & 7);
        out.depth_write = (state & 1u << 14) != 0 ? 1 : 0;
        out.cull = static_cast< u8 >(state >> 15 & 3);
        out.color_write = (state & NEBULA_STATE_COLOR_WRITE) != 0 ? 1 : 0;
        out.alpha_write = (state & NEBULA_STATE_ALPHA_PASS) != 0 ? 2 : (state & NEBULA_STATE_ALPHA_WRITE) != 0 ? 1 : 0;
        out.pixel_shading = (state & NEBULA_STATE_PIXEL_SHADING) != 0;
        if ((state & NEBULA_STATE_STENCIL_OWN) != 0) {
            out.stencil_func = static_cast< u8 >(state >> 20 & 3);
            out.stencil_op = static_cast< u8 >(state >> 22 & 3);
        }
        return out;
    }

    void setDepth(PassState& rState, bool test, u8 func, bool write) {
        rState.depth_test = test ? 1 : 0;
        rState.depth_func = test ? func : static_cast< u8 >(NEBULA_COMPARE_ALWAYS);
        rState.depth_write = test && write ? 1 : 0;
    }

    void setBlend(PassState& rState, u8 src, u8 dst) {
        rState.blend_op = NEBULA_BLEND_ADD;
        rState.blend_src = src;
        rState.blend_dst = dst;
    }

    void setNoBlend(PassState& rState) {
        rState.blend_op = NEBULA_BLEND_REPLACE;
        rState.blend_src = NEBULA_FACTOR_ONE;
        rState.blend_dst = NEBULA_FACTOR_ZERO;
    }
}  // namespace PassStates

namespace {
    const NebulaHostServices* host() {
        return nebula_host();
    }

    template < class T >
    struct Grown {
        T* mData;
        u32 mCount;
        u32 mRoom;

        T& add() {
            if (mCount == mRoom) {
                reserve(mRoom != 0 ? mRoom * 2 : 256);
            }
            return mData[mCount++];
        }

        // `count` more entries in one run; the first is returned.
        T* addRun(u32 count) {
            if (mCount + count > mRoom) {
                u32 room = mRoom != 0 ? mRoom : 256;
                while (room < mCount + count) {
                    room *= 2;
                }
                reserve(room);
            }
            T* pFirst = mData + mCount;
            mCount += count;
            return pFirst;
        }

        void reserve(u32 room) {
            mData = static_cast< T* >(host()->heap_realloc(host()->user, mData, room * sizeof(T)));
            mRoom = room;
        }
    };

    // A row of NEBULA_ITEM_INSTANCES draws' instances.
    typedef f32 InstanceRow[4];

    struct Page {
        Page* mNext;
        u32 mRoom;
        u32 mUsed;
    };

    const u32 cPageBytes = 1u << 20;

    // A frame's list and everything it points to. The renderer holds on to two or three at a time; a list it let go is
    // written again, and when none is free there is one more.
    struct Store {
        NebulaFrameList mList;
        Store* mNext;
        Grown< NebulaFramePass > mPasses;
        Grown< NebulaFrameItem > mItems;
        Grown< NebulaFrameView > mViews;
        Grown< NebulaFrameCamera > mCameras;
        Grown< NebulaFrameBlock > mBlocks;
        Grown< NebulaFrameLightSet > mLightSets;
        Grown< NebulaFrameLight > mLights;
        Grown< u32 > mLightFollow;
        Grown< NebulaFrameLightOwner > mLightOwners;
        Grown< NebulaFrameWorldLight > mWorldLights;
        Grown< NebulaFrameShadowDrop > mShadowDrops;
        Grown< NebulaFramePointLight > mPointLights;
        Grown< u32 > mCategories;  // pairs: the first item of a run of draws, and the category they are drawn as
        Grown< NebulaFrameConstants > mConstants;
        Grown< NebulaScreenProbe > mProbes;
        Grown< u32 > mRingIndices;
        Grown< InstanceRow > mInstanceRows;
        u8* mRingVertices;
        u32 mRingBytes;
        u32 mRingRoom;
        Page* mFirstPage;
        Page* mPage;
    };

    Store* sStores;
    Store* sCurrent;
    u32 sNumber;
    s32 sOpenDraw = -1;

    bool sPixelLit;

    // How draws project now.
    bool sPerspective = false;
    f32 sTerms[6] = {1.0f, 0.0f, 1.0f, 0.0f, -1.0f, -2.0f};
    bool sViewportSet = false;
    f32 sViewport[6] = {};
    f32 sDepthScale = 1.0f;
    f32 sDepthOffset = 0.0f;
    bool sScissorSet = false;
    f32 sScissor[4] = {};
    bool sViewDirty = true;
    u32 sViewIndex;

    // The lights now: as many as the game has set (the highest number set, plus one), and the words saying which follow the
    // camera. A light set of the list is a copy of these.
    Grown< NebulaFrameLight > sLights;
    Grown< u32 > sLightFollow;
    u32 sLightsCamera = 0;
    bool sLightsDirty = true;
    bool sLightsSet = false;
    u32 sLightSet = NEBULA_NONE32;
    // Whose lights those are (NebulaFrameLightOwner), with the world lights of its key and fill and of its point light:
    // a set made from lights loaded on an earlier frame still brings its world lights into this frame's list.
    NebulaFrameLightOwner sLightOwner = {};
    NebulaFrameWorldLight sOwnerLights[2] = {};
    NebulaFrameWorldLight sOwnerPoint = {};
    // The stage's light, as the last frame that knew it said.
    u64 sStageLight = 0;
    f32 sStageAmbient[3] = {};
    bool sStageLightKnown = false;

    // The pass's own decisions.
    bool sPassAlpha = false;
    u8 sPassStencilTest = NEBULA_STENCIL_OFF;
    u8 sPassStencilOp = NEBULA_STENCIL_KEEP;

    u32 sTarget;
    u8 sLayer = NEBULA_LAYER_SCREEN;
    u8 sMainLayer = NEBULA_LAYER_SCREEN;
    u8 sPhase = 0xFF;

    u32 sSlot;
    u32 sGeneration;
    u32 sPart;
    u32 sNextSlot = 1;
    u32 sCuts;
    bool sCutPending;
    u32 sSceneCamera = NEBULA_NONE32;
    u32 sSceneView = NEBULA_NONE32;

    // The last asker given out. A probe set ends with one more point that shows nothing and carries its asker in user_x
    // (a whole number a float holds exactly), so an answer finds its asker.
    const u32 cLastAsker = 1u << 24;
    u32 sProbeAskers;
    Grown< NebulaScreenProbe > sProbeAnswers;

    void resetStore(Store* pStore) {
        pStore->mPasses.mCount = 0;
        pStore->mItems.mCount = 0;
        pStore->mViews.mCount = 0;
        pStore->mCameras.mCount = 0;
        pStore->mBlocks.mCount = 0;
        pStore->mLightSets.mCount = 0;
        pStore->mLights.mCount = 0;
        pStore->mLightFollow.mCount = 0;
        pStore->mLightOwners.mCount = 0;
        pStore->mWorldLights.mCount = 0;
        pStore->mShadowDrops.mCount = 0;
        pStore->mPointLights.mCount = 0;
        pStore->mCategories.mCount = 0;
        pStore->mConstants.mCount = 0;
        pStore->mProbes.mCount = 0;
        pStore->mRingIndices.mCount = 0;
        pStore->mInstanceRows.mCount = 0;
        pStore->mRingBytes = 0;
        pStore->mPage = pStore->mFirstPage;
        if (pStore->mPage != nullptr) {
            for (Page* p = pStore->mFirstPage; p != nullptr; p = p->mNext) {
                p->mUsed = sizeof(Page);
            }
        }
        memset(&pStore->mList, 0, sizeof(pStore->mList));
    }

    Store* store() {
        if (sCurrent != nullptr) {
            return sCurrent;
        }
        Store* pFree = nullptr;
        for (Store* p = sStores; p != nullptr; p = p->mNext) {
            if (p->mList.released != 0) {
                pFree = p;
                break;
            }
        }
        if (pFree == nullptr) {
            pFree = static_cast< Store* >(host()->heap_alloc(host()->user, sizeof(Store)));
            memset(pFree, 0, sizeof(Store));
            pFree->mNext = sStores;
            sStores = pFree;
        }
        resetStore(pFree);
        sCurrent = pFree;
        sOpenDraw = -1;
        sViewDirty = true;
        sLightsDirty = true;
        sLightSet = NEBULA_NONE32;
        sSceneCamera = NEBULA_NONE32;
        sSceneView = NEBULA_NONE32;
        return pFree;
    }

    NebulaFramePass& addPass(u8 kind) {
        Store* pStore = store();
        sOpenDraw = -1;
        NebulaFramePass& rPass = pStore->mPasses.add();
        memset(&rPass, 0, sizeof(rPass));
        rPass.kind = kind;
        rPass.layer = sLayer;
        rPass.phase = sPhase;
        rPass.target = sTarget;
        rPass.scene_camera = NEBULA_NONE32;
        rPass.scene_view = NEBULA_NONE32;
        return rPass;
    }

    void hostViewport(const f32* pViewport, f32* pOut) {
        const f32 viewport[6] = {pViewport[0] * NEBULA_FRAME_UNITS,    pViewport[1] * NEBULA_FRAME_UNITS,
                                 pViewport[2] * NEBULA_FRAME_UNITS,    pViewport[3] * NEBULA_FRAME_UNITS,
                                 pViewport[4] * sDepthScale + sDepthOffset, pViewport[5] * sDepthScale + sDepthOffset};
        for (u32 i = 0; i < 6; i++) {
            pOut[i] = viewport[i];
        }
    }

    void setProjection(const f32 (*pMtx)[4], bool perspective) {
        sPerspective = perspective;
        sTerms[0] = pMtx[0][0];
        sTerms[2] = pMtx[1][1];
        sTerms[4] = pMtx[2][2];
        sTerms[5] = pMtx[2][3];
        sTerms[1] = perspective ? pMtx[0][2] : pMtx[0][3];
        sTerms[3] = perspective ? pMtx[1][2] : pMtx[1][3];
        sViewDirty = true;
    }

    // Site pipelines by what names them; the renderer's table is the real one, this saves building a description.
    struct SitePipeline {
        u64 mKey;
        u32 mState;
        u32 mPipeline;
    };
    SitePipeline* sSitePipelines;
    u32 sSiteRoom;
    u32 sSiteCount;

    SitePipeline& siteEntry(u64 key, u32 state) {
        if (sSiteCount * 4 >= sSiteRoom * 3) {
            const u32 room = sSiteRoom != 0 ? sSiteRoom * 2 : 1024;
            auto* pNew = static_cast< SitePipeline* >(host()->heap_alloc(host()->user, room * sizeof(SitePipeline)));
            memset(pNew, 0, room * sizeof(SitePipeline));
            for (u32 i = 0; i < sSiteRoom; i++) {
                const SitePipeline& rOld = sSitePipelines[i];
                if (rOld.mKey == 0) { continue; }
                u32 at = static_cast< u32 >((rOld.mKey ^ rOld.mState) * 2654435761u) & (room - 1);
                while (pNew[at].mKey != 0) { at = (at + 1) & (room - 1); }
                pNew[at] = rOld;
            }
            host()->heap_free(host()->user, sSitePipelines);
            sSitePipelines = pNew;
            sSiteRoom = room;
        }
        u32 at = static_cast< u32 >((key ^ state) * 2654435761u) & (sSiteRoom - 1);
        while (sSitePipelines[at].mKey != 0 &&
               (sSitePipelines[at].mKey != key || sSitePipelines[at].mState != state)) {
            at = (at + 1) & (sSiteRoom - 1);
        }
        SitePipeline& rEntry = sSitePipelines[at];
        if (rEntry.mKey == 0) { rEntry.mKey = key; rEntry.mState = state; ++sSiteCount; }
        return rEntry;
    }
}  // namespace

namespace Frame {
    void initDefaults() {
        static const f32 cDefaultProjection[7] = {1.0f, 0.0f, 1.0f, 0.0f, -1.0f, -2.0f, 0.0f};
        setViewport(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
        setProjectionv(cDefaultProjection);
        setScissor(0.0f, 0.0f, 1.0f, 1.0f);
    }

    u32 number() {
        return sNumber;
    }

    void begin() {
        store();
        sPixelLit = host()->option(host()->user, NEBULA_OPTION_PIXEL_LIT) != 0;
        // Draws before the first object's take no object's identity (not the last frame's last object, with its count).
        setObject(0, 0);
    }

    void end() {
        Store* pStore = store();
        NebulaFrameList& rList = pStore->mList;
        rList.passes = pStore->mPasses.mData;
        rList.items = pStore->mItems.mData;
        rList.views = pStore->mViews.mData;
        rList.cameras = pStore->mCameras.mData;
        rList.blocks = pStore->mBlocks.mData;
        rList.light_sets = pStore->mLightSets.mData;
        rList.lights = pStore->mLights.mData;
        rList.light_follow = pStore->mLightFollow.mData;
        rList.light_set_owners = pStore->mLightOwners.mData;
        rList.world_lights = pStore->mWorldLights.mData;
        rList.world_light_count = pStore->mWorldLights.mCount;
        rList.shadow_drops = pStore->mShadowDrops.mData;
        rList.shadow_drop_count = pStore->mShadowDrops.mCount;
        rList.point_lights = pStore->mPointLights.mData;
        rList.point_light_count = pStore->mPointLights.mCount;
        // Each item's category, by the runs the draws were made in: a draw writes its item's flags whole when it is
        // made, so they are set now.
        for (u32 run = 0; run + 1 < pStore->mCategories.mCount; run += 2) {
            const u32 next = run + 2 < pStore->mCategories.mCount ? pStore->mCategories.mData[run + 2] : pStore->mItems.mCount;
            const u32 last = next < pStore->mItems.mCount ? next : pStore->mItems.mCount;
            const u32 bits = pStore->mCategories.mData[run + 1] << NEBULA_ITEM_CATEGORY_SHIFT;
            for (u32 item = pStore->mCategories.mData[run]; item < last; item++) {
                pStore->mItems.mData[item].flags = (pStore->mItems.mData[item].flags & ~static_cast< u32 >(NEBULA_ITEM_CATEGORY_MASK)) | bits;
            }
        }
        rList.stage_light_known = sStageLightKnown ? 1u : 0u;
        rList.stage_light = sStageLight;
        rList.stage_ambient[0] = sStageAmbient[0];
        rList.stage_ambient[1] = sStageAmbient[1];
        rList.stage_ambient[2] = sStageAmbient[2];
        // A scene that stops saying what its light is has none on the next frame.
        sStageLightKnown = false;
        rList.constants = pStore->mConstants.mData;
        rList.ring_vertices = pStore->mRingVertices;
        rList.ring_indices = pStore->mRingIndices.mData;
        rList.probes = pStore->mProbes.mData;
        rList.instance_rows = pStore->mInstanceRows.mData;
        rList.instance_row_count = pStore->mInstanceRows.mCount;
        rList.pass_count = pStore->mPasses.mCount;
        rList.item_count = pStore->mItems.mCount;
        rList.view_count = pStore->mViews.mCount;
        rList.camera_count = pStore->mCameras.mCount;
        rList.block_count = pStore->mBlocks.mCount;
        rList.light_set_count = pStore->mLightSets.mCount;
        rList.light_count = pStore->mLights.mCount;
        rList.light_follow_count = pStore->mLightFollow.mCount;
        rList.constants_count = pStore->mConstants.mCount;
        rList.probe_count = pStore->mProbes.mCount;
        rList.ring_vertex_bytes = pStore->mRingBytes;
        rList.ring_index_count = pStore->mRingIndices.mCount;
        rList.camera_epoch = sCuts;
        if (sCutPending) {
            rList.flags |= NEBULA_FRAME_CAMERA_CUT;
            sCutPending = false;
        }
        rList.released = 0;
        host()->submit_frame(host()->user, &rList);
        sCurrent = nullptr;
        sNumber++;
        sPhase = 0xFF;
    }

    void* arena(u32 bytes, u32 align) {
        Store* pStore = store();
        Page* pPage = pStore->mPage;
        u32 at = 0;
        if (pPage != nullptr) {
            at = (pPage->mUsed + (align - 1)) & ~(align - 1);
        }
        while (pPage == nullptr || at + bytes > pPage->mRoom) {
            Page* pNext = pPage != nullptr ? pPage->mNext : nullptr;
            if (pNext == nullptr) {
                const u32 room = bytes + 64 > cPageBytes ? bytes + 64 : cPageBytes;
                pNext = static_cast< Page* >(host()->heap_alloc(host()->user, room));
                pNext->mNext = nullptr;
                pNext->mRoom = room;
                pNext->mUsed = sizeof(Page);
                if (pPage != nullptr) {
                    pPage->mNext = pNext;
                } else {
                    pStore->mFirstPage = pNext;
                }
            }
            pPage = pNext;
            at = (pPage->mUsed + (align - 1)) & ~(align - 1);
        }
        pPage->mUsed = at + bytes;
        pStore->mPage = pPage;
        return reinterpret_cast< u8* >(pPage) + at;
    }

    NebulaFrameItem& addItem() {
        Store* pStore = store();
        if (sOpenDraw < 0) {
            NebulaFramePass& rPass = addPass(NEBULA_PASS_DRAW);
            rPass.first_item = pStore->mItems.mCount;
            sOpenDraw = static_cast< s32 >(pStore->mPasses.mCount - 1);
        }
        pStore->mPasses.mData[sOpenDraw].item_count++;
        NebulaFrameItem& rItem = pStore->mItems.add();
        memset(&rItem, 0, sizeof(rItem));
        return rItem;
    }

    NebulaFrameItem& itemAt(u32 index) {
        return store()->mItems.mData[index];
    }

    u32 itemCount() {
        return store()->mItems.mCount;
    }

    u32 addConstants(u32 slot, u32 vertexRows, u32 pixelRows, const f32 (*pRows)[4], const u32* pCoordRows, u32 coordCount) {
        u32* pKept = coordCount != 0 ? Frame::alloc< u32 >(coordCount) : nullptr;
        if (coordCount != 0) { memcpy(pKept, pCoordRows, coordCount * sizeof(u32)); }
        Store* pStore = store();
        NebulaFrameConstants& rConstants = pStore->mConstants.add();
        rConstants.slot = slot; rConstants.vertex_rows = vertexRows;
        rConstants.pixel_rows = pixelRows; rConstants.rows = pRows;
        rConstants.coord_rows = pKept; rConstants.coord_row_count = coordCount;
        return pStore->mConstants.mCount - 1;
    }


    u32 addBlock(const NebulaFrameBlock& rBlock) {
        Store* pStore = store();
        pStore->mBlocks.add() = rBlock;
        return pStore->mBlocks.mCount - 1;
    }

    u32 camera(const f32 (*pView)[4], u32 role) {
        Store* pStore = store();
        for (u32 i = pStore->mCameras.mCount; i != 0; i--) {
            const NebulaFrameCamera& rCamera = pStore->mCameras.mData[i - 1];
            if (rCamera.role == role && memcmp(rCamera.view, pView, sizeof(rCamera.view)) == 0) {
                return static_cast< u32 >(i - 1);
            }
        }
        NebulaFrameCamera& rCamera = pStore->mCameras.add();
        memcpy(rCamera.view, pView, sizeof(rCamera.view));
        rCamera.role = role;
        return static_cast< u32 >(pStore->mCameras.mCount - 1);
    }

    u32 currentView() {
        Store* pStore = store();
        if (sViewDirty) {
            NebulaFrameView& rView = pStore->mViews.add();
            memset(&rView, 0, sizeof(rView));
            memcpy(rView.terms, sTerms, sizeof(sTerms));
            hostViewport(sViewport, rView.viewport);
            for (u32 i = 0; i < 4; i++) {
                rView.scissor[i] = sScissor[i] * NEBULA_FRAME_UNITS;
            }
            rView.orthographic = sPerspective ? 0 : 1;
            sViewIndex = static_cast< u32 >(pStore->mViews.mCount - 1);
            sViewDirty = false;
        }
        return sViewIndex;
    }

    void setPerspective(const f32 (*pMtx)[4]) {
        setProjection(pMtx, true);
    }

    void setOrthographic(const f32 (*pMtx)[4]) {
        setProjection(pMtx, false);
    }

    void setProjectionv(const f32* pValues) {
        sPerspective = pValues[0] == 0.0f;
        for (u32 i = 0; i < 6; i++) {
            sTerms[i] = pValues[1 + i];
        }
        sViewDirty = true;
    }

    void getProjectionv(f32* pValues) {
        pValues[0] = sPerspective ? 0.0f : 1.0f;
        for (u32 i = 0; i < 6; i++) {
            pValues[1 + i] = sTerms[i];
        }
    }

    void setViewport(f32 left, f32 top, f32 width, f32 height, f32 nearZ, f32 farZ) {
        sViewport[0] = left;
        sViewport[1] = top;
        sViewport[2] = width;
        sViewport[3] = height;
        sViewport[4] = nearZ;
        sViewport[5] = farZ;
        sViewportSet = true;
        sViewDirty = true;
    }

    void getViewportv(f32* pValues) {
        if (!sViewportSet) {
            NEBULA_PANIC("Frame: the viewport read before the game set one");
        }
        for (u32 i = 0; i < 6; i++) {
            pValues[i] = sViewport[i];
        }
    }

    void setZScaleOffset(f32 scale, f32 offset) {
        sDepthScale = scale;
        sDepthOffset = offset;
        sViewDirty = true;
    }

    void setScissor(f32 left, f32 top, f32 width, f32 height) {
        sScissor[0] = left;
        sScissor[1] = top;
        sScissor[2] = width;
        sScissor[3] = height;
        sScissorSet = true;
        sViewDirty = true;
    }

    void getScissor(f32* pLeft, f32* pTop, f32* pWidth, f32* pHeight) {
        if (!sScissorSet) {
            NEBULA_PANIC("Frame: the scissor read before the game set one");
        }
        *pLeft = sScissor[0];
        *pTop = sScissor[1];
        *pWidth = sScissor[2];
        *pHeight = sScissor[3];
    }

    void hostView(const f32 (*pMtx)[4], f32 left, f32 top, f32 width, f32 height, f32 nearZ, f32 farZ, f32* pOut) {
        pOut[0] = pMtx[0][0];
        pOut[1] = pMtx[0][2];
        pOut[2] = pMtx[1][1];
        pOut[3] = pMtx[1][2];
        pOut[4] = pMtx[2][2];
        pOut[5] = pMtx[2][3];
        const f32 viewport[6] = {left, top, width, height, nearZ, farZ};
        hostViewport(viewport, pOut + 6);
    }

    static void growLights(u32 count) {
        while (sLights.mCount < count) {
            memset(&sLights.add(), 0, sizeof(NebulaFrameLight));
        }
    }

    static void growLightFollow(u32 words) {
        while (sLightFollow.mCount < words) {
            sLightFollow.add() = 0;
        }
    }

    u32 currentLightSet(u32 block) {
        if (!sLightsSet) {
            return NEBULA_NONE32;
        }
        Store* pStore = store();
        const u32 camera = pStore->mBlocks.mData[block].camera;
        const u32 cameraPlusOne = camera != NEBULA_NONE32 ? camera + 1u : 0u;
        if (sLightsCamera != cameraPlusOne) {
            sLightsCamera = cameraPlusOne;
            sLightsDirty = true;
        }
        if (sLightsDirty || sLightSet == NEBULA_NONE32) {
            // The list's set is a copy of the lights now: their run, and the words for as many lights.
            const u32 count = sLights.mCount;
            const u32 words = (count + 31u) / 32u;
            growLightFollow(words);
            NebulaFrameLightSet& rSet = pStore->mLightSets.add();
            rSet.first = pStore->mLights.mCount;
            rSet.count = count;
            rSet.follow_first = pStore->mLightFollow.mCount;
            rSet.camera_plus_one = cameraPlusOne;
            if (count != 0) {
                memcpy(pStore->mLights.addRun(count), sLights.mData, count * sizeof(NebulaFrameLight));
            }
            if (words != 0) {
                memcpy(pStore->mLightFollow.addRun(words), sLightFollow.mData, words * sizeof(u32));
            }
            pStore->mLightOwners.add() = sLightOwner;
            if (sLightOwner.actor_light != 0) {
                worldLight(sOwnerLights[0]);
                worldLight(sOwnerLights[1]);
            }
            if (sLightOwner.point_light != 0) {
                worldLight(sOwnerPoint);
            }
            sLightSet = static_cast< u32 >(pStore->mLightSets.mCount - 1);
            sLightsDirty = false;
        }
        return sLightSet;
    }

    void setLight(u32 index, const NebulaLight& rLight) {
        growLights(index + 1);
        NebulaFrameLight& rOut = sLights.mData[index];
        for (u32 c = 0; c < 4; c++) {
            rOut.color[c] = rLight.color[c];
        }
        memcpy(rOut.angle, rLight.angle, sizeof(rOut.angle));
        memcpy(rOut.distance, rLight.distance, sizeof(rOut.distance));
        memcpy(rOut.position, rLight.position, sizeof(rOut.position));
        memcpy(rOut.direction, rLight.direction, sizeof(rOut.direction));
        sLightsDirty = true;
        sLightsSet = true;
    }

    void worldLight(const NebulaFrameWorldLight& rLight) {
        Store* pStore = store();
        Grown< NebulaFrameWorldLight >& rLights = pStore->mWorldLights;
        // Ascending by id, each id once: the newest values of a light named twice stand.
        u32 at = rLights.mCount;
        while (at != 0 && rLights.mData[at - 1].id > rLight.id) {
            at--;
        }
        if (at != 0 && rLights.mData[at - 1].id == rLight.id) {
            rLights.mData[at - 1] = rLight;
            return;
        }
        rLights.add();
        for (u32 i = rLights.mCount - 1; i > at; i--) {
            rLights.mData[i] = rLights.mData[i - 1];
        }
        rLights.mData[at] = rLight;
    }

    void shadowDrop(const NebulaFrameShadowDrop& rDrop) {
        Grown< NebulaFrameShadowDrop >& rDrops = store()->mShadowDrops;
        rDrops.add();
        rDrops.mData[rDrops.mCount - 1] = rDrop;
    }

    void pointLight(const NebulaFramePointLight& rLight) {
        Grown< NebulaFramePointLight >& rLights = store()->mPointLights;
        rLights.add();
        rLights.mData[rLights.mCount - 1] = rLight;
    }

    void setLightOwner(const NebulaFrameWorldLight* pKey, const NebulaFrameWorldLight* pFill, const f32* pAmbient) {
        NebulaFrameLightOwner owner = sLightOwner;
        owner.actor_light = pKey != nullptr ? pKey->id : 0;
        for (u32 c = 0; c < 3; c++) {
            owner.ambient[c] = pAmbient != nullptr ? pAmbient[c] : 0.0f;
        }
        if (pKey != nullptr) {
            sOwnerLights[0] = *pKey;
            sOwnerLights[1] = *pFill;
        }
        if (memcmp(&owner, &sLightOwner, sizeof(owner)) != 0) {
            sLightOwner = owner;
            sLightsDirty = true;
        }
    }

    void setLightPointOwner(const NebulaFrameWorldLight* pPoint) {
        const u64 id = pPoint != nullptr ? pPoint->id : 0;
        if (pPoint != nullptr) {
            sOwnerPoint = *pPoint;
        }
        if (sLightOwner.point_light != id) {
            sLightOwner.point_light = id;
            sLightsDirty = true;
        }
    }

    void setStageLight(u64 id, const f32* pAmbient) {
        sStageLight = id;
        for (u32 c = 0; c < 3; c++) {
            sStageAmbient[c] = pAmbient[c];
        }
        sStageLightKnown = true;
    }

    // Lights 0-31 at once.
    void setLightFollowsCamera(u32 mask) {
        growLightFollow(1);
        if (sLightFollow.mData[0] != mask) {
            sLightFollow.mData[0] = mask;
            sLightsDirty = true;
        }
    }

    // Light `index`, whatever its number.
    void setLightFollows(u32 index, bool follows) {
        growLightFollow(index / 32u + 1u);
        u32& rWord = sLightFollow.mData[index / 32u];
        const u32 next = follows ? (rWord | 1u << (index % 32u)) : (rWord & ~(1u << (index % 32u)));
        if (rWord != next) {
            rWord = next;
            sLightsDirty = true;
        }
    }

    u32 finishState(u32 state) {
        if ((state & NEBULA_STATE_ALPHA_PASS) != 0) {
            state &= ~NEBULA_STATE_ALPHA_PASS;
            if (sPassAlpha) {
                state |= NEBULA_STATE_ALPHA_WRITE;
            }
        }
        if ((state & NEBULA_STATE_STENCIL_OWN) != 0) {
            state &= ~NEBULA_STATE_STENCIL_OWN;
        } else {
            state &= ~NEBULA_STATE_STENCIL(3, 3);
            state |= NEBULA_STATE_STENCIL(sPassStencilTest, sPassStencilOp);
        }
        return state;
    }

    void setPassAlpha(bool write) {
        sPassAlpha = write;
    }

    void setPassStencil(u8 test, u8 op) {
        sPassStencilTest = test;
        sPassStencilOp = op;
    }

    static MR::GrowBuffer< NebulaLayoutInput > sLayoutRooms[cLayoutRooms];

    NebulaLayoutInput* layoutRoom(u32 which, u32 count) {
        return sLayoutRooms[which].room(count);
    }

    u32 sitePipeline(u32 shader, u32 state, const NebulaLayoutInput* pInputs, u32 inputCount, u32 stride, bool wide) {
        u64 key = static_cast< u64 >(shader) << 1 | (wide ? 1 : 0);
        key = key << 16 | stride;
        for (u32 i = 0; i < inputCount; i++) {
            key = (key ^ pInputs[i].semantic ^ static_cast< u64 >(pInputs[i].offset) << 8) * 0x100000001B3ull;
        }
        SitePipeline& rEntry = siteEntry(key, state);
        if (rEntry.mPipeline != 0) { return rEntry.mPipeline; }
        {
                NebulaPipelineDesc desc;
                memset(&desc, 0, sizeof(desc));
                desc.vertex = NEBULA_PROGRAM_SITE | static_cast< u64 >(shader) << 1 | (wide ? 1 : 0);
                desc.pixel = NEBULA_PROGRAM_SITE | static_cast< u64 >(shader) << 1;
                desc.state = state;
                desc.position_stride = static_cast< u32 >(stride);
                desc.input_count = static_cast< u32 >(inputCount);
                rEntry.mKey = key;
                rEntry.mState = state;
                rEntry.mPipeline = host()->pipeline_register(host()->user, &desc, pInputs);
                return rEntry.mPipeline;
        }
    }

    // An instanced draw's pipeline: `vertexShader` builds the corners from the instance rows, `pixelShader` colours them.
    u32 instancePipeline(u32 vertexShader, u32 pixelShader, u32 instanceBytes, u32 state) {
        const u64 key = static_cast< u64 >(vertexShader) << 1 << 16;
        SitePipeline& rEntry = siteEntry(key, state);
        if (rEntry.mPipeline != 0) {
            return rEntry.mPipeline;
        }
        NebulaPipelineDesc desc;
        memset(&desc, 0, sizeof(desc));
        desc.vertex = NEBULA_PROGRAM_SITE | static_cast< u64 >(vertexShader) << 1;
        desc.pixel = NEBULA_PROGRAM_SITE | static_cast< u64 >(pixelShader) << 1;
        desc.state = state;
        // No vertex streams: the stride only keeps the layout from naming a matrix stream.
        desc.position_stride = instanceBytes;
        rEntry.mKey = key;
        rEntry.mState = state;
        rEntry.mPipeline = host()->pipeline_register(host()->user, &desc, nullptr);
        return rEntry.mPipeline;
    }

    u32 particlePipeline(u32 state) {
        return instancePipeline(NEBULA_SHADER_PARTICLE_INSTANCES, NEBULA_SHADER_PARTICLE, sizeof(NebulaParticleInstance), state);
    }

    u32 glyphPipeline(u32 state) {
        return instancePipeline(NEBULA_SHADER_GLYPH_INSTANCES, NEBULA_SHADER_FONT_TEXT, sizeof(NebulaGlyphInstance), state);
    }

    u32 sitePipeline(u32 shader, u32 state, const NebulaModelMesh* pMesh) {
        const u64 key = (meshLayoutKey(*pMesh) * 0x9E3779B97F4A7C15ull + shader) ^ 0x4000000000000000ull;
        SitePipeline& rEntry = siteEntry(key, state);
        if (rEntry.mPipeline != 0) { return rEntry.mPipeline; }
        {
                NebulaPipelineDesc desc;
                memset(&desc, 0, sizeof(desc));
                desc.vertex = NEBULA_PROGRAM_SITE | static_cast< u64 >(shader) << 1;
                desc.pixel = NEBULA_PROGRAM_SITE | static_cast< u64 >(shader) << 1;
                desc.state = state;
                static const u8 cSemantics[] = {NEBULA_INPUT_NORMAL, NEBULA_INPUT_COLOR0, NEBULA_INPUT_COLOR1, NEBULA_INPUT_TANGENT, NEBULA_INPUT_BINORMAL};
                NebulaLayoutInput* pRoom = layoutRoom(cLayoutRoomBuild, static_cast< u32 >(sizeof(cSemantics)) + NEBULA_MATERIAL_SLOTS);
                for (u8 semantic : cSemantics) {
                    meshLayoutInput(&desc, pRoom, semantic, *pMesh);
                }
                for (u16 coord = 0; coord < NEBULA_MATERIAL_SLOTS; coord++) {
                    meshLayoutInput(&desc, pRoom, static_cast< u16 >(NEBULA_INPUT_COORD0 + coord), *pMesh);
                }
                rEntry.mKey = key;
                rEntry.mState = state;
                rEntry.mPipeline = host()->pipeline_register(host()->user, &desc, pRoom);
                return rEntry.mPipeline;
        }
    }

    u32 formPipeline(u64 vertex, u64 pixel, const u8* pFormInputs, u32 formInputCount, u32 state, const NebulaLayoutInput* pInputs, u32 inputCount, u32 stride) {
        u64 key = (vertex ^ pixel * 0x9E3779B97F4A7C15ull) + stride;
        for (u32 i = 0; i < inputCount; i++) {
            key = (key ^ pInputs[i].semantic ^ static_cast< u64 >(pInputs[i].offset) << 8) * 0x100000001B3ull;
        }
        for (u32 f = 0; f < formInputCount; f++) {
            key = (key ^ pFormInputs[f]) * 0x100000001B3ull;
        }
        key |= 1;
        SitePipeline& rEntry = siteEntry(key, state);
        if (rEntry.mPipeline != 0) { return rEntry.mPipeline; }
        {
                NebulaPipelineDesc desc;
                memset(&desc, 0, sizeof(desc));
                desc.vertex = vertex;
                desc.pixel = pixel;
                desc.state = state;
                desc.position_stride = static_cast< u32 >(stride);
                u16 programCoord = 0;
                NebulaLayoutInput* pRoom = layoutRoom(cLayoutRoomBuild, formInputCount);
                for (u32 f = 0; f < formInputCount; f++) {
                    NebulaLayoutInput& rInput = pRoom[desc.input_count++];
                    const u8 semantic = pFormInputs[f];
                    rInput.semantic = semantic;
                    rInput.stream = NEBULA_STREAM_ZERO;
                    rInput.format = semantic >= NEBULA_INPUT_COORD0 ? 1 : semantic == NEBULA_INPUT_COLOR0 || semantic == NEBULA_INPUT_COLOR1 ? 2 : 0;
                    for (u32 n = 0; n < inputCount; n++) {
                        if (pInputs[n].semantic == semantic) {
                            rInput = pInputs[n];
                            break;
                        }
                    }
                    if (semantic >= NEBULA_INPUT_COORD0) {
                        rInput.semantic = static_cast< u16 >(NEBULA_INPUT_COORD0 + programCoord++);
                    }
                }
                rEntry.mKey = key;
                rEntry.mState = state;
                rEntry.mPipeline = host()->pipeline_register(host()->user, &desc, pRoom);
                return rEntry.mPipeline;
        }
    }

    bool pixelLit() {
        return sPixelLit;
    }

    void notDrawn(const ModelData* pData, u32 material, u32 replace, u32 pass) {
        MR::FormattedName text("not drawn: %s has no form replacing %u pass %u", pData->getMaterialName(static_cast< u32 >(material)),
                        replace, pass);
        host()->log(host()->user, text.c_str());
    }

    void setObject(u32 slot, u32 generation) {
        sSlot = slot;
        sGeneration = generation;
        sPart = 0;
    }

    u32 objectSlot() {
        return sSlot;
    }

    u32 objectGeneration() {
        return sGeneration;
    }

    u32 nextPart() {
        return sPart++;
    }

    void setPart(u32 part) {
        sPart = part;
    }

    u32 newSlot() {
        return sNextSlot++;
    }

    void targetBegin(const void* pKey, f32 across, f32 down, u32 clearRgba) {
        sTarget = RenderTexture::idOf(pKey);
        sLayer = NEBULA_LAYER_TARGET;
        NebulaFramePass& rPass = addPass(NEBULA_PASS_DRAW);
        rPass.first_item = store()->mItems.mCount;
        rPass.load = 1;
        rPass.clear_rgba = clearRgba;
        rPass.width = static_cast< u32 >(across * NEBULA_FRAME_UNITS);
        rPass.height = static_cast< u32 >(down * NEBULA_FRAME_UNITS);
        sOpenDraw = static_cast< s32 >(store()->mPasses.mCount - 1);
    }

    void targetBeginPixels(const void* pKey, u32 width, u32 height, u32 clearRgba) {
        targetBegin(pKey, 0.0f, 0.0f, clearRgba);
        NebulaFramePass& rPass = store()->mPasses.mData[sOpenDraw];
        rPass.width = width;
        rPass.height = height;
        rPass.pixels = 1;
    }

    void targetBeginOverFrame(const void* pKey, f32 across, f32 down, u32 clearRgba, const void* pFrameKey,
                              NebulaTargetChannels channels, const void* pDepthKey) {
        targetBegin(pKey, across, down, clearRgba);
        NebulaFramePass& rPass = store()->mPasses.mData[sOpenDraw];
        rPass.over_frame_texture = RenderTexture::idOf(pFrameKey);
        rPass.over_frame_depth = RenderTexture::idOf(pDepthKey);
        rPass.over_frame_channels = channels;
    }

    void targetEnd(const void*, NebulaTargetChannels channels) {
        Store* pStore = store();
        // Every pass into the target carries what its picture holds.
        for (u32 i = pStore->mPasses.mCount; i != 0; i--) {
            NebulaFramePass& rPass = pStore->mPasses.mData[i - 1];
            if (rPass.target != sTarget) {
                break;
            }
            rPass.channels = channels;
        }
        sOpenDraw = -1;
        sTarget = 0;
        sLayer = sMainLayer;
    }

    void clear(u32 clearRgba, u32 channels) {
        NebulaFramePass& rPass = addPass(NEBULA_PASS_CLEAR);
        rPass.clear_rgba = clearRgba;
        rPass.clear_channels = static_cast< u8 >(channels);
    }

    void clearFrame(u32 clearRgba, u32 channels) {
        clear(clearRgba, channels);
    }

    void registerPipelineList() {
        GameAssets::File file;
        if (!GameAssets::open("/NebulaData/Pipelines.bin", &file)) {
            return;
        }
        // The file's format: nebula_frame_list.h NEBULA_PIPELINE_FILE_MAGIC. Records follow each other unaligned.
        const u8* pBytes = static_cast< const u8* >(GameAssets::map(file.mEntry));
        const u32 length = static_cast< u32 >(file.mLength);
        if (length < NEBULA_PIPELINE_FILE_HEADER_BYTES || memcmp(pBytes, NEBULA_PIPELINE_FILE_MAGIC, 8) != 0) {
            return;
        }
        u32 count;
        memcpy(&count, pBytes + 8, sizeof(count));
        u32 at = NEBULA_PIPELINE_FILE_HEADER_BYTES;
        const NebulaHostServices* pHost = nebula_host();
        for (u32 i = 0; i < count; i++) {
            if (length - at < NEBULA_PIPELINE_RECORD_BYTES) {
                return;
            }
            NebulaPipelineDesc desc;
            memcpy(&desc.vertex, pBytes + at, 8);
            memcpy(&desc.pixel, pBytes + at + 8, 8);
            memcpy(&desc.state, pBytes + at + 16, 4);
            memcpy(&desc.input_count, pBytes + at + 20, 4);
            memcpy(&desc.position_stride, pBytes + at + 24, 4);
            at += NEBULA_PIPELINE_RECORD_BYTES;
            if ((length - at) / NEBULA_LAYOUT_INPUT_BYTES < desc.input_count) {
                return;
            }
            NebulaLayoutInput* pInputs = layoutRoom(cLayoutRoomBuild, desc.input_count);
            memcpy(pInputs, pBytes + at, desc.input_count * sizeof(NebulaLayoutInput));
            at += desc.input_count * NEBULA_LAYOUT_INPUT_BYTES;
            pHost->pipeline_register(pHost->user, &desc, pInputs);
            if (desc.pixel == (NEBULA_PROGRAM_SITE | static_cast< u64 >(NEBULA_SHADER_PARTICLE) << 1)) {
                // Dynamic emitters choose this form only after proving their alpha test cannot reject. Prepare both
                // forms with the existing particle state list, before play, so the optimization adds no first-use hitch.
                desc.state |= NEBULA_STATE_PIXEL_SHADING;
                pHost->pipeline_register(pHost->user, &desc, pInputs);
            }
        }
    }

    void copyFrame(const void* pKey) {
        const u32 texture = RenderTexture::idOf(pKey);
        NebulaFramePass& rPass = addPass(NEBULA_PASS_COPY_FRAME);
        rPass.target = texture;
    }

    void takeFrame(const void* pKey) {
        const u32 texture = RenderTexture::idOf(pKey);
        NebulaFramePass& rPass = addPass(NEBULA_PASS_TAKE_FRAME);
        rPass.target = texture;
    }

    void show() {
        addPass(NEBULA_PASS_SHOW);
    }

    u32 newProbeAsker() {
        sProbeAskers = sProbeAskers == cLastAsker ? 1u : sProbeAskers + 1u;
        return sProbeAskers;
    }

    void probeScreen(u32 channel, u32 asker, const NebulaScreenProbe* pProbes, u32 count, bool compared) {
        if (count == 0) {
            return;
        }
        Store* pStore = store();
        const u32 first = pStore->mProbes.mCount;
        for (u32 i = 0; i < count; i++) {
            NebulaScreenProbe& rProbe = pStore->mProbes.add();
            rProbe = pProbes[i];
            if (rProbe.x >= 0.0f) {
                rProbe.x *= NEBULA_FRAME_UNITS;
                rProbe.y *= NEBULA_FRAME_UNITS;
            }
        }
        NebulaScreenProbe& rMark = pStore->mProbes.add();
        rMark = NebulaScreenProbe{-1.0f, -1.0f, static_cast< f32 >(asker), 0.0f, 0.0f, 0.0f};
        NebulaFramePass& rPass = addPass(NEBULA_PASS_PROBE);
        rPass.probe_channel = channel;
        rPass.arg0 = compared ? NEBULA_PROBE_COMPARED : NEBULA_PROBE_DEPTH;
        rPass.first_probe = first;
        rPass.probe_count = count + 1;
    }

    // The channel's latest answers if they answer this asker's probes; none before its first have come back.
    u32 screenProbeResults(u32 channel, u32 asker, NebulaScreenProbe* pProbes, u32 capacity) {
        if (sProbeAnswers.mRoom < capacity + 1) {
            sProbeAnswers.reserve(capacity + 1);
        }
        const u32 done = host()->probe_results(host()->user, channel, sProbeAnswers.mData, capacity + 1);
        if (done == 0 || sProbeAnswers.mData[done - 1].user_x != static_cast< f32 >(asker)) {
            return 0;
        }
        memcpy(pProbes, sProbeAnswers.mData, (done - 1) * sizeof(NebulaScreenProbe));
        return done - 1;
    }

    void sceneBegin(const f32 (*pView)[4]) {
        Store* pStore = store();
        pStore->mList.flags |= NEBULA_FRAME_HAS_SCENE;
        NebulaFramePass& rPass = addPass(NEBULA_PASS_SCENE_BEGIN);
        sMainLayer = NEBULA_LAYER_SCENE;
        sLayer = NEBULA_LAYER_SCENE;
        sOpenDraw = -1;
        rPass.scene_camera = NEBULA_NONE32;
        rPass.scene_view = NEBULA_NONE32;
        if (pView != nullptr) {
            sSceneCamera = camera(pView, NEBULA_CAMERA_SCENE);
            sSceneView = currentView();
            rPass.scene_camera = sSceneCamera;
            rPass.scene_view = sSceneView;
            if (pView[3][3] != 0.0f) {
                // The player's up in the eye space of the scene's view.
                const f32 x = pView[0][0] * pView[3][0] + pView[0][1] * pView[3][1] + pView[0][2] * pView[3][2];
                const f32 y = pView[1][0] * pView[3][0] + pView[1][1] * pView[3][1] + pView[1][2] * pView[3][2];
                const f32 z = pView[2][0] * pView[3][0] + pView[2][1] * pView[3][1] + pView[2][2] * pView[3][2];
                const f32 squared = x * x + y * y + z * z;
                if (squared > 0.0f) {
                    const f32 scale = 1.0f / __builtin_sqrtf(squared);
                    pStore->mList.sky_up[0] = x * scale;
                    pStore->mList.sky_up[1] = y * scale;
                    pStore->mList.sky_up[2] = z * scale;
                    pStore->mList.sky_up_known = 1;
                }
            }
        }
    }

    void opaqueEnd(const f32 (*pView)[4]) {
        NebulaFramePass& rPass = addPass(NEBULA_PASS_OPAQUE_END);
        if (pView != nullptr) {
            rPass.scene_camera = camera(pView, NEBULA_CAMERA_SCENE);
            rPass.scene_view = currentView();
        } else {
            rPass.scene_camera = sSceneCamera;
            rPass.scene_view = sSceneView;
        }
    }

    void sceneEnd() {
        NebulaFramePass& rPass = addPass(NEBULA_PASS_SCENE_END);
        rPass.scene_camera = sSceneCamera;
        rPass.scene_view = sSceneView;
        sMainLayer = NEBULA_LAYER_SCREEN;
        sLayer = NEBULA_LAYER_SCREEN;
    }

    void scene2D(bool) {
        sOpenDraw = -1;
        setObject(0, 0);
    }

    void mirror(bool begin) {
        sMainLayer = begin ? NEBULA_LAYER_MIRROR : NEBULA_LAYER_SCENE;
        if (sTarget == 0) {
            sLayer = sMainLayer;
        }
        sOpenDraw = -1;
        setObject(0, 0);
    }

    // A phase's draws made outside any object (the silhouette fill, the mirror's fill) start from no object, so their
    // numbers do not depend on how many draws the object drawn last made.
    void phase(u8 phase) {
        sPhase = phase;
        sOpenDraw = -1;
        setObject(0, 0);
    }

    void category(u8 category) {
        Store* pStore = store();
        Grown< u32 >& rRuns = pStore->mCategories;
        if (rRuns.mCount >= 2 && rRuns.mData[rRuns.mCount - 1] == category) {
            return;  // the run goes on
        }
        if (rRuns.mCount >= 2 && rRuns.mData[rRuns.mCount - 2] == pStore->mItems.mCount) {
            rRuns.mData[rRuns.mCount - 1] = category;  // the run before held no draw
            return;
        }
        rRuns.add() = pStore->mItems.mCount;
        rRuns.add() = category;
    }

    void bloom(u32 kind, u32 arg0, u32 arg1) {
        NebulaFramePass& rPass = addPass(static_cast< u8 >(kind));
        rPass.arg0 = arg0;
        rPass.arg1 = arg1;
    }

    void cameraCut(u32 cuts) {
        sCuts = cuts;
        sCutPending = true;
    }

}  // namespace Frame

namespace FrameRing {
    void* vertices(u32 bytes, u32* pAt) {
        Store* pStore = store();
        const u32 at = (pStore->mRingBytes + 15u) & ~15u;
        if (at + bytes > pStore->mRingRoom) {
            u32 room = pStore->mRingRoom != 0 ? pStore->mRingRoom * 2 : 1u << 20;
            while (at + bytes > room) {
                room *= 2;
            }
            pStore->mRingVertices = static_cast< u8* >(host()->heap_realloc(host()->user, pStore->mRingVertices, room));
            pStore->mRingRoom = room;
        }
        pStore->mRingBytes = at + bytes;
        *pAt = at;
        return pStore->mRingVertices + at;
    }

    u32* indices(u32 count, u32* pFirst) {
        Store* pStore = store();
        Grown< u32 >& rRing = pStore->mRingIndices;
        if (rRing.mCount + count > rRing.mRoom) {
            u32 room = rRing.mRoom != 0 ? rRing.mRoom * 2 : 1u << 16;
            while (rRing.mCount + count > room) {
                room *= 2;
            }
            rRing.reserve(room);
        }
        *pFirst = rRing.mCount;
        rRing.mCount += count;
        return rRing.mData + *pFirst;
    }

    void* instanceRows(u32 count, u32* pFirst) {
        Grown< InstanceRow >& rRows = store()->mInstanceRows;
        if (rRows.mCount + count > rRows.mRoom) {
            u32 room = rRows.mRoom != 0 ? rRows.mRoom : 1024;
            while (rRows.mCount + count > room) {
                room *= 2;
            }
            rRows.reserve(room);
        }
        *pFirst = rRows.mCount;
        rRows.mCount += count;
        return rRows.mData + *pFirst;
    }

    u32 instanceRowCount() {
        return store()->mInstanceRows.mCount;
    }

    NebulaParticleInstance& particle() {
        u32 first;
        return *static_cast< NebulaParticleInstance* >(instanceRows(sizeof(NebulaParticleInstance) / sizeof(InstanceRow), &first));
    }
}  // namespace FrameRing
