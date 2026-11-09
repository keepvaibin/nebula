#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/HitSensorInfo.hpp"
#include "Game/LiveActor/HitSensorKeeper.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/LiveActorGroupArray.hpp"
#include "Game/LiveActor/MessageSensorHolder.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"

namespace {
    // The kinds' bounds are markers, not kinds.
    bool isTypeBetween(const HitSensor* pSensor, u32 start, u32 end) {
        return pSensor->mType > start && pSensor->mType < end;
    }
};  // namespace

namespace MR {
    HitSensor* addHitSensor(LiveActor* pActor, const char* pName, u32 type, u32 groupSize, f32 radius, const Vec3f& rOffset) {
        return pActor->mSensorKeeper->add(pName, type, groupSize, radius, pActor, rOffset);
    }

    HitSensor* addHitSensorBinder(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, const Vec3f& rOffset) {
        return addHitSensor(pActor, pName, ATYPE_BINDER, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorTransferableBinder(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, const Vec3f& rOffset) {
        return addHitSensor(pActor, pName, ATYPE_TRANSFERABLE_BINDER, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorPriorBinder(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, const Vec3f& rOffset) {
        return addHitSensor(pActor, pName, ATYPE_PRIOR_BINDER, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorRide(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, const Vec3f& rOffset) {
        return addHitSensor(pActor, pName, ATYPE_RIDE, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorMapObj(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, const Vec3f& rOffset) {
        return addHitSensor(pActor, pName, ATYPE_MAP_OBJ, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorMapObjPress(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, const Vec3f& rOffset) {
        return addHitSensor(pActor, pName, ATYPE_MAP_OBJ_PRESS, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorMapObjSimple(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, const Vec3f& rOffset) {
        return addHitSensor(pActor, pName, ATYPE_MAP_OBJ_SIMPLE, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorMapObjMoveCollision(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, const Vec3f& rOffset) {
        return addHitSensor(pActor, pName, ATYPE_MAP_OBJ_MOVE_COLLISION, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorEnemy(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, const Vec3f& rOffset) {
        return addHitSensor(pActor, pName, ATYPE_ENEMY, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorEnemySimple(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, const Vec3f& rOffset) {
        return addHitSensor(pActor, pName, ATYPE_ENEMY_SIMPLE, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorEnemyAttack(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, const Vec3f& rOffset) {
        return addHitSensor(pActor, pName, ATYPE_ENEMY_ATTACK, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorNpc(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, const Vec3f& rOffset) {
        return addHitSensor(pActor, pName, ATYPE_NPC, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorEye(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, const Vec3f& rOffset) {
        return addHitSensor(pActor, pName, ATYPE_EYE, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorPush(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, const Vec3f& rOffset) {
        return addHitSensor(pActor, pName, ATYPE_PUSH, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorPos(LiveActor* pActor, const char* pName, u32 type, u32 groupSize, f32 radius, const Vec3f* pPos, const Vec3f& rOffset) {
        return pActor->mSensorKeeper->addPos(pName, type, groupSize, radius, pActor, pPos, rOffset);
    }

    HitSensor* addHitSensorPosBinder(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, const Vec3f* pPos, const Vec3f& rOffset) {
        return addHitSensorPos(pActor, pName, ATYPE_BINDER, groupSize, radius, pPos, rOffset);
    }

    HitSensor* addHitSensorPosRide(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, const Vec3f* pPos, const Vec3f& rOffset) {
        return addHitSensorPos(pActor, pName, ATYPE_RIDE, groupSize, radius, pPos, rOffset);
    }

    HitSensor* addHitSensorPosMapObj(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, const Vec3f* pPos, const Vec3f& rOffset) {
        return addHitSensorPos(pActor, pName, ATYPE_MAP_OBJ, groupSize, radius, pPos, rOffset);
    }

    HitSensor* addHitSensorPosEye(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, const Vec3f* pPos, const Vec3f& rOffset) {
        return addHitSensorPos(pActor, pName, ATYPE_EYE, groupSize, radius, pPos, rOffset);
    }

    HitSensor* addHitSensorMtx(LiveActor* pActor, const char* pName, u32 type, u32 groupSize, f32 radius, MtxPtr pMtx, const Vec3f& rOffset) {
        return pActor->mSensorKeeper->addMtx(pName, type, groupSize, radius, pActor, pMtx, rOffset);
    }

    HitSensor* addHitSensorMtxRide(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, MtxPtr pMtx, const Vec3f& rOffset) {
        return addHitSensorMtx(pActor, pName, ATYPE_RIDE, groupSize, radius, pMtx, rOffset);
    }

    HitSensor* addHitSensorMtxMapObj(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, MtxPtr pMtx, const Vec3f& rOffset) {
        return addHitSensorMtx(pActor, pName, ATYPE_MAP_OBJ, groupSize, radius, pMtx, rOffset);
    }

    HitSensor* addHitSensorMtxEnemy(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, MtxPtr pMtx, const Vec3f& rOffset) {
        return addHitSensorMtx(pActor, pName, ATYPE_ENEMY, groupSize, radius, pMtx, rOffset);
    }

    HitSensor* addHitSensorMtxEnemyAttack(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, MtxPtr pMtx, const Vec3f& rOffset) {
        return addHitSensorMtx(pActor, pName, ATYPE_ENEMY_ATTACK, groupSize, radius, pMtx, rOffset);
    }

    HitSensor* addHitSensorMtxNpc(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, MtxPtr pMtx, const Vec3f& rOffset) {
        return addHitSensorMtx(pActor, pName, ATYPE_NPC, groupSize, radius, pMtx, rOffset);
    }

    HitSensor* addHitSensorMtxAnimal(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius, MtxPtr pMtx, const Vec3f& rOffset) {
        return addHitSensorMtx(pActor, pName, ATYPE_ANIMAL, groupSize, radius, pMtx, rOffset);
    }

    HitSensor* addHitSensorAtJoint(LiveActor* pActor, const char* pName, const char* pJointName, u32 type, u32 groupSize, f32 radius,
                                   const Vec3f& rOffset) {
        return addHitSensorMtx(pActor, pName, type, groupSize, radius, getJointMtx(pActor, pJointName), rOffset);
    }

    HitSensor* addHitSensorAtJointRide(LiveActor* pActor, const char* pName, const char* pJointName, u32 groupSize, f32 radius,
                                       const Vec3f& rOffset) {
        return addHitSensorAtJoint(pActor, pName, pJointName, ATYPE_RIDE, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorAtJointMapObj(LiveActor* pActor, const char* pName, const char* pJointName, u32 groupSize, f32 radius,
                                         const Vec3f& rOffset) {
        return addHitSensorAtJoint(pActor, pName, pJointName, ATYPE_MAP_OBJ, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorAtJointMapObjSimple(LiveActor* pActor, const char* pName, const char* pJointName, u32 groupSize, f32 radius,
                                               const Vec3f& rOffset) {
        return addHitSensorAtJoint(pActor, pName, pJointName, ATYPE_MAP_OBJ_SIMPLE, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorAtJointEnemy(LiveActor* pActor, const char* pName, const char* pJointName, u32 groupSize, f32 radius,
                                        const Vec3f& rOffset) {
        return addHitSensorAtJoint(pActor, pName, pJointName, ATYPE_ENEMY, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorAtJointEnemySimple(LiveActor* pActor, const char* pName, const char* pJointName, u32 groupSize, f32 radius,
                                              const Vec3f& rOffset) {
        return addHitSensorAtJoint(pActor, pName, pJointName, ATYPE_ENEMY_SIMPLE, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorAtJointEnemyAttack(LiveActor* pActor, const char* pName, const char* pJointName, u32 groupSize, f32 radius,
                                              const Vec3f& rOffset) {
        return addHitSensorAtJoint(pActor, pName, pJointName, ATYPE_ENEMY_ATTACK, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorAtJointNpc(LiveActor* pActor, const char* pName, const char* pJointName, u32 groupSize, f32 radius,
                                      const Vec3f& rOffset) {
        return addHitSensorAtJoint(pActor, pName, pJointName, ATYPE_NPC, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorAtJointEye(LiveActor* pActor, const char* pName, const char* pJointName, u32 groupSize, f32 radius,
                                      const Vec3f& rOffset) {
        return addHitSensorAtJoint(pActor, pName, pJointName, ATYPE_EYE, groupSize, radius, rOffset);
    }

    HitSensor* addHitSensorCallback(LiveActor* pActor, const char* pName, u32 type, u32 groupSize, f32 radius) {
        return pActor->mSensorKeeper->addCallback(pName, type, groupSize, radius, pActor);
    }

    HitSensor* addHitSensorCallbackBinder(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius) {
        return addHitSensorCallback(pActor, pName, ATYPE_BINDER, groupSize, radius);
    }

    HitSensor* addHitSensorCallbackPriorBinder(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius) {
        return addHitSensorCallback(pActor, pName, ATYPE_PRIOR_BINDER, groupSize, radius);
    }

    HitSensor* addHitSensorCallbackRide(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius) {
        return addHitSensorCallback(pActor, pName, ATYPE_RIDE, groupSize, radius);
    }

    HitSensor* addHitSensorCallbackMapObj(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius) {
        return addHitSensorCallback(pActor, pName, ATYPE_MAP_OBJ, groupSize, radius);
    }

    HitSensor* addHitSensorCallbackMapObjSimple(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius) {
        return addHitSensorCallback(pActor, pName, ATYPE_MAP_OBJ_SIMPLE, groupSize, radius);
    }

    HitSensor* addHitSensorCallbackEnemy(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius) {
        return addHitSensorCallback(pActor, pName, ATYPE_ENEMY, groupSize, radius);
    }

    HitSensor* addHitSensorCallbackEnemyAttack(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius) {
        return addHitSensorCallback(pActor, pName, ATYPE_ENEMY_ATTACK, groupSize, radius);
    }

    HitSensor* addHitSensorCallbackEye(LiveActor* pActor, const char* pName, u32 groupSize, f32 radius) {
        return addHitSensorCallback(pActor, pName, ATYPE_EYE, groupSize, radius);
    }

    HitSensor* addBodyMessageSensorReceiver(LiveActor* pActor) {
        return addBodyMessageSensor(pActor, ATYPE_RECEIVER);
    }

    HitSensor* addBodyMessageSensorMapObj(LiveActor* pActor) {
        return addBodyMessageSensor(pActor, ATYPE_MAP_OBJ);
    }

    HitSensor* addBodyMessageSensorMapObjPress(LiveActor* pActor) {
        return addBodyMessageSensor(pActor, ATYPE_MAP_OBJ_PRESS);
    }

    HitSensor* addBodyMessageSensorMapObjMoveCollision(LiveActor* pActor) {
        return addBodyMessageSensor(pActor, ATYPE_MAP_OBJ_MOVE_COLLISION);
    }

    HitSensor* addBodyMessageSensorEnemy(LiveActor* pActor) {
        return addBodyMessageSensor(pActor, ATYPE_ENEMY);
    }

    HitSensor* addMessageSensor(LiveActor* pActor, const char* pName, u32 type) {
        return addHitSensor(pActor, pName, type, 0, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    }

    HitSensor* addMessageSensorReceiver(LiveActor* pActor, const char* pName) {
        return addMessageSensor(pActor, pName, ATYPE_RECEIVER);
    }

    HitSensor* addMessageSensorMapObj(LiveActor* pActor, const char* pName) {
        return addMessageSensor(pActor, pName, ATYPE_MAP_OBJ);
    }

    HitSensor* addMessageSensorMapObjMoveCollision(LiveActor* pActor, const char* pName) {
        return addMessageSensor(pActor, pName, ATYPE_MAP_OBJ_MOVE_COLLISION);
    }

    HitSensor* addMessageSensorEnemy(LiveActor* pActor, const char* pName) {
        return addMessageSensor(pActor, pName, ATYPE_ENEMY);
    }

    bool tryUpdateHitSensorsAll(LiveActor* pActor) {
        if (pActor->mSensorKeeper != nullptr) {
            updateHitSensorsAll(pActor);

            return true;
        }

        return false;
    }

    void updateHitSensorsAll(LiveActor* pActor) {
        pActor->mSensorKeeper->update();
    }

    bool isSensorType(const HitSensor* pSensor, u32 type) {
        return pSensor->isType(type);
    }

    HitSensor* getSensorWithIndex(LiveActor* pActor, int index) {
        return pActor->mSensorKeeper->getNthSensorInfo(index)->mSensor;
    }

    HitSensor* getTaking(const LiveActor* pActor) {
        return pActor->mSensorKeeper != nullptr ? pActor->mSensorKeeper->mTaking : nullptr;
    }

    HitSensor* getTaken(const LiveActor* pActor) {
        return pActor->mSensorKeeper != nullptr ? pActor->mSensorKeeper->mTaken : nullptr;
    }

    void setSensorPos(HitSensor* pSensor, const Vec3f& rPos) {
        pSensor->mPosition.set(rPos);
    }

    void setSensorOffset(LiveActor* pActor, const char* pName, const Vec3f& rOffset) {
        pActor->mSensorKeeper->getSensorInfo(pName)->setOffset(rOffset);
    }

    void setSensorRadius(LiveActor* pActor, const char* pName, f32 radius) {
        pActor->mSensorKeeper->getSensorInfo(pName)->mSensor->mRadius = radius;
    }

    void setHitSensorApart(HitSensor* pSender, HitSensor* pReceiver) {
        if (getTaking(getSensorHost(pSender)) == pReceiver || getTaken(getSensorHost(pReceiver)) == pSender) {
            getSensorHost(pSender)->mSensorKeeper->mTaking = nullptr;
            getSensorHost(pReceiver)->mSensorKeeper->mTaken = nullptr;
        } else {
            getSensorHost(pSender)->mSensorKeeper->mTaken = nullptr;
            getSensorHost(pReceiver)->mSensorKeeper->mTaking = nullptr;
        }
    }

    void validateHitSensors(LiveActor* pActor) {
        if (pActor->mSensorKeeper != nullptr) {
            pActor->mSensorKeeper->validate();
        }
    }

    void invalidateHitSensors(LiveActor* pActor) {
        if (pActor->mSensorKeeper != nullptr) {
            pActor->mSensorKeeper->invalidate();
        }
    }

    void validateHitSensor(LiveActor* pActor, const char* pName) {
        pActor->mSensorKeeper->getSensor(pName)->validate();
    }

    void invalidateHitSensor(LiveActor* pActor, const char* pName) {
        pActor->mSensorKeeper->getSensor(pName)->invalidate();
    }

    bool isValidHitSensor(LiveActor* pActor, const char* pName) {
        return pActor->mSensorKeeper->getSensor(pName)->mValidByHost;
    }

    void clearHitSensors(LiveActor* pActor) {
        if (pActor->mSensorKeeper != nullptr) {
            pActor->mSensorKeeper->clear();
        }
    }

    HitSensor* getSensor(LiveActor* pActor, int index) {
        return pActor->mSensorKeeper->getNthSensorInfo(index)->mSensor;
    }

    LiveActor* getSensorHost(const HitSensor* pSensor) {
        return pSensor->mHost;
    }

    bool isSensor(const HitSensor* pSensor, const char* pName) {
        return pSensor == getSensorHost(pSensor)->getSensor(pName);
    }

    bool isSensorPlayer(const HitSensor* pSensor) {
        return pSensor->isType(ATYPE_PLAYER);
    }

    bool isSensorBinder(const HitSensor* pSensor) {
        return pSensor->isType(ATYPE_BINDER);
    }

    bool isSensorRide(const HitSensor* pSensor) {
        return ::isTypeBetween(pSensor, ATYPE_RIDE_START, ATYPE_RIDE_END);
    }

    bool isSensorPlayerOrRide(const HitSensor* pSensor) {
        return isSensorPlayer(pSensor) || isSensorRide(pSensor);
    }

    bool isSensorEnemy(const HitSensor* pSensor) {
        return ::isTypeBetween(pSensor, ATYPE_ENEMY_START, ATYPE_ENEMY_END);
    }

    bool isSensorEnemyAttack(const HitSensor* pSensor) {
        return pSensor->isType(ATYPE_ENEMY_ATTACK);
    }

    bool isSensorNpc(const HitSensor* pSensor) {
        return ::isTypeBetween(pSensor, ATYPE_NPC_START, ATYPE_NPC_END);
    }

    bool isSensorMapObj(const HitSensor* pSensor) {
        return ::isTypeBetween(pSensor, ATYPE_MAPOBJ_START, ATYPE_MAPOBJ_END);
    }

    bool isSensorAutoRush(const HitSensor* pSensor) {
        return ::isTypeBetween(pSensor, ATYPE_AUTO_RUSH_OBJ_START, ATYPE_AUTO_RUSH_OBJ_END);
    }

    bool isSensorRush(const HitSensor* pSensor) {
        return ::isTypeBetween(pSensor, ATYPE_RUSH_OBJ_START, ATYPE_RUSH_OBJ_END);
    }

    bool isSensorPressObj(const HitSensor* pSensor) {
        return ::isTypeBetween(pSensor, ATYPE_PRESS_OBJ_START, ATYPE_PRESS_OBJ_END);
    }

    bool isSensorEye(const HitSensor* pSensor) {
        return pSensor->isType(ATYPE_EYE);
    }

    bool isSensorPush(const HitSensor* pSensor) {
        return pSensor->isType(ATYPE_PUSH);
    }

    bool isSensorItem(const HitSensor* pSensor) {
        return pSensor->isType(ATYPE_COIN) || pSensor->isType(ATYPE_COIN_RED) || pSensor->isType(ATYPE_KINOKO_ONEUP);
    }

    bool tryGetItem(HitSensor* pSender, HitSensor* pReceiver) {
        return pReceiver->receiveMessage(ACTMES_ITEM_GET, pSender);
    }

    const Vec3f& getSensorPos(const HitSensor* pSensor) {
        return pSensor->mPosition;
    }

    void calcSensorDirection(Vec3f* pDir, const HitSensor* pSensor1, const HitSensor* pSensor2) {
        Vec3f dir = pSensor2->mPosition - pSensor1->mPosition;
        pDir->set(dir);
    }

    void calcSensorDirectionNormalize(Vec3f* pDir, const HitSensor* pSensor1, const HitSensor* pSensor2) {
        calcSensorDirection(pDir, pSensor1, pSensor2);
        normalizeOrZero(pDir);
    }

    void calcSensorHorizon(Vec3f* pHorizon, const Vec3f& rGravity, const HitSensor* pSensor1, const HitSensor* pSensor2) {
        Vec3f horizon = pSensor2->mPosition - pSensor1->mPosition;
        pHorizon->killElement(horizon, rGravity);
    }

    void calcSensorHorizonNormalize(Vec3f* pHorizon, const Vec3f& rGravity, const HitSensor* pSensor1, const HitSensor* pSensor2) {
        calcSensorHorizon(pHorizon, rGravity, pSensor1, pSensor2);
        normalizeOrZero(pHorizon);
    }

    HitSensor* getMessageSensor() {
        return getSceneObj< MessageSensorHolder >(SceneObj_MessageSensorHolder)->getSensor(nullptr);
    }

    bool sendArbitraryMsg(u32 msg, HitSensor* pReceiver, HitSensor* pSender) {
        return pReceiver->receiveMessage(msg, pSender);
    }

    bool sendMsgPush(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_PUSH, pReceiver, pSender);
    }

    bool sendMsgPlayerTrample(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_PLAYER_TRAMPLE, pReceiver, pSender);
    }

    bool sendMsgPlayerPunch(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_PLAYER_PUNCH, pReceiver, pSender);
    }

    bool sendMsgJump(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_JUMP, pReceiver, pSender);
    }

    bool sendMsgTouchJump(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_TOUCHJUMP, pReceiver, pSender);
    }

    bool sendMsgTaken(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_TAKEN, pReceiver, pSender);
    }

    bool sendMsgKick(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_KICK, pReceiver, pSender);
    }

    bool sendMsgAwayJump(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_AWAYJUMP, pReceiver, pSender);
    }

    bool sendMsgEnemyAttackMsgToDir(u32 msg, HitSensor* pReceiver, HitSensor* pSender, const Vec3f& rDir) {
        Vec3f senderPos = pSender->mPosition;
        pSender->mPosition.sub(pReceiver->mPosition, rDir);

        bool isSent = pReceiver->receiveMessage(msg, pSender);
        setSensorPos(pSender, senderPos);

        return isSent;
    }

    bool sendMsgEnemyAttackFlipWeak(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_ENEMY_ATTACK_FLIP_WEAK, pReceiver, pSender);
    }

    bool sendMsgEnemyAttackFlipWeakJump(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_ENEMY_ATTACK_FLIP_WEAK_JUMP, pReceiver, pSender);
    }

    bool sendMsgEnemyAttackFlip(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_ENEMY_ATTACK_FLIP, pReceiver, pSender);
    }

    bool sendMsgEnemyAttackFlipToDir(HitSensor* pReceiver, HitSensor* pSender, const Vec3f& rDir) {
        return sendMsgEnemyAttackMsgToDir(ACTMES_ENEMY_ATTACK_FLIP, pReceiver, pSender, rDir);
    }

    bool sendMsgEnemyAttackFlipJump(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_ENEMY_ATTACK_FLIP_JUMP, pReceiver, pSender);
    }

    bool sendMsgEnemyAttackFlipRot(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_ENEMY_ATTACK_FLIP_ROT, pReceiver, pSender);
    }

    bool sendMsgEnemyAttackFlipMaximum(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_ENEMY_ATTACK_FLIP_MAXIMUM, pReceiver, pSender);
    }

    bool sendMsgEnemyAttackFlipMaximumToDir(HitSensor* pReceiver, HitSensor* pSender, const Vec3f& rDir) {
        return sendMsgEnemyAttackMsgToDir(ACTMES_ENEMY_ATTACK_FLIP_MAXIMUM, pReceiver, pSender, rDir);
    }

    bool sendMsgEnemyAttack(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_ENEMY_ATTACK, pReceiver, pSender);
    }

    bool sendMsgEnemyAttackStrong(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_ENEMY_ATTACK_STRONG, pReceiver, pSender);
    }

    bool sendMsgEnemyAttackStrongToDir(HitSensor* pReceiver, HitSensor* pSender, const Vec3f& rDir) {
        return sendMsgEnemyAttackMsgToDir(ACTMES_ENEMY_ATTACK_STRONG, pReceiver, pSender, rDir);
    }

    bool sendMsgEnemyAttackFire(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_ENEMY_ATTACK_FIRERUN, pReceiver, pSender);
    }

    bool sendMsgEnemyAttackFireStrong(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_ENEMY_ATTACK_FIRERUN_STRONG, pReceiver, pSender);
    }

    bool sendMsgEnemyAttackElectric(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_ENEMY_ATTACK_ELECTRIC, pReceiver, pSender);
    }

    bool sendMsgEnemyAttackFreeze(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_ENEMY_ATTACK_FREEZE, pReceiver, pSender);
    }

    bool sendMsgEnemyAttackHeatBeam(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_ENEMY_ATTACK_HEATBEAM, pReceiver, pSender);
    }

    bool sendMsgEnemyAttackExplosion(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_ENEMY_ATTACK_EXPLOSION, pReceiver, pSender);
    }

    bool sendMsgEnemyAttackCounterSpin(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_ENEMY_ATTACK_COUNTER_SPIN, pReceiver, pSender);
    }

    bool sendMsgEnemyAttackCounterHipDrop(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_ENEMY_ATTACK_COUNTER_HIPDROP, pReceiver, pSender);
    }

    bool sendMsgLockOnStarPieceShoot(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_IS_LOCKON_STAR_PIECE_SHOOT, pReceiver, pSender);
    }

    bool sendMsgStarPieceAttack(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_STAR_PIECE_ATTACK, pReceiver, pSender);
    }

    bool sendMsgStarPieceGift(HitSensor* pReceiver, HitSensor* pSender, u32 num) {
        return sendArbitraryMsg((num + ACTMES_STAR_PIECE_GIFT) - 1, pReceiver, pSender);
    }

    bool sendMsgEnemyAttackMaximum(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_ENEMY_ATTACK_MAXIMUM, pReceiver, pSender);
    }

    bool sendMsgEnemyAttackMaximumToDir(HitSensor* pReceiver, HitSensor* pSender, const Vec3f& rDir) {
        return sendMsgEnemyAttackMsgToDir(ACTMES_ENEMY_ATTACK_MAXIMUM, pReceiver, pSender, rDir);
    }

    bool sendMsgEnemyAttackToBindedSensor(LiveActor* pActor, HitSensor* pSender) {
        return sendMsgToBindedSensor(ACTMES_ENEMY_ATTACK, pActor, pSender);
    }

    bool sendMsgEnemyAttackExplosionToBindedSensor(LiveActor* pActor, HitSensor* pSender) {
        return sendMsgToBindedSensor(ACTMES_ENEMY_ATTACK_EXPLOSION, pActor, pSender);
    }

    bool sendSimpleMsgToActor(u32 msg, LiveActor* pActor) {
        HitSensor* pReceiver = getMessageSensor();
        HitSensor* pSender = getMessageSensor();

        // No actor (a target that was never picked) takes no message.
        if (pActor == nullptr) {
            return false;
        }

        return pActor->receiveMessage(msg, pSender, pReceiver);
    }

    bool sendMsgToBindedSensor(u32 msg, LiveActor* pActor, HitSensor* pSender) {
        const u32 planeRoom = pActor->mBinder->mPlaneNum > 3 ? pActor->mBinder->mPlaneNum : 3;  // planes, or ground, wall and roof
        MR::LocalArray< HitInfo*, 32 > info(planeRoom);  // a receiver may send again
        u32 size = pActor->mBinder->copyPlaneArrayAndSortingSensor(info.mItems, planeRoom);

        if (size == 0) {
            return false;
        }

        bool isSent = false;
        HitSensor* pReceiver = info[0]->mParentTriangle.mSensor;
        isSent |= pReceiver->receiveMessage(msg, pSender);

        for (u32 i = 1; i < size; i++) {
            if (info[i]->mParentTriangle.mSensor == pReceiver) {
                continue;
            }

            pReceiver = info[i]->mParentTriangle.mSensor;
            isSent |= pReceiver->receiveMessage(msg, pSender);
        }

        return isSent;
    }

    bool sendMsgToBindedSensor(u32 msg, HitSensor* pSender) {
        return sendMsgToBindedSensor(msg, getSensorHost(pSender), pSender);
    }

    bool sendMsgToGroundSensor(u32 msg, HitSensor* pSender) {
        if (!isBindedGround(getSensorHost(pSender))) {
            return false;
        }

        return getGroundSensor(getSensorHost(pSender))->receiveMessage(msg, pSender);
    }

    bool sendMsgToWallSensor(u32 msg, HitSensor* pSender) {
        if (!isBindedWall(getSensorHost(pSender))) {
            return false;
        }

        return getWallSensor(getSensorHost(pSender))->receiveMessage(msg, pSender);
    }

    bool sendMsgStartDemo(LiveActor* pActor) {
        return sendSimpleMsgToActor(ACTMES_START_DEMO, pActor);
    }

    bool sendMsgToEnemyAttackBlow(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_TO_ENEMY_ATTACK_BLOW, pReceiver, pSender);
    }

    bool sendMsgToEnemyAttackTrample(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_TO_ENEMY_ATTACK_TRAMPLE, pReceiver, pSender);
    }

    bool sendMsgToEnemyAttackBlowOrTrample(HitSensor* pReceiver, HitSensor* pSender, f32 ratio) {
        Vec3f dir = pReceiver->mPosition - pSender->mPosition;
        normalizeOrZero(&dir);

        if (ratio < getSensorHost(pSender)->mGravity.dot(dir)) {
            return sendMsgToEnemyAttackTrample(pReceiver, pSender);
        } else {
            return sendMsgToEnemyAttackBlow(pReceiver, pSender);
        }
    }

    bool sendMsgToEnemyAttackShockWave(HitSensor* pReceiver, HitSensor* pSender) {
        return sendArbitraryMsg(ACTMES_TO_ENEMY_ATTACK_SHOCK_WAVE, pReceiver, pSender);
    }

    void sendMsgToAllLiveActor(u32 msg, LiveActor* pActor) {
        AllLiveActorGroup* const pGroup = getAllLiveActorGroup();
        HitSensor* const pMessageSensor = getMessageSensor();

        // A message may register actors: the count is read each time.
        for (int i = 0; i < pGroup->getObjNum(); i++) {
            LiveActor* const pGroupActor = pGroup->getActor(i);

            if (!isDead(pGroupActor) && pGroupActor != pActor) {
                pGroupActor->receiveMessage(msg, pMessageSensor, pMessageSensor);
            }
        }
    }

    void sendMsgToGroupMember(u32 msg, LiveActor* pActor, HitSensor* pSender, const char* pName) {
        MsgSharedGroup* pGroup = static_cast< MsgSharedGroup* >(getGroupFromArray(pActor));

        if (pGroup != nullptr) {
            pGroup->sendMsgToGroupMember(msg, pSender, pName);
        } else {
            pActor->receiveMessage(msg, pSender, pActor->getSensor(pName));
        }
    }

    void sendMsgExplosionToNearActor(HitSensor* pSender, f32 distance) {
        HitSensor* pReceiver;

        for (u32 i = 0; i < pSender->mSensorCount; i++) {
            pReceiver = pSender->mSensors[i];

            if (distance < 0.0f || isNear(pSender, pReceiver, distance)) {
                sendArbitraryMsg(ACTMES_ENEMY_ATTACK_EXPLOSION, pReceiver, pSender);
            }
        }
    }

    bool isInSpinStormRange(u32 msg, HitSensor* pSender, HitSensor* pReceiver, f32 range) {
        return msg == ACTMES_SPIN_STORM_RANGE && isNear(pSender, pReceiver, range);
    }

    bool receiveItemShowMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
        if (msg == ACTMES_ITEM_SHOW && isDead(getSensorHost(pReceiver))) {
            getSensorHost(pReceiver)->makeActorAppeared();

            return true;
        }

        return false;
    }

    bool receiveItemHideMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
        if (msg == ACTMES_ITEM_HIDE && !isDead(getSensorHost(pReceiver))) {
            getSensorHost(pReceiver)->makeActorDead();

            return true;
        }

        return false;
    }

    HitSensor* getGroundSensor(const LiveActor* pActor) {
        return pActor->mBinder->mGroundInfo.mParentTriangle.mSensor;
    }

    HitSensor* getRoofSensor(const LiveActor* pActor) {
        return pActor->mBinder->mRoofInfo.mParentTriangle.mSensor;
    }

    HitSensor* getWallSensor(const LiveActor* pActor) {
        return pActor->mBinder->mWallInfo.mParentTriangle.mSensor;
    }

    bool isMsgPlayerHitAll(u32 msg) {
        return msg == ACTMES_PLAYER_PUNCH || msg == ACTMES_PLAYER_UPPER_PUNCH || msg == ACTMES_JET_TURTLE_ATTACK || msg == ACTMES_FIREBALL_ATTACK ||
               msg == ACTMES_FREEZE_ATTACK || msg == ACTMES_INVINCIBLE_ATTACK;
    }

    bool isMsgPlayerSpinAttack(u32 msg) {
        return msg == ACTMES_PLAYER_PUNCH;
    }

    bool isMsgPlayerTrample(u32 msg) {
        return msg == ACTMES_PLAYER_TRAMPLE;
    }

    bool isMsgPlayerHipDrop(u32 msg) {
        return msg == ACTMES_PLAYER_HIP_DROP;
    }

    bool isMsgPlayerHipDropFloor(u32 msg) {
        return msg == ACTMES_PLAYER_HIP_DROP_FLOOR;
    }

    bool isMsgPlayerUpperPunch(u32 msg) {
        return msg == ACTMES_PLAYER_UPPER_PUNCH;
    }

    bool isMsgPlayerKick(u32 msg) {
        return msg == ACTMES_KICK;
    }

    bool isMsgJetTurtleAttack(u32 msg) {
        return msg == ACTMES_JET_TURTLE_ATTACK;
    }

    bool isMsgFireBallAttack(u32 msg) {
        return msg == ACTMES_FIREBALL_ATTACK;
    }

    bool isMsgSearchlightAttack(u32 msg) {
        return msg == ACTMES_SEARCHLIGHT_ATTACK;
    }

    bool isMsgFreezeAttack(u32 msg) {
        return msg == ACTMES_FREEZE_ATTACK;
    }

    bool isMsgInvincibleAttack(u32 msg) {
        return msg == ACTMES_INVINCIBLE_ATTACK;
    }

    bool isMsgInvalidHit(u32 msg) {
        return msg == ACTMES_INVALID_HIT;
    }

    bool isMsgAutoRushBegin(u32 msg) {
        return msg == ACTMES_AUTORUSH_BEGIN;
    }

    bool isMsgRushBegin(u32 msg) {
        return msg == ACTMES_RUSH_BEGIN;
    }

    bool isMsgUpdateBaseMtx(u32 msg) {
        return msg == ACTMES_UPDATE_BASEMTX;
    }

    bool isMsgRushCancel(u32 msg) {
        return msg == ACTMES_RUSH_CANCEL;
    }

    bool isMsgIsRushTakeOver(u32 msg) {
        return msg == ACTMES_IS_RUSH_TAKEOVER;
    }

    bool isMsgFloorTouch(u32 msg) {
        return msg == ACTMES_FLOOR_TOUCH;
    }

    bool isMsgWallTouch(u32 msg) {
        return msg == ACTMES_WALL_TOUCH;
    }

    bool isMsgCeilTouch(u32 msg) {
        return msg == ACTMES_CEIL_TOUCH;
    }

    bool isMsgItemGet(u32 msg) {
        return msg == ACTMES_ITEM_GET;
    }

    bool isMsgItemPull(u32 msg) {
        return msg == ACTMES_ITEM_PULL;
    }

    bool isMsgItemShow(u32 msg) {
        return msg == ACTMES_ITEM_SHOW;
    }

    bool isMsgItemHide(u32 msg) {
        return msg == ACTMES_ITEM_HIDE;
    }

    bool isMsgItemStartMove(u32 msg) {
        return msg == ACTMES_ITEM_START_MOVE;
    }

    bool isMsgItemEndMove(u32 msg) {
        return msg == ACTMES_ITEM_END_MOVE;
    }

    bool isMsgInhaleBlackHole(u32 msg) {
        return msg == ACTMES_INHALE_BLACK_HOLE;
    }

    bool isMsgEnemyAttack(u32 msg) {
        return msg == ACTMES_ENEMY_ATTACK;
    }

    bool isMsgEnemyAttackFire(u32 msg) {
        return msg == ACTMES_ENEMY_ATTACK_FIRERUN;
    }

    bool isMsgEnemyAttackFireStrong(u32 msg) {
        return msg == ACTMES_ENEMY_ATTACK_FIRERUN_STRONG;
    }

    bool isMsgEnemyAttackElectric(u32 msg) {
        return msg == ACTMES_ENEMY_ATTACK_ELECTRIC;
    }

    bool isMsgExplosionAttack(u32 msg) {
        return msg == ACTMES_ENEMY_ATTACK_EXPLOSION;
    }

    bool isMsgToEnemyAttackBlow(u32 msg) {
        return msg == ACTMES_TO_ENEMY_ATTACK_BLOW;
    }

    bool isMsgToEnemyAttackTrample(u32 msg) {
        return msg == ACTMES_TO_ENEMY_ATTACK_TRAMPLE;
    }

    bool isMsgToEnemyAttackShockWave(u32 msg) {
        return msg == ACTMES_TO_ENEMY_ATTACK_SHOCK_WAVE;
    }

    bool isMsgSpinStormRange(u32 msg) {
        return msg == ACTMES_SPIN_STORM_RANGE;
    }

    bool isMsgTutorialStart(u32 msg) {
        return msg == ACTMES_TUTORIAL_START;
    }

    bool isMsgTutorialNext(u32 msg) {
        return msg == ACTMES_TUTORIAL_NEXT;
    }

    bool isMsgTutorialPrev(u32 msg) {
        return msg == ACTMES_TUTORIAL_PREV;
    }

    bool isMsgTutorialPass(u32 msg) {
        return msg == ACTMES_TUTORIAL_PASS;
    }

    bool isMsgTutorialOmit(u32 msg) {
        return msg == ACTMES_TUTORIAL_OMIT;
    }

    bool isMsgRaceReady(u32 msg) {
        return msg == ACTMES_RACE_READY;
    }

    bool isMsgRaceStart(u32 msg) {
        return msg == ACTMES_RACE_START;
    }

    bool isMsgRaceReset(u32 msg) {
        return msg == ACTMES_RACE_RESET;
    }

    bool isMsgLockOnStarPieceShoot(u32 msg) {
        return msg == ACTMES_IS_LOCKON_STAR_PIECE_SHOOT;
    }

    bool isMsgBallDashWall(u32 msg) {
        return msg == ACTMES_BALL_DASH_WALL;
    }

    bool isMsgBallDashGround(u32 msg) {
        return msg == ACTMES_BALL_DASH_GROUND;
    }

    bool isMsgStartPowerStarGet(u32 msg) {
        return msg == ACTMES_START_POWER_STAR_GET;
    }

    bool isMsgTouchPlantItem(u32 msg) {
        return msg == ACTMES_PLANT_GROUP_EMIT_ITEM;
    }

    bool isMsgHitmarkEmit(u32 msg) {
        return msg == ACTMES_HITMARK_EMIT;
    }

    bool isMsgStarPieceAttack(u32 msg) {
        return msg == ACTMES_STAR_PIECE_ATTACK;
    }

    bool isMsgStarPieceReflect(u32 msg) {
        return msg == ACTMES_IS_STAR_PIECE_REFLECT;
    }

    bool isMsgStarPieceGift(u32 msg) {
        return msg >= ACTMES_STAR_PIECE_GIFT && msg <= ACTMES_STAR_PIECE_GIFT_MAX;
    }

    s32 getNumStarPieceGift(u32 msg) {
        return (msg - ACTMES_STAR_PIECE_GIFT) + 1;
    }

    void calcPosBetweenSensors(Vec3f* pPos, const HitSensor* pSensor1, const HitSensor* pSensor2, f32 offset) {
        Vec3f dir = pSensor2->mPosition - pSensor1->mPosition;
        normalizeOrZero(&dir);

        f32 dist = pSensor1->mPosition.distance(pSensor2->mPosition);
        f32 radius2 = pSensor2->mRadius;
        f32 radius1 = pSensor1->mRadius;
        f32 value = (radius1 + radius2 - dist) / 2.0f;

        pPos->set(dir);
        *pPos *= value + pSensor1->mRadius + offset;
        pPos->add(pSensor1->mPosition);
    }

    bool tryForceKillIfMsgStartPowerStarGet(LiveActor* pActor, u32 msg) {
        if (msg == ACTMES_START_POWER_STAR_GET) {
            if (isExistEffectKeeper(pActor)) {
                forceDeleteEffectAll(pActor);
            }

            pActor->makeActorDead();

            return true;
        }

        return false;
    }

    HitSensor* addBodyMessageSensor(LiveActor* pActor, u32 type) {
        return addHitSensor(pActor, "body", type, 0, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    }
};  // namespace MR
