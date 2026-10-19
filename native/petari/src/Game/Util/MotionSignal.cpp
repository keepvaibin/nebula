#ifdef TARGET_PC
#include "Game/Util/MotionSignal.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/Map/LightDataHolder.hpp"
#include "Game/NameObj/NameObj.hpp"
#include "Game/Screen/LayoutActor.hpp"
#include "JSystem/J3DGraphAnimator/J3DModel.hpp"
#include "JSystem/J3DGraphBase/J3DPacket.hpp"
#include "JSystem/JParticle/JPAEmitter.hpp"
#include "JSystem/JParticle/JPAParticle.hpp"
#include <nebula_game_api.h>
#include <nw4r/lyt/pane.h>
#include <revolution/os.h>

namespace {
    // Host marker kinds and flags (galaxy/motion_signals.h).
    enum {
        Kind_ShapePacket = 1,
        Kind_NameObj = 2,
        Kind_Emitter = 3,
        Kind_Particle = 4,
        Kind_Category = 5,
        Kind_Scene = 6,
        Kind_Pass = 7,
        Kind_Light = 8,
        Kind_LayoutPane = 9,
        Kind_LayoutActor = 10,
        Kind_StarPointer = 11,
    };

    enum {
        Layout_In2D = 1,
        Layout_StarPointerDirector = 2,
        Scene_CameraCut = 1,
        Pass_MirrorBegin = 1,
        Pass_MirrorEnd = 2,
    };

    enum {
        LightSite_ActorLightInfo,
        LightSite_Coin,
        LightSite_PointLight,
        LightSite_AllWhite,
        LightSite_Player,
    };

    // MarioAccess::setTrans and setBaseMtx also carry the player every frame (demos, carriers, the dome's galaxy
    // map); only a jump farther than this from the player's position is a new life. Walking moves ~15 units a frame.
    const f32 cPlayerTeleportDistance = 500.0f;

    u32 sCameraCuts;
    bool sCameraCutPending;
    u32 sObjectEvents;
    bool s2DOpen;

    // An epoch rides above the pass bit of a marker's aux.
    u16 auxEpoch(u16 epoch) {
        return static_cast< u16 >(epoch << 1);
    }

    void bump(u16& rEpoch) {
        rEpoch = static_cast< u16 >((rEpoch + 1) & 0x7FFF);
        sObjectEvents++;
    }

    u32 id(const void* pObj) {
        return __NebulaObjectId(pObj);
    }

    u16 layoutAux() {
        return s2DOpen ? Layout_In2D : 0;
    }
};  // namespace

namespace MotionSignal {
    // The model (the instance) and the shape name the draw: objects drawn as instances of one model re-point the
    // model's shape packets at each instance's model before its draw.
    void drawShapePacketImpl(const J3DShapePacket* pPacket) {
        const J3DModel* pModel = pPacket->getModel();
        __NebulaGXHostMarker(Kind_ShapePacket, pModel != nullptr ? auxEpoch(pModel->mMotionEpoch) : 0,
                             pModel != nullptr ? id(pModel) : id(pPacket), id(pPacket->getShape()));
    }

    void drawNameObjImpl(const NameObj* pObj) {
        __NebulaGXHostMarker(Kind_NameObj, auxEpoch(pObj->mMotionEpoch), id(pObj), id(*reinterpret_cast< void* const* >(pObj)));
    }

    void drawEmitterImpl(const JPAResource* pResource, const JPABaseEmitter* pEmitter, bool child) {
        __NebulaGXHostMarker(Kind_Emitter, static_cast< u16 >(auxEpoch(pEmitter->mMotionEpoch) | (child ? 1 : 0)), id(pEmitter),
                             id(pResource));
    }

    // Particles come from a pool: the particle, its pass and its lifetime name it, and its age must continue from the
    // previous frame for the renderer to take it for the same particle.
    void drawParticleImpl(const JPABaseParticle* pParticle, bool child) {
        __NebulaGXHostMarker(Kind_Particle, child ? 1 : 0, id(pParticle),
                             static_cast< u32 >(static_cast< u16 >(pParticle->mAge)) << 16 |
                                 static_cast< u16 >(pParticle->mLifeTime));
    }

    void stripeParticle(const JPABaseParticle* pParticle, u8 strip) {
        __NebulaGXStripeParticle(id(pParticle),
                                 static_cast< u32 >(static_cast< u16 >(pParticle->mAge)) << 16 |
                                     static_cast< u16 >(pParticle->mLifeTime),
                                 strip);
    }

    void categoryImpl(u16 source, u32 type) {
        __NebulaGXHostMarker(Kind_Category, source, type, 0);
    }

    void mirror(bool begin) {
        __NebulaGXHostMarker(Kind_Pass, begin ? Pass_MirrorBegin : Pass_MirrorEnd, 0, 0);
    }

    void lightImpl(u16 site, const void* pInfo, u32 followCamera) {
        __NebulaGXHostMarker(Kind_Light, site, pInfo != nullptr ? id(pInfo) : 0, followCamera);
    }

    // Bit 0: light A (GX light 0) follows the camera; bit 1: light B (GX light 1). The renderer moves those lights
    // with the camera under ray tracing.
    void actorLight(const ActorLightInfo* pInfo) {
        if (__NebulaGXMarkers != 0) {
            lightImpl(LightSite_ActorLightInfo, pInfo, (pInfo->mInfo0.mIsFollowCamera ? 1 : 0) | (pInfo->mInfo1.mIsFollowCamera ? 2 : 0));
        }
    }

    void coinLight(const LightInfoCoin* pInfo) {
        if (__NebulaGXMarkers != 0) {
            lightImpl(LightSite_Coin, pInfo, pInfo->base.mIsFollowCamera ? 1 : 0);
        }
    }

    void pointLight(const PointLightInfo* pInfo) {
        if (__NebulaGXMarkers != 0) {
            lightImpl(LightSite_PointLight, pInfo, 0);
        }
    }

    void allLightWhite() {
        if (__NebulaGXMarkers != 0) {
            lightImpl(LightSite_AllWhite, nullptr, 0);
        }
    }

    void playerLight() {
        if (__NebulaGXMarkers != 0) {
            lightImpl(LightSite_Player, nullptr, 0);
        }
    }

    void scene2D(bool begin) {
        __NebulaGXSceneMarker(begin ? NEBULA_SCENE_2D_BEGIN : NEBULA_SCENE_2D_END, nullptr);
        s2DOpen = begin;
    }

    void layoutImpl(u8 kind, u16 aux, const void* pObj, const void* pTag) {
        __NebulaGXHostMarker(kind, aux, id(pObj), pTag != nullptr ? id(pTag) : 0);
    }

    void layoutPane(const nw4r::lyt::Pane* pPane) {
        if (__NebulaGXMarkers != 0) {
            layoutImpl(Kind_LayoutPane, layoutAux(), pPane, pPane->GetParent());
        }
    }

    void layoutActor(const LayoutActor* pActor) {
        if (__NebulaGXMarkers != 0) {
            layoutImpl(Kind_LayoutActor, layoutAux(), pActor, *reinterpret_cast< void* const* >(pActor));
        }
    }

    void starPointerLayout(const LayoutActor* pLayout) {
        if (__NebulaGXMarkers != 0) {
            layoutImpl(Kind_StarPointer, layoutAux(), pLayout, *reinterpret_cast< void* const* >(pLayout));
        }
    }

    void starPointerDirector(const void* pDirector) {
        if (__NebulaGXMarkers != 0) {
            layoutImpl(Kind_StarPointer, static_cast< u16 >(layoutAux() | Layout_StarPointerDirector), pDirector, nullptr);
        }
    }

    void sceneBegin() {
        __NebulaGXHostMarker(Kind_Scene, sCameraCutPending ? Scene_CameraCut : 0, sCameraCuts, sObjectEvents);
        sCameraCutPending = false;
        sObjectEvents = 0;
    }

    void newLife(LiveActor* pActor) {
        bump(pActor->mMotionEpoch);
        if (pActor->mModelManager != nullptr) {
            J3DModel* pModel = pActor->mModelManager->getJ3DModel();
            if (pModel != nullptr) {
                bump(pModel->mMotionEpoch);
            }
        }
    }

    // An actor the game kills again while dead (some kill their dead children every frame) ends no life.
    void died(LiveActor* pActor) {
        if (!pActor->mFlag.mIsDead) {
            newLife(pActor);
        }
    }

    void playerPlaced(LiveActor* pPlayer, const TVec3f& rTarget) {
        const f32 dx = rTarget.x - pPlayer->mPosition.x;
        const f32 dy = rTarget.y - pPlayer->mPosition.y;
        const f32 dz = rTarget.z - pPlayer->mPosition.z;
        // Comparisons are false for NaN: an unreadable position is no new life.
        if (dx * dx + dy * dy + dz * dz > cPlayerTeleportDistance * cPlayerTeleportDistance) {
            newLife(pPlayer);
        }
    }

    void emitterInit(JPABaseEmitter* pEmitter) {
        bump(pEmitter->mMotionEpoch);
    }

    void cameraCut() {
        sCameraCuts++;
        sCameraCutPending = true;
    }
};  // namespace MotionSignal
#endif
