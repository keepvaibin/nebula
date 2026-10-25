#pragma once

#ifdef TARGET_PC
#include <JSystem/JGeometry/TVec.hpp>
#include <nebula_gx_fifo.h>
#include <revolution/types.h>

class J3DMatPacket;
class J3DModel;
class J3DModelData;
class J3DShapePacket;
class JPABaseEmitter;
class JPABaseParticle;
class JPAResource;
class LayoutActor;
class LiveActor;
class NameObj;
struct ActorLightInfo;
struct LightInfoCoin;
struct PointLightInfo;

namespace nw4r {
    namespace lyt {
        class Pane;
    };  // namespace lyt
};  // namespace nw4r

// What the renderer needs to draw between two game frames (frame interpolation, motion vectors, temporal upscaling)
// and to know the scene (ray tracing): the object each draw belongs to, the draw phase it is in, when an object starts
// a new life or jumps, and when the camera cuts. The game writes them as host markers into the GX stream
// (galaxy/motion_signals.h), only while the renderer asks for them (__NebulaGXMarkers).
namespace MotionSignal {
    // SceneFunction's draw phases in GameScene::draw3D (galaxy::motion::ScenePhase).
    enum ScenePhase {
        ScenePhase_OpaBeforeVolumeShadow,
        ScenePhase_OpaBeforeSilhouette,
        ScenePhase_Opa,
        ScenePhase_Xlu,
        ScenePhase_ListOpa,
        ScenePhase_ListXlu,
        ScenePhase_SilhouetteAndFillShadow,
        ScenePhase_AlphaShadow,
        ScenePhase_AfterIndirect,
        ScenePhase_ImageEffect,
    };

    void drawShapePacketImpl(const J3DShapePacket* pPacket);
    void drawMaterialImpl(const J3DShapePacket* pPacket);
    // A model's resource name (ResourceHolder: the archive's .bdl or .bmd), for its materials' identities.
    void modelResource(const J3DModelData* pData, const char* pName);
    void drawNameObjImpl(const NameObj* pObj);
    void drawEmitterImpl(const JPAResource* pResource, const JPABaseEmitter* pEmitter, bool child);
    void drawParticleImpl(const JPABaseParticle* pParticle, bool child);
    void categoryImpl(u16 source, u32 type);
    void lightImpl(u16 site, const void* pInfo, u32 followCamera);
    void layoutImpl(u8 kind, u16 aux, const void* pObj, const void* pTag);

    // A J3D model's shape packet draws next (J3DShapePacket::prepareDraw).
    inline void drawShapePacket(const J3DShapePacket* pPacket) {
        if (__NebulaGXMarkers != 0) {
            drawShapePacketImpl(pPacket);
        }
    }

    // A J3D shape packet draws next (J3DShapePacket::prepareDraw, every drawer: J3DMatPacket::draw, the game's
    // DrawBufferShapeDrawer, MR::simpleDraw): its material's identity, the hash of "model/material" (the model's
    // resource name and the material's name, lower case), names the surface for the renderer's material table (the
    // ray tracer's light sources and materials, finer than their textures).
    inline void drawMaterial(const J3DShapePacket* pPacket) {
        if (__NebulaGXMarkers != 0) {
            drawMaterialImpl(pPacket);
        }
    }

    // A NameObj's own draw method runs next (the draw lists' delegator).
    inline void drawNameObj(const NameObj* pObj) {
        if (__NebulaGXMarkers != 0) {
            drawNameObjImpl(pObj);
        }
    }

    // An emitter's particles draw next (JPAResource::drawP, drawC for its children).
    inline void drawEmitter(const JPAResource* pResource, const JPABaseEmitter* pEmitter, bool child) {
        if (__NebulaGXMarkers != 0) {
            drawEmitterImpl(pResource, pEmitter, child);
        }
    }

    // One particle draws next (its own GXBegin).
    inline void drawParticle(const JPABaseParticle* pParticle, bool child) {
        if (__NebulaGXMarkers != 0) {
            drawParticleImpl(pParticle, child);
        }
    }

    // A particle of a stripe: its two vertices are written next, inside the stripe's one GXBegin (strip 0 JPADrawStripe,
    // 1 and 2 the passes of JPADrawStripeX).
    void stripeParticle(const JPABaseParticle* pParticle, u8 strip);

    // CategoryList::execute(MR::DrawType), drawOpa and drawXlu(MR::DrawBufferType).
    inline void drawType(u32 type) {
        if (__NebulaGXMarkers != 0) {
            categoryImpl(1, type);
        }
    }

    inline void drawBufferOpa(u32 type) {
        if (__NebulaGXMarkers != 0) {
            categoryImpl(2, type);
        }
    }

    inline void drawBufferXlu(u32 type) {
        if (__NebulaGXMarkers != 0) {
            categoryImpl(3, type);
        }
    }

    inline void phase(ScenePhase phase) {
        if (__NebulaGXMarkers != 0) {
            categoryImpl(4, phase);
        }
    }

    // GameScene::drawMirror: the mirror's draws begin (after isDrawMirror) and end (before its clearZBuffer).
    void mirror(bool begin);

    // Draws only the ray tracer's rays meet, never drawn to the picture (the player's body in the first-person view):
    // whether the renderer takes them now (it traces the scene and reads the markers that keep them out of the
    // picture; without both they would be drawn), and their bounds.
    bool traceOnlyWanted();
    void traceOnly(bool begin);

    // GX light loads (LightFunction, MR::loadLightPlayer).
    void actorLight(const ActorLightInfo* pInfo);
    void coinLight(const LightInfoCoin* pInfo);
    void pointLight(const PointLightInfo* pInfo);
    void allLightWhite();
    void playerLight();

    // The 2D layer: GameScene::draw2D's bounds, and the pane, layout actor or star pointer whose draws follow.
    void scene2D(bool begin);
    void layoutPane(const nw4r::lyt::Pane* pPane);
    void layoutActor(const LayoutActor* pActor);
    void starPointerLayout(const LayoutActor* pLayout);
    void starPointerDirector(const void* pDirector);

    // GameScene::draw3D's scene marker: the camera's world-to-eye rows, then the player's up (against its gravity) and
    // 1, or zeros without a player. The renderer's sky light comes from that up.
    void sceneCamera(f32 (*pOut)[4]);

    // After GameScene::draw3D's begin: the camera's cut count and the object events since the previous scene.
    void sceneBegin();

    // A new life: the actor was placed anew (MR::resetPosition) or died (LiveActor::makeActorDead). Its draws are drawn
    // at their own frame, never blended from the old life.
    void newLife(LiveActor* pActor);
    void died(LiveActor* pActor);
    // MarioAccess::setTrans and setBaseMtx: a jump far from where the player is starts a new life.
    void playerPlaced(LiveActor* pPlayer, const TVec3f& rTarget);
    // JPABaseEmitter::init: a pooled emitter starts a new effect.
    void emitterInit(JPABaseEmitter* pEmitter);
    // CameraViewInterpolator::reduceOscillation took its target without blending: a camera cut.
    void cameraCut();
};  // namespace MotionSignal
#endif
