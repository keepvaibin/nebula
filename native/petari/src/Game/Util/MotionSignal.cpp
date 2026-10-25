#ifdef TARGET_PC
#include "Game/Util/MotionSignal.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/Map/LightDataHolder.hpp"
#include "Game/NameObj/NameObj.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioHolder.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Screen/LayoutActor.hpp"
#include "JSystem/J3DGraphAnimator/J3DModel.hpp"
#include "JSystem/J3DGraphAnimator/J3DModelData.hpp"
#include "JSystem/J3DGraphBase/J3DMaterial.hpp"
#include "JSystem/J3DGraphBase/J3DShape.hpp"
#include "JSystem/JUtility/JUTNameTab.hpp"
#include "JSystem/J3DGraphBase/J3DPacket.hpp"
#include "JSystem/JParticle/JPAEmitter.hpp"
#include "JSystem/JParticle/JPAEmitterManager.hpp"
#include "JSystem/JParticle/JPAParticle.hpp"
#include "JSystem/JParticle/JPAResource.hpp"
#include "Game/Effect/ParticleResourceHolder.hpp"
#include "Game/Util/SystemUtil.hpp"
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
        Kind_Material = 12,
        Kind_Effect = 13,
        Kind_TraceOnly = 14,
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

    // FNV-1a over lower-case ASCII (rt_materials.cpp rt_material_identity hashes the table's names the same way).
    u32 hashName(u32 hash, const char* pName, bool stopAtDot) {
        for (const char* c = pName; *c != '\0' && !(stopAtDot && *c == '.'); ++c) {
            const u8 v = static_cast< u8 >(*c >= 'A' && *c <= 'Z' ? *c + ('a' - 'A') : *c);
            hash = (hash ^ v) * 16777619u;
        }
        return hash;
    }

    // The models' resource names' hashes ("model/" so far), by model data: open addressing, linear probing. Scene heaps
    // are freed whole (no model data is deleted alone), so addresses come back with new models, which register again
    // over them; a full table lets a new model take its home slot.
    const u32 cModelNames = 16384;
    struct ModelName {
        const J3DModelData* mData;
        u32 mHash;
    };
    ModelName sModelNames[cModelNames];

    u32 modelSlot(const J3DModelData* pData) {
        const u32 key = static_cast< u32 >(reinterpret_cast< uintptr_t >(pData) >> 4);
        return (key * 2654435761u) & (cModelNames - 1);
    }

    // The particle effects' identities ("Effect/<ParticleNames.bcsv name>", hashed as the material table hashes its
    // `material` names), by the resource's user index; 0: not yet named.
    const u32 cEffectNames = 4096;
    u32 sEffectNames[cEffectNames];

    // An emitter's effect identity: its resource's name in the game's particle archive (Effect.arc), whose
    // resource manager the emitter draws from; 0 for another archive's resources or an unknown index.
    u32 effectIdentity(const JPAResource* pResource, const JPABaseEmitter* pEmitter) {
        const u16 index = pResource->getUsrIdx();
        if (index >= cEffectNames) {
            return 0;
        }
        const ParticleResourceHolder* pHolder = MR::getParticleResourceHolder();
        if (pHolder == nullptr || pEmitter->mpEmtrMgr == nullptr ||
            pEmitter->mpEmtrMgr->getResourceManager(pEmitter->getResourceManagerID()) != pHolder->mResourceMgr) {
            return 0;
        }
        if (sEffectNames[index] == 0) {
            const char* pName = pHolder->getParticleName(index);
            if (pName == nullptr) {
                return 0;
            }
            const u32 hash = hashName(hashName(hashName(2166136261u, "Effect", false), "/", false), pName, false);
            sEffectNames[index] = hash != 0 ? hash : 1;
        }
        return sEffectNames[index];
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

    void modelResource(const J3DModelData* pData, const char* pName) {
        if (pData == nullptr || pName == nullptr) {
            return;
        }
        const char* base = pName;
        for (const char* c = pName; *c != '\0'; ++c) {
            if (*c == '/' || *c == '\\') {
                base = c + 1;
            }
        }
        const u32 hash = hashName(hashName(2166136261u, base, true), "/", false);
        for (u32 i = 0, slot = modelSlot(pData); i < cModelNames; ++i, slot = (slot + 1) & (cModelNames - 1)) {
            if (sModelNames[slot].mData == nullptr || sModelNames[slot].mData == pData) {
                sModelNames[slot].mData = pData;
                sModelNames[slot].mHash = hash;
                return;
            }
        }
        sModelNames[modelSlot(pData)].mData = pData;
        sModelNames[modelSlot(pData)].mHash = hash;
    }

    void drawMaterialImpl(const J3DShapePacket* pPacket) {
        J3DShape* pShape = pPacket->getShape();
        J3DMaterial* pMaterial = pShape != nullptr ? pShape->getMaterial() : nullptr;
        J3DModel* pModel = const_cast< J3DModel* >(pPacket->getModel());
        J3DModelData* pData = pModel != nullptr ? pModel->getModelData() : nullptr;
        u32 key = 0;
        if (pMaterial != nullptr && pData != nullptr && pData->getMaterialName() != nullptr) {
            const char* pName = pData->getMaterialName()->getName(pMaterial->getIndex());
            for (u32 i = 0, slot = modelSlot(pData); pName != nullptr && i < cModelNames;
                 ++i, slot = (slot + 1) & (cModelNames - 1)) {
                if (sModelNames[slot].mData == pData) {
                    key = hashName(sModelNames[slot].mHash, pName, false);
                    break;
                }
                if (sModelNames[slot].mData == nullptr) {
                    break;
                }
            }
        }
        __NebulaGXHostMarker(Kind_Material, 0, key, 0);
    }

    void drawNameObjImpl(const NameObj* pObj) {
        __NebulaGXHostMarker(Kind_NameObj, auxEpoch(pObj->mMotionEpoch), id(pObj), id(*reinterpret_cast< void* const* >(pObj)));
    }

    void drawEmitterImpl(const JPAResource* pResource, const JPABaseEmitter* pEmitter, bool child) {
        __NebulaGXHostMarker(Kind_Emitter, static_cast< u16 >(auxEpoch(pEmitter->mMotionEpoch) | (child ? 1 : 0)), id(pEmitter),
                             id(pResource));
        // The effect its particles belong to, for the material table's effect rows (a fire's light, by its name).
        __NebulaGXHostMarker(Kind_Effect, 0, effectIdentity(pResource, pEmitter), 0);
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

    bool traceOnlyWanted() {
        return __NebulaGXMarkers != 0 && __NebulaOption(NEBULA_OPTION_RT_SHADOW_CASTERS) != 0;
    }

    void traceOnly(bool begin) {
        __NebulaGXHostMarker(Kind_TraceOnly, begin ? 1 : 0, 0, 0);
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

    void sceneCamera(f32 (*pOut)[4]) {
        const TPos3f& view = MR::getCameraViewMtx();
        for (int row = 0; row < 3; row++) {
            for (int column = 0; column < 4; column++) {
                pOut[row][column] = view.mMtx[row][column];
            }
        }
        pOut[3][0] = pOut[3][1] = pOut[3][2] = pOut[3][3] = 0.0f;
        if (MR::isExistSceneObj(SceneObj_MarioHolder) && MR::getMarioHolder()->getMarioActor() != nullptr) {
            const TVec3f& gravity = MR::getMarioHolder()->getMarioActor()->getGravityVec();
            if (gravity.x != 0.0f || gravity.y != 0.0f || gravity.z != 0.0f) {
                pOut[3][0] = -gravity.x;
                pOut[3][1] = -gravity.y;
                pOut[3][2] = -gravity.z;
                pOut[3][3] = 1.0f;
            }
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
