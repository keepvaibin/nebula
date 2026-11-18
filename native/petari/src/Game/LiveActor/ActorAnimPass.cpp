#include "Game/LiveActor/ActorAnimPass.hpp"

#if defined(NEBULA_PLAIN_PATHS)
#include "Game/Animation/XanimeCore.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/System/LumaCore.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"

// LiveActor::calcAnim is calcAnmMtx (the model's base scale, calcAndSetBaseMtx, then ModelManager::calcAnim: calc, which
// is the animation's frame, the visibility animation, the model's joint walk and clearAnm; then updateList), then the
// collision matrix. Of these only the joint walk is split off to the step threads, for actors where the walk reads and
// writes nothing but its own model's and its own player's: the model's joint matrices and scale flags, the player's joint
// poses, and the walk's own state (thread_local in this module).
//
// What one model's walk shares with others, and how this pass keeps each as the calls one after another leave it:
// - The model data's joints are every model's of that data. calcAnm writes the root's calculator into the root joint and
//   clearAnm takes it out after the walk; here both run before the walk (the root joint is left as calc leaves it) and
//   the walk takes the calculator as an argument. An actor is in a batch only when no other joint of its data holds a
//   calculator or a callback (a JointController's, a pose the player animator overwrites), and, with no animation player
//   of its own, when the root joint holds none either.
// - A clip is every model's that plays it, and calcAnm sets the clip's frame (XanimeCore::updateFrame): the frames are
//   set in list order here as there, and the walk reads each track at its player's own frame, made the same way.
// - The base matrix of a taken actor is its host's joint matrix: a taken actor is never in a batch.
// - Model::calc's callback is game code: an actor whose model has one is never in a batch.
// An actor's exact class (lumacore::exactClassOf) keeps LiveActor's calcAnim and calcAndSetBaseMtx
// (lumacore::kAnimatesAsLiveActor, which the compiler decides), so these are the calls its calcAnim makes.
namespace {
    // Walks a step thread takes at a time: the pass runs on the game thread when it has no more than this many (condition
    // 1 of P-040: the count at which the measured pass on the step threads is faster, on the PC and on the tablet).
    const u32 sWalkGrain = 8;

    struct Walk {
        LiveActor* mActor;
        Model* mModel;
        MtxCalc* mRoot;
    };

    // The batch: actors whose base matrices and frames are set, in list order, their walks to run. Game thread.
    Walk* sWalks;
    u32 sWalkRoom;
    u32 sWalkCount;

    void walkModels(void* /*pContext*/, u32 first, u32 last) {
        for (u32 i = first; i < last; i++) {
            sWalks[i].mModel->calcAnmMtxOnStepThread(sWalks[i].mRoot);
        }
    }

    // An object whose calcAnim this pass splits: its exact class keeps LiveActor's, it is not taken, its model has no calc
    // callback, its data's nesting fits a step thread's stack, and no joint of its data holds a callback or a calculator
    // (the root's may, when the actor's own player sets it).
    LiveActor* walkable(NameObj* pObj) {
        const u16 id = lumacore::exactClassOf(pObj);
        if (id == 0 || !lumacore::kAnimatesAsLiveActor[id]) {
            return nullptr;
        }
        LiveActor* const pActor = static_cast< LiveActor* >(pObj);
        if (pActor->mFlag.mIsNoCalcAnim) {
            return pActor;
        }
        ModelManager* const pManager = pActor->mModelManager;
        if (pManager == nullptr || MR::getTaken(pActor) != nullptr) {
            return nullptr;
        }
        const Model* const pModel = pManager->getModel();
        const ModelData* const pData = pModel->getModelData();
        if (pModel->mCalcCallBack != nullptr || pData->mWalkDepth > Skeleton::cStepThreadDepth) {
            return nullptr;
        }
        const u32 jointCount = pData->getJointCount();
        for (u32 i = 0; i < jointCount; i++) {
            const Joint* const pJoint = pData->getJointAt(i);
            if (pJoint->getCallBack() != nullptr) {
                return nullptr;
            }
            if (pJoint->getMtxCalc() != nullptr && (i != 0 || pManager->mXanimePlayer == nullptr)) {
                return nullptr;
            }
        }
        return pActor;
    }

#if defined(NEBULA_ANIM_WALK_VERIFY)
    // A build that checks the walks: each made again on the game thread as evaluate makes it (the root's calculator in
    // the root joint, the clips at the player's frame), from the same start; its results the same to the bit.
    struct Kept {
        u8* mBytes;
        u32 mRoom;
    };
    Kept sBefore;
    Kept sAfter;

    u32 walkBytes(const Walk& rWalk) {
        const u32 joints = rWalk.mModel->getModelData()->getJointCount();
        const XanimeCore* const pCore = static_cast< const XanimeCore* >(rWalk.mRoot);
        return joints * (sizeof(Mtx) + 1u) + (pCore != nullptr ? pCore->mJointCount * sizeof(XjointInfo) : 0u);
    }

    void reserve(Kept* pKept, u32 bytes) {
        if (pKept->mRoom < bytes) {
            const NebulaHostServices* const pHost = nebula_host();
            pKept->mRoom = bytes * 2u;
            pKept->mBytes = static_cast< u8* >(pHost->heap_realloc(pHost->user, pKept->mBytes, pKept->mRoom));
        }
    }

    // A walk's results to `pOut` (`toWalk` false) or back from it.
    u8* copyWalk(const Walk& rWalk, u8* pBytes, bool toWalk) {
        Model* const pModel = rWalk.mModel;
        const u32 joints = pModel->getModelData()->getJointCount();
        XanimeCore* const pCore = static_cast< XanimeCore* >(rWalk.mRoot);
        struct Part {
            void* mAt;
            u32 mSize;
        };
        const Part parts[] = {
            {pModel->mAnmMtx, joints * static_cast< u32 >(sizeof(Mtx))},
            {pModel->mScaleFlags, joints},
            {pCore != nullptr ? pCore->mJointList : nullptr, pCore != nullptr ? pCore->mJointCount * static_cast< u32 >(sizeof(XjointInfo)) : 0u},
        };
        for (const Part& rPart : parts) {
            if (rPart.mSize != 0) {
                if (toWalk) {
                    __builtin_memcpy(rPart.mAt, pBytes, rPart.mSize);
                } else {
                    __builtin_memcpy(pBytes, rPart.mAt, rPart.mSize);
                }
                pBytes += rPart.mSize;
            }
        }
        return pBytes;
    }

    void keepStarts() {
        u32 bytes = 0;
        for (u32 i = 0; i < sWalkCount; i++) {
            bytes += walkBytes(sWalks[i]);
        }
        reserve(&sBefore, bytes);
        reserve(&sAfter, bytes);
        u8* pBytes = sBefore.mBytes;
        for (u32 i = 0; i < sWalkCount; i++) {
            pBytes = copyWalk(sWalks[i], pBytes, false);
        }
    }

    void checkWalks() {
        u8* pBefore = sBefore.mBytes;
        u8* pAfter = sAfter.mBytes;
        for (u32 i = 0; i < sWalkCount; i++) {
            const Walk& rWalk = sWalks[i];
            const u32 bytes = walkBytes(rWalk);
            copyWalk(rWalk, pAfter, false);
            copyWalk(rWalk, pBefore, true);

            Joint* const pRoot = rWalk.mModel->getModelData()->getJointAt(0);
            MtxCalc* const pKeptCalc = pRoot->getMtxCalc();
            pRoot->setMtxCalc(rWalk.mRoot);
            XanimeCore* const pCore = static_cast< XanimeCore* >(rWalk.mRoot);
            f32 frames[8];
            const u32 tracks = pCore != nullptr ? pCore->mTrackCount : 0u;
            if (tracks > 8u) {
                NEBULA_PANIC("anim walk check: %s has %u tracks", rWalk.mActor->mName, tracks);
            }
            for (u32 t = 0; t < tracks; t++) {
                Clip* const pClip = pCore->mTrackList[t].mClip;
                if (pClip != nullptr) {
                    frames[t] = pClip->getFrame();
                    pClip->setFrame(pCore->mFrameRatio * pClip->getFrameMax());
                }
            }
            rWalk.mModel->calcAnmMtx();
            for (u32 t = 0; t < tracks; t++) {
                Clip* const pClip = pCore->mTrackList[t].mClip;
                if (pClip != nullptr) {
                    pClip->setFrame(frames[t]);
                }
            }
            pRoot->setMtxCalc(pKeptCalc);

            u8* const pMade = pBefore;
            copyWalk(rWalk, pMade, false);
            if (__builtin_memcmp(pMade, pAfter, bytes) != 0) {
                NEBULA_PANIC("anim walk on a step thread differs from the game thread's: %s", rWalk.mActor->mName);
            }
            pBefore += bytes;
            pAfter += bytes;
        }
    }
#endif

    // The batch's walks on the step threads, then each actor's draw lists and collision matrix in list order.
    void flush() {
        if (sWalkCount == 0) {
            return;
        }
#if defined(NEBULA_ANIM_WALK_VERIFY)
        keepStarts();
#endif
        NebulaHost::parallelFor(sWalkCount, ::sWalkGrain, &::walkModels, nullptr);
#if defined(NEBULA_ANIM_WALK_VERIFY)
        checkWalks();
#endif
        for (u32 i = 0; i < sWalkCount; i++) {
            LiveActor* const pActor = sWalks[i].mActor;
            pActor->mModelManager->updateList();
            if (pActor->mCollisionParts != nullptr) {
                MR::setCollisionMtx(pActor);
            }
        }
        sWalkCount = 0;
    }

    // LiveActor::calcAnim up to the walk: the model's base, the frames, the root joint as calc leaves it.
    void prepare(LiveActor* pActor) {
        ModelManager* const pManager = pActor->mModelManager;
        pActor->mFlag.mHasPose = true;
        pManager->getModel()->setBaseScale(pActor->mScale);
        pActor->LiveActor::calcAndSetBaseMtx();
        if (sWalkCount == sWalkRoom) {
            const NebulaHostServices* const pHost = nebula_host();
            sWalkRoom = sWalkRoom != 0 ? sWalkRoom * 2u : 64u;
            sWalks = static_cast< Walk* >(pHost->heap_realloc(pHost->user, sWalks, sWalkRoom * sizeof(Walk)));
        }
        Walk& rWalk = sWalks[sWalkCount++];
        rWalk.mActor = pActor;
        rWalk.mModel = pManager->getModel();
        rWalk.mRoot = pManager->prepareWalk();
    }
}  // namespace

void ActorAnimPass::execute(NameObjCategoryList::CategoryInfo& rInfo) {
    // end() is read each pass: a call may add to or remove from this category.
    if (static_cast< u32 >(rInfo.mNameObjArr.size()) <= ::sWalkGrain) {
        for (NameObj** pNameObj = rInfo.mNameObjArr.begin(); pNameObj != rInfo.mNameObjArr.end(); pNameObj++) {
            (*pNameObj)->calcAnim();
        }
        return;
    }
    for (NameObj** pNameObj = rInfo.mNameObjArr.begin(); pNameObj != rInfo.mNameObjArr.end(); pNameObj++) {
        NameObj* const pObj = *pNameObj;
        LiveActor* const pActor = ::walkable(pObj);
        if (pActor == nullptr) {
            ::flush();
            pObj->calcAnim();
        } else if (!pActor->mFlag.mIsNoCalcAnim) {
            ::prepare(pActor);
        }
    }
    ::flush();
}
#endif
