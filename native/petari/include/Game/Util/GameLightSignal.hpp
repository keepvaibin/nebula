#pragma once

#ifdef TARGET_PC
#include "Game/Util/Color.hpp"
#include <JSystem/JGeometry/TVec.hpp>
#include <revolution.h>

class LiveActor;
struct ActorLightInfo;
struct PointLightInfo;

// Nebula: the game's lights for the renderer's ray tracer (__NebulaGameLight, NEBULA_LIGHT_* in nebula_game_api.h),
// from the objects that own them rather than from the GX light registers.
namespace GameLightSignal {
    // LightDirector's construction: a new scene, before its actors are made.
    void sceneStart();
    // GameScene::draw3D's begin: the star core and the lights of clipped requesters (sent once a frame).
    void frame();

    // BrightSun::control: the visible sun at `rPosition`, placed from the camera.
    void sun(const TVec3f& rPosition);
    // AstroCore::init: the Comet Observatory's star core.
    void registerStarCore(const LiveActor* pCore);
    // LightFunction::loadPointLightInfo: the game's one point light (LightPointCtrl's current one).
    void pointLight(const PointLightInfo* pInfo);
    // MR::requestPointLight: every actor asking for that light, with its brightness at the reference distance.
    void pointRequest(const LiveActor* pActor, const TVec3f& rPosition, Color8 color, f32 brightness);
    // LightFunction::loadActorLightInfo: world-fixed lights far out (key lights of areas).
    void actorLight(const ActorLightInfo* pInfo);
};  // namespace GameLightSignal
#endif
