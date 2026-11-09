#pragma once

#include <nebula_geometry.hpp>
#include "Game/Render/FrameList.hpp"

class EmitterCore;
class ParticleResource;
class LiveActor;

// What the renderer needs from frame to frame: whose drawing follows, when an object starts a new life, when the camera
// cuts. Item identities carry them.
namespace MotionSignal {
    // SceneFunction's draw phases in GameScene::draw3D.
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

    // The object with this slot (0: none yet) and generation draws next.

    inline void drawObject(u32& rSlot, u32 generation) {
        if (rSlot == 0) {
            rSlot = Frame::newSlot();
        }
        Frame::setObject(rSlot, generation);
    }

    // An emitter's particles draw next (its children under a slot of their own). Returns the name of its effect.
    u32 drawEmitter(const ParticleResource* pResource, EmitterCore* pEmitter, bool child);

    // GameScene::draw3D's begin: the camera's world-to-eye rows, then the player's up (against its gravity) and 1, or zeros
    // without a player. The renderer's sky light comes from that up.
    void sceneCamera(f32 (*pOut)[4]);

    // A new life: the actor was placed anew (MR::resetPosition) or died (LiveActor::makeActorDead). Its draws
    // at their own frame, never blended from the old life.
    void newLife(LiveActor* pActor);
    void died(LiveActor* pActor);
    // MarioAccess::setTrans and setBaseMtx: a jump far from where the player is starts a new life.
    void playerPlaced(LiveActor* pPlayer, const Vec3f& rTarget);
    // The player's life count (0 without a player); it changes when playerPlaced found a jump or the player died.
    u32 playerLife();
    // EmitterCore::init: a pooled emitter starts a new effect.
    void emitterInit(EmitterCore* pEmitter);
    // A camera cut: CameraViewInterpolator::reduceOscillation took its target without blending, or a camera animation
    // played across one of its own cuts.
    void cameraCut();
};  // namespace MotionSignal
