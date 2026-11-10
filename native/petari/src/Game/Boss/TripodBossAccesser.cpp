#include "Game/Boss/TripodBossAccesser.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 cPlacementBoneIDToBoneIndexTable[] = {0,  1, 2, 7,  8,  9, 14, 15, 16, 21, 3,  4,  0,  1,  5,  6,  2,  -1, -1, -1, 10,
                                               11, 7, 8, 12, 13, 9, -1, -1, -1, 17, 18, 14, 15, 19, 20, 16, -1, -1, -1, 21};
    const s32 cPlacementBoneIDToBoneIndexTableSize = ARRAY_SIZE(cPlacementBoneIDToBoneIndexTable);

    s32 convertBoneIDToIndex(s32 id) {
        if (id < 0 || cPlacementBoneIDToBoneIndexTableSize <= id) {
            return -1;
        }

        return cPlacementBoneIDToBoneIndexTable[id];
    }
};  // namespace

TripodBossAccesser::TripodBossAccesser(const char* pName) : NameObj(pName), mBoss(), mPartsNum() {
    lumacore::setClass(this, lumacore::kClassId< TripodBossAccesser >);
}

TripodBoss* TripodBossAccesser::getTriPodBoss() const {
    return mBoss;
}

void TripodBossAccesser::setTriPodBoss(TripodBoss* pBoss) {
    mBoss = pBoss;
}

void TripodBossAccesser::addTripodBossParts(LiveActor* pActor) {
    mParts.at(mPartsNum) = pActor;
    mPartsNum++;
}

void TripodBossAccesser::showTripodBossParts() {
    for (s32 i = 0; i < mPartsNum; i++) {
        if (MR::isDead(mParts[i])) {
            mParts[i]->makeActorAppeared();
        }
    }
}

void TripodBossAccesser::hideTripodBossParts() {
    for (s32 i = 0; i < mPartsNum; i++) {
        if (!MR::isDead(mParts[i])) {
            mParts[i]->makeActorDead();
        }
    }
}

void TripodBossAccesser::activeTripodBossParts() {
    for (s32 i = 0; i < mPartsNum; i++) {
        if (MR::isDead(mParts[i])) {
            mParts[i]->makeActorAppeared();
        }

        MR::sendSimpleMsgToActor(ACTMES_TRIPODBOSS_STARTED, mParts[i]);
    }
}

bool TripodBossAccesser::isTripoddBossParts(const NameObj* pObj) const {
    if (pObj == this || pObj == mBoss) {
        return true;
    }

    for (s32 i = 0; i < mPartsNum; i++) {
        if (mParts[i] == pObj) {
            return true;
        }
    }

    return false;
}

TripodBossAccesser* TripodBossAccesser::createSceneObj() {
    return static_cast< TripodBossAccesser* >(MR::createSceneObj(SceneObj_TripodBossAccesser));
}

namespace MR {
    TripodBossAccesser* getTripodBossAccesser() {
        return MR::getSceneObj< TripodBossAccesser >(SceneObj_TripodBossAccesser);
    }

    void addTripodBossParts(LiveActor* pActor) {
        TripodBossAccesser::createSceneObj()->addTripodBossParts(pActor);
        MR::joinToMovementOnOffGroup("三脚ボス部品", pActor, 256);
    }

    void addTripodBossPartsMovement(NameObj* pObj) {
        MR::joinToMovementOnOffGroup("三脚ボス部品", pObj, 256);
    }

    bool isTripoddBossParts(const NameObj* pObj) {
        return MR::getSceneObj< TripodBossAccesser >(SceneObj_TripodBossAccesser)->isTripoddBossParts(pObj);
    }

    void showTripodBossParts() {
        TripodBossAccesser::createSceneObj()->showTripodBossParts();
    }

    void hideTripodBossParts() {
        TripodBossAccesser::createSceneObj()->hideTripodBossParts();
    }

    void activeTripodBossParts() {
        TripodBossAccesser::createSceneObj()->activeTripodBossParts();
    }

    void requestMovementTripodBossParts() {
        MR::onMovementOnOffGroup("三脚ボス部品");
    }

    bool isCreatedTripodBoss() {
        if (!MR::isExistSceneObj(SceneObj_TripodBossAccesser)) {
            return false;
        }

        return getTripodBossAccesser()->getTriPodBoss() != nullptr;
    }

    bool isStartedTripodBoss() {
        return getTripodBossAccesser()->getTriPodBoss()->isStarted();
    }

    bool isBrokenTripodBoss() {
        return getTripodBossAccesser()->getTriPodBoss()->isBroken();
    }

    bool isDemoTripodBoss() {
        return getTripodBossAccesser()->getTriPodBoss()->isDemo();
    }

    bool isStartDemoTripodBoss() {
        return getTripodBossAccesser()->getTriPodBoss()->isStartDemo();
    }

    bool isDamageDemoTripodBoss() {
        return getTripodBossAccesser()->getTriPodBoss()->isDamageDemo();
    }

    bool isEndBreakDownDemoTripodBoss() {
        return getTripodBossAccesser()->getTriPodBoss()->isEndBreakDownDemo();
    }

    bool isLeaveMarioNowTripodBoss() {
        return getTripodBossAccesser()->getTriPodBoss()->isLeaveMarioNow();
    }

    bool isRideMarioTripodBoss() {
        return getTripodBossAccesser()->getTriPodBoss()->isRideMario();
    }

    bool tryDamageDemoTripodBoss() {
        return getTripodBossAccesser()->getTriPodBoss()->tryDamageDemo();
    }

    void requestEndDamageDemoTripodBoss() {
        getTripodBossAccesser()->getTriPodBoss()->requestEndDamageDemo();
    }

    const void* getTripodBossGravityHost() {
        return getTripodBossAccesser();
    }

    void setTripodBossJointAttachBaseMatrix(const Pos3f& rPos, s32 id) {
        getTripodBossAccesser()->getTriPodBoss()->setJointAttachBaseMatrix(rPos, ::convertBoneIDToIndex(id));
    }

    void requestStartTripodBossStepSequence(s32 seq) {
        getTripodBossAccesser()->getTriPodBoss()->requestStartStepSequence(seq);
    }

    void addTripodBossStepPoint(TripodBossStepPoint* pPoint) {
        getTripodBossAccesser()->getTriPodBoss()->addStepPoint(pPoint);
    }

    void getTripodBossJointMatrix(Pos3f* pMtx, s32 id) {
        getTripodBossAccesser()->getTriPodBoss()->getJointMatrix(pMtx, ::convertBoneIDToIndex(id));
    }

    void getTripodBossAttachJointMatrix(Pos3f* pMtx, s32 id) {
        getTripodBossAccesser()->getTriPodBoss()->getJointAttachMatrix(pMtx, ::convertBoneIDToIndex(id));
    }

    void concatTripodBossAttachJointMatrix(Pos3f* pMtx, s32 id) {
        if (MR::isCreatedTripodBoss()) {
            Pos3f mtx;
            MR::getTripodBossAttachJointMatrix(&mtx, id);
            pMtx->concat(mtx, *pMtx);
        }
    }

    bool isSteppingTripodBossLegID(s32 id) {
        return !getTripodBossAccesser()->getTriPodBoss()->isStopLeg(id);
    }

    bool isSteppingTripodBossJointID(s32 id) {
        s32 partID = TripodBoss::getPartIDFromBoneID(::convertBoneIDToIndex(id));
        bool isValidPartID = partID >= TripodBoss::PART_ID_LEFT_LEG && partID < TripodBoss::PART_ID_MAX;

        if (isValidPartID) {
            return MR::isSteppingTripodBossLegID(partID);
        }

        return false;
    }
};  // namespace MR
