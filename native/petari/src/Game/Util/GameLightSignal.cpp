#include "Game/Util/GameLightSignal.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/LiveActor/ActorLightCtrl.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Map/LightDataHolder.hpp"
#include "Game/Map/LightDirector.hpp"
#include "Game/Map/LightPointCtrl.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include <nebula_frame_list.h>
#include "Game/System/NebulaHost.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_channel_f1, "channel.f1", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_pointLight_f1, "pointLight.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_pointLight_f2, "pointLight.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_pointLight_f3, "pointLight.f3", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_pointLight_f4, "pointLight.f4", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_pointRequest_f1, "pointRequest.f1", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_pointRequest_f2, "pointRequest.f2", 0.999999f);
NEBULA_TUNABLE_NAMED(f32, cNum_pointRequest_f3, "pointRequest.f3", 0.999999f);
NEBULA_TUNABLE_NAMED(f32, cNum_pointRequest_f4, "pointRequest.f4", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_pointRequest_f5, "pointRequest.f5", 15.0f);

namespace {
    // An actor that asked for the point light, with its last request as a world light.
    struct Requester {
        const LiveActor* mActor;
        NebulaFrameWorldLight mLight;
        u32 mFrame;
    };

    // A requester silent for more frames than this keeps its light only while the game clips it: a clipped actor runs
    // no control, which is where it asks. One that died, or that the game runs but that stopped asking, lets its light
    // fade out.
    NEBULA_TUNABLE(u32, cRequestGap, 2);
    // Every actor that asked: no limit.
    MR::GrowBuffer< Requester > sRequesters;
    u32 sRequesterCount;
    const LiveActor* sStarCore;
    u32 sFrame;

    // The sun's world light has no owner record: one sun a scene, under this id (no object's address).
    const u64 cSunId = 1;

    f32 channel(u8 value) {
        return static_cast< f32 >(value) / cNum_channel_f1;
    }

    // One light of an area's light record: its colour, and its place in the world or, when it follows the camera, in
    // the camera's eye space.
    NebulaFrameWorldLight lightOf(u64 id, u32 kind, const LightInfo& rInfo) {
        NebulaFrameWorldLight light = {};
        light.id = id;
        light.kind = kind;
        light.flags = rInfo.mIsFollowCamera ? static_cast< u32 >(NEBULA_WORLD_LIGHT_FOLLOWS_CAMERA) : 0u;
        light.color[0] = channel(rInfo.mColor.r);
        light.color[1] = channel(rInfo.mColor.g);
        light.color[2] = channel(rInfo.mColor.b);
        light.position[0] = rInfo.mPos.x;
        light.position[1] = rInfo.mPos.y;
        light.position[2] = rInfo.mPos.z;
        return light;
    }

    NebulaFrameWorldLight pointOf(u64 id, u32 kind, f32 x, f32 y, f32 z, f32 r, f32 g, f32 b, f32 reach) {
        NebulaFrameWorldLight light = {};
        light.id = id;
        light.kind = kind;
        light.color[0] = r;
        light.color[1] = g;
        light.color[2] = b;
        light.reach = reach;
        light.position[0] = x;
        light.position[1] = y;
        light.position[2] = z;
        return light;
    }
};  // namespace

namespace GameLightSignal {
    void sceneStart() {
        sRequesterCount = 0;
        sStarCore = nullptr;
        // The scene before this one owned the lights loaded last: no set of the new scene is theirs.
        noLight();
    }

    void frame() {
        sFrame++;
        // The light of the stage's own surfaces where the player is: the planet light of the player's area, else of the
        // stage's default area. Its two lights are in the list whether or not a draw loads them.
        if (MR::isExistSceneObj(SceneObj_LightDirector)) {
            const LightDirector* director = MR::getSceneObj< LightDirector >(SceneObj_LightDirector);
            const AreaLightInfo* area = director->mPlayerLightCtrl != nullptr && director->mPlayerLightCtrl->mAreaLightInf != nullptr
                                            ? director->mPlayerLightCtrl->mAreaLightInf
                                            : director->mDefaultAreaLight;
            if (area != nullptr) {
                const ActorLightInfo& rPlanet = area->mPlanetLight;
                const u64 id = NebulaHost::objectId(&rPlanet);
                Frame::worldLight(lightOf(id, NEBULA_WORLD_LIGHT_KEY, rPlanet.mInfo0));
                Frame::worldLight(lightOf(id + 1, NEBULA_WORLD_LIGHT_FILL, rPlanet.mInfo1));
                const f32 ambient[3] = {channel(rPlanet.mColor.r), channel(rPlanet.mColor.g), channel(rPlanet.mColor.b)};
                Frame::setStageLight(id, ambient);
            }
        }
        if (sStarCore != nullptr && !MR::isDead(sStarCore)) {
            const Vec3f& p = sStarCore->mPosition;
            Frame::worldLight(
                pointOf(NebulaHost::objectId(sStarCore), NEBULA_WORLD_LIGHT_STAR_CORE, p.x, p.y, p.z, 1.0f, 1.0f, 1.0f, sStarCore->mScale.x));
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
            Frame::worldLight(r.mLight);
            i++;
        }
    }

    void sun(const Vec3f& rPosition) {
        const Vec3f camera = MR::getCamPos();
        const f32 x = rPosition.x - camera.x;
        const f32 y = rPosition.y - camera.y;
        const f32 z = rPosition.z - camera.z;
        const f32 distance = __builtin_sqrtf(x * x + y * y + z * z);
        if (!(distance > 1e-3f)) {
            return;
        }
        Frame::worldLight(pointOf(cSunId, NEBULA_WORLD_LIGHT_SUN, x / distance, y / distance, z / distance, 1.0f, 1.0f, 1.0f, distance));
    }

    void registerStarCore(const LiveActor* pCore) {
        sStarCore = pCore;
    }

    // The light's falloff, 1 / (1 + k1 d + k2 d^2): the distance where the light halves.
    void pointLight(const PointLightInfo* pInfo) {
        const f32 reference = pInfo->mRefDistance;
        const f32 brightness = pInfo->mRefBrightness;
        const LightFalloff function = pInfo->mDistAttnFn;
        f32 half = 0.0f;
        if (reference > 0.0f && brightness > 0.0f && brightness < 1.0f && function != LIGHT_FALLOFF_NONE) {
            const f32 q = (1.0f - brightness) / brightness;
            const f32 k1 = function == LIGHT_FALLOFF_GENTLE ? q / reference : function == LIGHT_FALLOFF_MEDIUM ? cNum_pointLight_f1 * q / reference : 0.0f;
            const f32 k2 = function == LIGHT_FALLOFF_STEEP    ? q / (reference * reference)
                           : function == LIGHT_FALLOFF_MEDIUM ? cNum_pointLight_f2 * q / (reference * reference)
                                                      : 0.0f;
            half = k2 > 0.0f ? (-k1 + __builtin_sqrtf(k1 * k1 + cNum_pointLight_f3 * k2)) / (cNum_pointLight_f4 * k2) : 1.0f / k1;
        }
        const Vec& p = pInfo->mPos;
        const NebulaColor8& c = pInfo->mColor;
        const NebulaFrameWorldLight light =
            pointOf(NebulaHost::objectId(pInfo), NEBULA_WORLD_LIGHT_POINT, p.x, p.y, p.z, channel(c.r), channel(c.g), channel(c.b), half);
        Frame::worldLight(light);
        Frame::setLightPointOwner(&light);
    }

    // LightPointCtrl blends its light with LightValues::distAttn(15, brightness, LIGHT_FALLOFF_GENTLE): it halves at
    // 15 sqrt(b / (1 - b)).
    void pointRequest(const LiveActor* pActor, const Vec3f& rPosition, Color8 color, f32 brightness) {
        const f32 clamped = brightness >= cNum_pointRequest_f1 ? (brightness <= cNum_pointRequest_f2 ? brightness : cNum_pointRequest_f3) : cNum_pointRequest_f4;
        const NebulaFrameWorldLight light =
            pointOf(NebulaHost::objectId(pActor), NEBULA_WORLD_LIGHT_REQUEST, rPosition.x, rPosition.y, rPosition.z, channel(color.r),
                    channel(color.g), channel(color.b), cNum_pointRequest_f5 * __builtin_sqrtf(clamped / (1.0f - clamped)));
        Frame::worldLight(light);
        Requester* slot = nullptr;
        for (u32 i = 0; i < sRequesterCount && slot == nullptr; i++) {
            if (sRequesters[i].mActor == pActor) {
                slot = &sRequesters[i];
            }
        }
        if (slot == nullptr) {
            slot = &sRequesters.room(sRequesterCount + 1)[sRequesterCount++];
        }
        slot->mActor = pActor;
        slot->mLight = light;
        slot->mFrame = sFrame;
    }

    void actorLight(const ActorLightInfo* pInfo) {
        const u64 id = NebulaHost::objectId(pInfo);
        const NebulaFrameWorldLight key = lightOf(id, NEBULA_WORLD_LIGHT_KEY, pInfo->mInfo0);
        const NebulaFrameWorldLight fill = lightOf(id + 1, NEBULA_WORLD_LIGHT_FILL, pInfo->mInfo1);
        const f32 ambient[3] = {channel(pInfo->mColor.r), channel(pInfo->mColor.g), channel(pInfo->mColor.b)};
        Frame::setLightOwner(&key, &fill, ambient);
    }

    void coinLight(const LightInfoCoin* pInfo) {
        // A coin's record holds one light of the world (light 0); its light 1 is none.
        const u64 id = NebulaHost::objectId(pInfo);
        const NebulaFrameWorldLight key = lightOf(id, NEBULA_WORLD_LIGHT_KEY, pInfo->base);
        NebulaFrameWorldLight fill = {};
        fill.id = id + 1;
        fill.kind = NEBULA_WORLD_LIGHT_FILL;
        Frame::setLightOwner(&key, &fill, nullptr);
    }

    void noLight() {
        Frame::setLightOwner(nullptr, nullptr, nullptr);
        Frame::setLightPointOwner(nullptr);
    }
};  // namespace GameLightSignal
