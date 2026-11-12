#pragma once

#include "Game/Util/Color.hpp"
#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class LiveActor;
struct ActorLightInfo;
struct LightInfoCoin;
struct PointLightInfo;

// Nebula: the game's lights as lights of the world (NebulaFrameWorldLight, nebula_frame_list.h), written into the
// frame's list by the objects that own them, each under its owner's id.
namespace GameLightSignal {
    // LightDirector's construction: a new scene, before its actors are made.
    void sceneStart();
    // GameScene::draw3D's begin: the stage's light where the player is, the star core and the lights of clipped
    // requesters (once a frame).
    void frame();

    // BrightSun::control: the visible sun at `rPosition`, placed from the camera.
    void sun(const Vec3f& rPosition);
    // AstroCore::init: the Comet Observatory's star core.
    void registerStarCore(const LiveActor* pCore);
    // LightFunction::loadPointLightInfo: the game's one point light (LightPointCtrl's current one), light 4 of the sets
    // made from now on.
    void pointLight(const PointLightInfo* pInfo);
    // MR::requestPointLight: every actor asking for that light, with its brightness at the reference distance.
    void pointRequest(const LiveActor* pActor, const Vec3f& rPosition, Color8 color, f32 brightness);
    // c95: which of an area's records (MR::LightType: player, strong, weak, planet) the next actorLight loads; said by
    // its caller (ActorLightCtrl::loadLight, MR::loadLight) just before, forgotten after it.
    void actorLightType(s32 type);
    // LightFunction::loadActorLightInfo: the record whose key and fill are lights 0 and 1 of the sets made from now on.
    void actorLight(const ActorLightInfo* pInfo);
    // LightFunction::loadLightInfoCoin: the coins' record, whose one light is light 0.
    void coinLight(const LightInfoCoin* pInfo);
    // LightFunction::loadAllLightWhite: the sets made from now on hold no light of the world.
    void noLight();
};  // namespace GameLightSignal
