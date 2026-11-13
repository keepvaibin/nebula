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
#include "Game/System/LumaDrawLight.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/AreaObj/AreaForm.hpp"
#include "Game/AreaObj/LightArea.hpp"
#include "Game/AreaObj/LightAreaHolder.hpp"
#include "Game/Map/LightFunction.hpp"
#include "Game/Map/LightZoneDataHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"  // c95: MR::LightType
#include <nebula_module.h>

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
    const u32 cRequestGap = 2;
    // Every actor that asked: no limit.
    MR::GrowBuffer< Requester > sRequesters;
    u32 sRequesterCount;
    const LiveActor* sStarCore;
    u32 sFrame;
    // c95: the record type the next actorLight loads (MR::LightType; -1 unknown).
    s32 sActorType = -1;

    // The sun's world light has no owner record: one sun a scene, under this id (no object's address).
    const u64 cSunId = 1;

    f32 channel(u8 value) {
        return static_cast< f32 >(value) / 255.0f;
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

    // The stage's light for the tracer, held for the stage (c54; the director 11-11: no light follows the player).
    // sHeldPlanet is what the list carries: the held planet light, or where a scripted change's ease has got to.
    const s32 cScriptedSteps = 30;  // the game's own step for a change of light (getDefaultStepInterpolate)
    ActorLightInfo sHeldPlanet;
    ActorLightInfo sEaseFrom;
    const AreaLightInfo* sHeldArea;  // the area light held, or eased to
    bool sStageHeld;                 // sHeldPlanet was taken for this scene
    s32 sEaseStep;                   // the ease's step, -1 none
    s32 sLastZone;                   // the player's area id the frame before (-1 -1 no area)
    s32 sLastLight;
    Vec3f sLastPos;
    // His last places, newest at sPathAt - 1: an area id changes up to cLeaveSteps (8) frames after he crossed a box.
    const s32 cPathLength = 16;
    const s32 cPathLooked = 12;  // the places looked at for a crossing
    Vec3f sPath[cPathLength];
    s32 sPathAt;
    s32 sPathCount;

    // Whether a box of the area (zone, light) holds the place: its form alone, switched on or not.
    bool areaHolds(const LightDirector& rDirector, s32 zone, s32 light, const Vec3f& rPos) {
        LightAreaHolder* holder = rDirector.mAreaHolder;
        if (holder == nullptr || light < 0) {
            return false;
        }
        for (s32 i = 0; i < holder->getNumAreaObj(); i++) {
            const LightArea* area = static_cast< const LightArea* >(holder->getAreaObj(i));
            if (area != nullptr && area->mPlacedZoneID == zone && area->mObjArg0 == light && area->mForm != nullptr &&
                area->mForm->isInVolume(rPos)) {
                return true;
            }
        }
        return false;
    }

    void logStageLight(const char* pWhat, const AreaLightInfo* pArea) {
        if (NebulaHost::option(NEBULA_OPTION_RAY_TRACING) != 0) {
            nebula_log("[rt-inputs] stage light: %s %s (%s)\n", pWhat,
                       pArea != nullptr && pArea->mAreaLightName != nullptr ? pArea->mAreaLightName : "-", MR::getCurrentStageName());
        }
    }

    void holdStageLight(const LightDirector& rDirector, const AreaLightInfo& rArea) {
        const ActorLightCtrl* ctrl = rDirector.mPlayerLightCtrl;
        const bool placed = ctrl != nullptr && ctrl->mActor != nullptr;
        const s32 zone = placed ? ctrl->mLightID.mZoneId : -1;
        const s32 light = placed ? ctrl->mLightID.mLightID : -1;
        const Vec3f pos = placed ? ctrl->mActor->mPosition : sLastPos;
        if (!sStageHeld) {
            // The stage's default light: outside every area box of the start zone.
            sHeldArea = rDirector.mDefaultAreaLight != nullptr ? rDirector.mDefaultAreaLight : &rArea;
            sHeldPlanet = sHeldArea->mPlanetLight;
            sEaseStep = -1;
            sStageHeld = true;
            logStageLight("held, the stage's default", sHeldArea);
        } else if (placed && (zone != sLastZone || light != sLastLight)) {
            // Walked: over his last places he crossed the old area's box or the new one's (the change comes up to 8
            // frames after the crossing, and areas nest); scripted: he stayed on the same side of both, so an area was
            // switched on or off under him or a zone loaded around him.
            const bool inOld = areaHolds(rDirector, sLastZone, sLastLight, pos);
            const bool inNew = areaHolds(rDirector, zone, light, pos);
            bool scripted = true;
            for (s32 k = 1; k <= sPathCount && k <= cPathLooked && scripted; k++) {
                const Vec3f& rThen = sPath[(sPathAt - k + cPathLength) % cPathLength];
                scripted = areaHolds(rDirector, sLastZone, sLastLight, rThen) == inOld && areaHolds(rDirector, zone, light, rThen) == inNew;
            }
            if (scripted && &rArea != sHeldArea) {
                sEaseFrom = sHeldPlanet;
                sHeldArea = &rArea;
                sEaseStep = 0;
                logStageLight("scripted, easing to", sHeldArea);
            } else if (!scripted) {
                logStageLight("walked into another area, held", sHeldArea);
            }
        }
        if (sEaseStep >= 0) {
            const ActorLightInfo& rTo = sHeldArea->mPlanetLight;
            sEaseStep++;
            if (sEaseStep >= cScriptedSteps) {
                sHeldPlanet = rTo;
                sEaseStep = -1;
            } else {
                sHeldPlanet.mInfo0.mIsFollowCamera = rTo.mInfo0.mIsFollowCamera;
                sHeldPlanet.mInfo1.mIsFollowCamera = rTo.mInfo1.mIsFollowCamera;
                LightFunction::blendActorLightInfo(&sHeldPlanet, sEaseFrom, rTo, static_cast< f32 >(sEaseStep) / cScriptedSteps);
            }
        }
        if (placed) {
            sLastZone = zone;
            sLastLight = light;
            sLastPos = pos;
            sPath[sPathAt] = pos;
            sPathAt = (sPathAt + 1) % cPathLength;
            sPathCount = sPathCount < cPathLength ? sPathCount + 1 : cPathLength;
        }
    }
};  // namespace

namespace GameLightSignal {
    void sceneStart() {
        sRequesterCount = 0;
        sStarCore = nullptr;
        sStageHeld = false;  // the new stage's light is taken again (c54)
        sLastZone = -1;
        sLastLight = -1;
        sPathAt = 0;
        sPathCount = 0;
        // The scene before this one owned the lights loaded last: no set of the new scene is theirs.
        noLight();
    }

    void frame() {
        sFrame++;
        // The stage being played: the renderer's table of each stage's light reads it (rt_stages.tsv).
        Frame::setStage(MR::getCurrentStageName());
        // The light of the stage's own surfaces where the player is: the planet light of the player's area, else of the
        // stage's default area. Its two lights are in the list whether or not a draw loads them.
        if (MR::isExistSceneObj(SceneObj_LightDirector)) {
            const LightDirector* director = MR::getSceneObj< LightDirector >(SceneObj_LightDirector);
            const AreaLightInfo* area = director->mPlayerLightCtrl != nullptr && director->mPlayerLightCtrl->mAreaLightInf != nullptr
                                            ? director->mPlayerLightCtrl->mAreaLightInf
                                            : director->mDefaultAreaLight;
            if (area != nullptr) {
                // Held for the stage, not the area he stands in (c54).
                holdStageLight(*director, *area);
                const ActorLightInfo& rPlanet = sHeldPlanet;
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
        luma::draw::light::frame();  // the mods' lights (draw.light); one load while no mod has one
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
            const f32 k1 = function == LIGHT_FALLOFF_GENTLE ? q / reference : function == LIGHT_FALLOFF_MEDIUM ? 0.5f * q / reference : 0.0f;
            const f32 k2 = function == LIGHT_FALLOFF_STEEP    ? q / (reference * reference)
                           : function == LIGHT_FALLOFF_MEDIUM ? 0.5f * q / (reference * reference)
                                                      : 0.0f;
            half = k2 > 0.0f ? (-k1 + __builtin_sqrtf(k1 * k1 + 4.0f * k2)) / (2.0f * k2) : 1.0f / k1;
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
        const f32 clamped = brightness >= 0.95f ? (brightness <= 0.999999f ? brightness : 0.999999f) : 0.95f;
        const NebulaFrameWorldLight light =
            pointOf(NebulaHost::objectId(pActor), NEBULA_WORLD_LIGHT_REQUEST, rPosition.x, rPosition.y, rPosition.z, channel(color.r),
                    channel(color.g), channel(color.b), 15.0f * __builtin_sqrtf(clamped / (1.0f - clamped)));
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

    void actorLightType(s32 type) {
        sActorType = type;
    }

    void actorLight(const ActorLightInfo* pInfo) {
        const u64 id = NebulaHost::objectId(pInfo);
        const NebulaFrameWorldLight key = lightOf(id, NEBULA_WORLD_LIGHT_KEY, pInfo->mInfo0);
        const NebulaFrameWorldLight fill = lightOf(id + 1, NEBULA_WORLD_LIGHT_FILL, pInfo->mInfo1);
        const f32 ambient[3] = {channel(pInfo->mColor.r), channel(pInfo->mColor.g), channel(pInfo->mColor.b)};
        // c95: player, strong, weak, planet (MR::LightType 0 to 3) as NEBULA_LIGHT_OWNER_PLAYER to _PLANET.
        const u32 kind = sActorType >= MR::LightType_Player && sActorType <= MR::LightType_Planet
                             ? static_cast< u32 >(NEBULA_LIGHT_OWNER_PLAYER + sActorType)
                             : static_cast< u32 >(NEBULA_LIGHT_OWNER_NONE);
        sActorType = -1;
        Frame::setLightOwner(&key, &fill, ambient, kind);
    }

    void coinLight(const LightInfoCoin* pInfo) {
        // A coin's record holds one light of the world (light 0); its light 1 is none.
        const u64 id = NebulaHost::objectId(pInfo);
        const NebulaFrameWorldLight key = lightOf(id, NEBULA_WORLD_LIGHT_KEY, pInfo->base);
        NebulaFrameWorldLight fill = {};
        fill.id = id + 1;
        fill.kind = NEBULA_WORLD_LIGHT_FILL;
        Frame::setLightOwner(&key, &fill, nullptr, NEBULA_LIGHT_OWNER_COIN);
    }

    void noLight() {
        Frame::setLightOwner(nullptr, nullptr, nullptr, NEBULA_LIGHT_OWNER_NONE);
        Frame::setLightPointOwner(nullptr);
    }
};  // namespace GameLightSignal
