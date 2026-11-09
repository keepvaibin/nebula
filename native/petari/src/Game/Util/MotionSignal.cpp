#include "Game/Util/MotionSignal.hpp"
#include "Game/Effect/ParticleResourceHolder.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioHolder.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/Particle/EmitterCore.hpp"
#include "Game/Particle/EmitterManager.hpp"
#include "Game/Particle/ParticleResource.hpp"
#include <nebula_module.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // MarioAccess::setTrans and setBaseMtx also carry the player every frame (demos, carriers, the dome's galaxy
    // map); only a jump farther than this from the player's position is a new life. Walking moves ~15 units a frame.
    const f32 cPlayerTeleportDistance = 500.0f;

    u32 sCameraCuts;

    void bump(u32& rEpoch) {
        ++rEpoch;
    }

    // FNV-1a over lower-case ASCII.
    u32 hashName(u32 hash, const char* pName) {
        for (const char* c = pName; *c != '\0'; ++c) {
            const u8 v = static_cast< u8 >(*c >= 'A' && *c <= 'Z' ? *c + ('a' - 'A') : *c);
            hash = (hash ^ v) * 16777619u;
        }
        return hash;
    }

    u32* sEffectNames;
    u32 sEffectRoom;

    // "Effect/<ParticleNames.bcsv name>" of an emitter in the game's particle archive; 0 for another archive's.
    u32 effectName(const ParticleResource* pResource, const EmitterCore* pEmitter) {
        const u32 index = pResource->getUsrIdx();
        if (index >= sEffectRoom) {
            const u32 room = index + 1 > sEffectRoom * 2 ? index + 1 : sEffectRoom * 2;
            const NebulaHostServices* pHost = nebula_host();
            auto* pNames = static_cast< u32* >(pHost->heap_realloc(pHost->user, sEffectNames, room * sizeof(u32)));
            memset(pNames + sEffectRoom, 0, (room - sEffectRoom) * sizeof(u32));
            sEffectNames = pNames; sEffectRoom = room;
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
            const u32 hash = hashName(hashName(hashName(2166136261u, "Effect"), "/"), pName);
            sEffectNames[index] = hash != 0 ? hash : 1;
        }
        return sEffectNames[index];
    }
};  // namespace

namespace MotionSignal {
    u32 drawEmitter(const ParticleResource* pResource, EmitterCore* pEmitter, bool child) {
        if (pEmitter->mDrawSlot == 0) {
            // Two numbers: the emitter's particles and its children's.
            pEmitter->mDrawSlot = Frame::newSlot();
            Frame::newSlot();
        }
        Frame::setObject(child ? pEmitter->mDrawSlot + 1 : pEmitter->mDrawSlot, pEmitter->mMotionEpoch);
        return effectName(pResource, pEmitter);
    }

    void sceneCamera(f32 (*pOut)[4]) {
        const Pos3f& view = MR::getCameraViewMtx();
        for (int row = 0; row < 3; row++) {
            for (int column = 0; column < 4; column++) {
                pOut[row][column] = view.mMtx[row][column];
            }
        }
        pOut[3][0] = pOut[3][1] = pOut[3][2] = pOut[3][3] = 0.0f;
        if (MR::isExistSceneObj(SceneObj_MarioHolder) && MR::getMarioHolder()->getMarioActor() != nullptr) {
            const Vec3f& gravity = MR::getMarioHolder()->getMarioActor()->getGravityVec();
            if (gravity.x != 0.0f || gravity.y != 0.0f || gravity.z != 0.0f) {
                pOut[3][0] = -gravity.x;
                pOut[3][1] = -gravity.y;
                pOut[3][2] = -gravity.z;
                pOut[3][3] = 1.0f;
            }
        }
    }

    void newLife(LiveActor* pActor) {
        bump(pActor->mMotionEpoch);
        if (pActor->mModelManager != nullptr) {
            Model* pModel = pActor->mModelManager->getModel();
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

    void playerPlaced(LiveActor* pPlayer, const Vec3f& rTarget) {
        const f32 dx = rTarget.x - pPlayer->mPosition.x;
        const f32 dy = rTarget.y - pPlayer->mPosition.y;
        const f32 dz = rTarget.z - pPlayer->mPosition.z;
        // Comparisons are false for NaN: an unreadable position is no new life.
        if (dx * dx + dy * dy + dz * dz > cPlayerTeleportDistance * cPlayerTeleportDistance) {
            newLife(pPlayer);
        }
    }

    u32 playerLife() {
        if (MR::isExistSceneObj(SceneObj_MarioHolder) && MR::getMarioHolder()->getMarioActor() != nullptr) {
            return MR::getMarioHolder()->getMarioActor()->mMotionEpoch;
        }
        return 0;
    }

    void emitterInit(EmitterCore* pEmitter) {
        bump(pEmitter->mMotionEpoch);
    }

    void cameraCut() {
        Frame::cameraCut(++sCameraCuts);
    }
};  // namespace MotionSignal
