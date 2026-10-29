#ifdef TARGET_PC
#include "Game/Util/GameLightSignal.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include "Game/LiveActor/ActorLightCtrl.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Map/LightDataHolder.hpp"
#include "Game/Map/LightDirector.hpp"
#include "Game/Map/LightPointCtrl.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include <nebula_game_api.h>
#include <revolution/os.h>

namespace {
    // An actor that asked for the point light, with its last request (NEBULA_LIGHT_POINT_REQUEST values).
    struct Requester {
        const LiveActor* mActor;
        f32 mValues[7];
        u32 mFrame;
    };

    const u32 cRequesters = 64;
    // A requester silent for more frames than this keeps its light only while the game clips it: a clipped actor runs
    // no control, which is where it asks. One that died, or that the game runs but that stopped asking, lets its light
    // fade out.
    const u32 cRequestGap = 2;
    Requester sRequesters[cRequesters];
    u32 sRequesterCount;
    const LiveActor* sStarCore;
    u32 sFrame;
    // The ActorLightInfos already sent this frame: one is loaded for every draw buffer group that uses it.
    const ActorLightInfo* sWorldSent[16];
    u32 sWorldSentCount;
    u32 sWorldFrame;

    f32 channel(u8 value) {
        return static_cast< f32 >(value) / 255.0f;
    }

    // An ActorLightInfo as NEBULA_LIGHT_AREA and NEBULA_LIGHT_PLAYER values.
    void actorLightValues(u32 kind, const ActorLightInfo& rInfo) {
        f32 values[17];
        values[0] = channel(rInfo.mColor.r);
        values[1] = channel(rInfo.mColor.g);
        values[2] = channel(rInfo.mColor.b);
        const LightInfo* const lights[2] = {&rInfo.mInfo0, &rInfo.mInfo1};
        for (u32 i = 0; i < 2; i++) {
            f32* v = values + 3 + 7 * i;
            v[0] = channel(lights[i]->mColor.r);
            v[1] = channel(lights[i]->mColor.g);
            v[2] = channel(lights[i]->mColor.b);
            v[3] = lights[i]->mPos.x;
            v[4] = lights[i]->mPos.y;
            v[5] = lights[i]->mPos.z;
            v[6] = lights[i]->mIsFollowCamera ? 1.0f : 0.0f;
        }
        __NebulaGameLight(kind, 0, values);
    }

    // A LightInfo that does not follow the camera and sits this far out (20000 units) lights its area as the sun does.
    void worldLight(const LightInfo& rInfo) {
        const Vec& p = rInfo.mPos;
        if (rInfo.mIsFollowCamera || p.x * p.x + p.y * p.y + p.z * p.z <= 4.0e8f) {
            return;
        }
        const f32 values[6] = {p.x, p.y, p.z, channel(rInfo.mColor.r), channel(rInfo.mColor.g), channel(rInfo.mColor.b)};
        __NebulaGameLight(NEBULA_LIGHT_WORLD, 0, values);
    }
};  // namespace

namespace GameLightSignal {
    void sceneStart() {
        sRequesterCount = 0;
        sStarCore = nullptr;
        sWorldSentCount = 0;
    }

    void frame() {
        sFrame++;
        // The stage's planet light (what its map geometry is lit with) and the player's current light, blended as
        // the player crosses light areas.
        if (MR::isExistSceneObj(SceneObj_LightDirector)) {
            const LightDirector* director = MR::getSceneObj< LightDirector >(SceneObj_LightDirector);
            if (director->mDefaultAreaLight != nullptr) {
                actorLightValues(NEBULA_LIGHT_AREA, director->mDefaultAreaLight->mPlanetLight);
            }
            if (director->_1C != nullptr && director->_1C->mAreaLightInf != nullptr) {
                actorLightValues(NEBULA_LIGHT_PLAYER, *director->_1C->getActorLight());
            }
        }
        if (sStarCore != nullptr && !MR::isDead(sStarCore)) {
            const TVec3f& p = sStarCore->mPosition;
            const f32 values[4] = {p.x, p.y, p.z, sStarCore->mScale.x};
            __NebulaGameLight(NEBULA_LIGHT_STAR_CORE, 0, values);
        }
        for (u32 i = 0; i < sRequesterCount;) {
            Requester& r = sRequesters[i];
            if (sFrame - r.mFrame <= cRequestGap) {
                i++;
                continue;
            }
            if (MR::isDead(r.mActor) || !MR::isClipped(r.mActor)) {
                sRequesters[i] = sRequesters[--sRequesterCount];
                continue;
            }
            __NebulaGameLight(NEBULA_LIGHT_POINT_REQUEST, __NebulaObjectId(r.mActor), r.mValues);
            i++;
        }
    }

    void sun(const TVec3f& rPosition) {
        const TVec3f camera = MR::getCamPos();
        const f32 x = rPosition.x - camera.x;
        const f32 y = rPosition.y - camera.y;
        const f32 z = rPosition.z - camera.z;
        const f32 distance = __builtin_sqrtf(x * x + y * y + z * z);
        if (!(distance > 1e-3f)) {
            return;
        }
        const f32 values[4] = {x / distance, y / distance, z / distance, distance};
        __NebulaGameLight(NEBULA_LIGHT_SUN, 0, values);
    }

    void registerStarCore(const LiveActor* pCore) {
        sStarCore = pCore;
    }

    // GXInitLightDistAttn's falloff, 1 / (1 + k1 d + k2 d^2): the distance where the light halves.
    void pointLight(const PointLightInfo* pInfo) {
        const f32 reference = pInfo->mRefDistance;
        const f32 brightness = pInfo->mRefBrightness;
        const GXDistAttnFn function = pInfo->mDistAttnFn;
        f32 half = 0.0f;
        if (reference > 0.0f && brightness > 0.0f && brightness < 1.0f && function != GX_DA_OFF) {
            const f32 q = (1.0f - brightness) / brightness;
            const f32 k1 = function == GX_DA_GENTLE ? q / reference : function == GX_DA_MEDIUM ? 0.5f * q / reference : 0.0f;
            const f32 k2 = function == GX_DA_STEEP    ? q / (reference * reference)
                           : function == GX_DA_MEDIUM ? 0.5f * q / (reference * reference)
                                                      : 0.0f;
            half = k2 > 0.0f ? (-k1 + __builtin_sqrtf(k1 * k1 + 4.0f * k2)) / (2.0f * k2) : 1.0f / k1;
        }
        const Vec& p = pInfo->mPos;
        const GXColor& c = pInfo->mColor;
        const f32 values[7] = {p.x, p.y, p.z, channel(c.r), channel(c.g), channel(c.b), half};
        __NebulaGameLight(NEBULA_LIGHT_POINT, __NebulaObjectId(pInfo), values);
    }

    // LightPointCtrl blends its light with J3DNativeState::initLightDistAttn(15, brightness, GX_DA_GENTLE): it halves at
    // 15 sqrt(b / (1 - b)).
    void pointRequest(const LiveActor* pActor, const TVec3f& rPosition, Color8 color, f32 brightness) {
        const f32 b = brightness >= 0.95f ? (brightness <= 0.999999f ? brightness : 0.999999f) : 0.95f;
        const f32 values[7] = {rPosition.x, rPosition.y,  rPosition.z, channel(color.r), channel(color.g), channel(color.b),
                               15.0f * __builtin_sqrtf(b / (1.0f - b))};
        __NebulaGameLight(NEBULA_LIGHT_POINT_REQUEST, __NebulaObjectId(pActor), values);
        Requester* slot = nullptr;
        for (u32 i = 0; i < sRequesterCount && slot == nullptr; i++) {
            if (sRequesters[i].mActor == pActor) {
                slot = &sRequesters[i];
            }
        }
        if (slot == nullptr) {
            if (sRequesterCount < cRequesters) {
                slot = &sRequesters[sRequesterCount++];
            } else {
                slot = &sRequesters[0];
                for (u32 i = 1; i < cRequesters; i++) {
                    if (sRequesters[i].mFrame < slot->mFrame) {
                        slot = &sRequesters[i];
                    }
                }
            }
        }
        slot->mActor = pActor;
        for (u32 i = 0; i < 7; i++) {
            slot->mValues[i] = values[i];
        }
        slot->mFrame = sFrame;
    }

    void actorLight(const ActorLightInfo* pInfo) {
        if (sWorldFrame != sFrame) {
            sWorldFrame = sFrame;
            sWorldSentCount = 0;
        }
        for (u32 i = 0; i < sWorldSentCount; i++) {
            if (sWorldSent[i] == pInfo) {
                return;
            }
        }
        if (sWorldSentCount < 16) {
            sWorldSent[sWorldSentCount++] = pInfo;
        }
        worldLight(pInfo->mInfo0);
        worldLight(pInfo->mInfo1);
    }
};  // namespace GameLightSignal
#endif
