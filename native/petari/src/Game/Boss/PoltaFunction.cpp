#include "Game/Boss/PoltaFunction.hpp"
#include "Game/Boss/Polta.hpp"
#include "Game/Boss/PoltaArm.hpp"
#include "Game/Boss/PoltaGroundRock.hpp"
#include "Game/Boss/PoltaGroundRockHolder.hpp"
#include "Game/Boss/PoltaRock.hpp"
#include "Game/Boss/PoltaRockHolder.hpp"
#include "Game/Boss/PoltaSensorCtrl.hpp"
#include "Game/Enemy/BombTeresa.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_setBodyHP_f1, "setBodyHP.f1", 3.0f);

namespace {
    NEBULA_TUNABLE(f32, cRockCircleBaseAngle, 15.0f);
    NEBULA_TUNABLE(f32, cRockCircleAngleStep, 7.0f);
    NEBULA_TUNABLE(f32, cBombTeresaRootHeight, 120.0f);
}  // namespace

namespace PoltaFunction {
    void onMovement(Polta* pPolta) {
        MR::forceDeleteEffectAll(pPolta);
        MR::requestMovementOn(pPolta);
        MR::requestMovementOn(pPolta->mBreakModel);
        MR::requestMovementOn(pPolta->mFormationModel);
        MR::requestMovementOn(pPolta->mLeftArm);
        MR::requestMovementOn(pPolta->mLeftArm->mBreakModel);
        MR::requestMovementOn(pPolta->mLeftArm->mFormationModel);
        MR::requestMovementOn(pPolta->mRightArm);
        MR::requestMovementOn(pPolta->mRightArm->mBreakModel);
        MR::requestMovementOn(pPolta->mRightArm->mFormationModel);
    }

    PoltaArm* getLeftArmActor(Polta* pPolta) {
        return pPolta->mLeftArm;
    }

    PoltaArm* getRightArmActor(Polta* pPolta) {
        return pPolta->mRightArm;
    }

    bool isCoreSensor(const Polta* pPolta, const HitSensor* pSensor) {
        return pPolta->mSensorCtrl->isCoreSensor(pSensor);
    }

    bool isBodySensor(const Polta* pPolta, const HitSensor* pSensor) {
        return pPolta->mSensorCtrl->isBodySensor(pSensor);
    }

    bool isArmSensor(const Polta* pPolta, const HitSensor* pSensor) {
        return isLeftArmSensor(pPolta, pSensor) || isRightArmSensor(pPolta, pSensor);
    }

    bool isLeftArmSensor(const Polta* pPolta, const HitSensor* pSensor) {
        return pPolta->mSensorCtrl->isLeftArmSensor(pSensor);
    }

    bool isRightArmSensor(const Polta* pPolta, const HitSensor* pSensor) {
        return pPolta->mSensorCtrl->isRightArmSensor(pSensor);
    }

    bool isHeadSensor(const Polta* pPolta, const HitSensor* pSensor) {
        return pPolta->mSensorCtrl->isHeadSensor(pSensor);
    }

    void setScreamSensorSize(Polta* pPolta, f32 screamSensorSize) {
        pPolta->mSensorCtrl->setScreamSensorSize(screamSensorSize);
    }

    bool attackScreamSensor(Polta* pPolta, HitSensor* pSender, HitSensor* pReceiver) {
        if (pPolta->mSensorCtrl->isScreamSensor(pSender)) {
            if (!MR::isSensorPlayer(pReceiver)) {
                MR::sendMsgToEnemyAttackShockWave(pReceiver, pSender);
            }

            return true;
        }

        return false;
    }

    void startAction(Polta* pPolta, const char* pActionName, bool isAffectBody) {
        if (isAffectBody) {
            MR::startAction(pPolta, pActionName);
        }

        pPolta->mLeftArm->requestControlled(pActionName);
        pPolta->mRightArm->requestControlled(pActionName);
    }

    void startBckBody(Polta* pPolta, const char* pActionName) {
        MR::startAction(pPolta, pActionName);
    }

    void damageLeftArm(Polta* pPolta) {
        pPolta->mLeftArm->requestDamage();
    }

    void damageRightArm(Polta* pPolta) {
        pPolta->mRightArm->requestDamage();
    }

    void requestStartControllArm(Polta* pPolta) {
        pPolta->mLeftArm->requestStartControll();
        pPolta->mRightArm->requestStartControll();
    }

    void startArm(Polta* pPolta) {
        pPolta->mLeftArm->start();
        pPolta->mRightArm->start();
    }

    void breakLeftArm(Polta* pPolta) {
        pPolta->mLeftArm->requestBreakBody();
    }

    void breakRightArm(Polta* pPolta) {
        pPolta->mRightArm->requestBreakBody();
    }

    void killLeftArm(Polta* pPolta) {
        pPolta->mLeftArm->kill();
    }

    void killRightArm(Polta* pPolta) {
        pPolta->mRightArm->kill();
    }

    bool isBrokenLeftArm(Polta* pPolta) {
        return pPolta->mLeftArm->isBroken();
    }

    bool isBrokenRightArm(Polta* pPolta) {
        return pPolta->mRightArm->isBroken();
    }

    bool isEnableAttackRightArm(Polta* pPolta) {
        return pPolta->mRightArm->isEnableAttack();
    }

    bool isEnableAttackLeftArm(Polta* pPolta) {
        return pPolta->mLeftArm->isEnableAttack();
    }

    bool isMaxGenerateBombTeresa(Polta* pPolta) {
        return pPolta->mBombTeresaHolder->getDeadActor() == nullptr;
    }

    s32 getCountDeadGroundRock(Polta* pPolta) {
        return pPolta->mGroundRockHolder->getObjNum() - pPolta->mGroundRockHolder->getLivingActorNum();
    }

    bool appearGroundRock(Polta* pPolta, f32 distance, f32 angle) {
        PoltaGroundRockHolder* pHolder = pPolta->mGroundRockHolder;
        PoltaGroundRock* pRock = pHolder->getDeadMember();
        if (pRock == nullptr) {
            return false;
        }

        Vec3f direction(*MR::getPlayerPos());
        const Vec3f& rGravity = pPolta->mGravity;
        direction.sub(pPolta->mPosition);
        direction.orthogonalize(rGravity);

        if (MR::normalizeOrZero(&direction)) {
            direction = pPolta->mFront;
        }

        MR::rotateVecDegree(&direction, pPolta->mGravity, angle);
        Vec3f position;
        position.scaleAdd(distance, direction, pPolta->mPosition);

        pRock->start(pPolta, position);
        return true;
    }

    // Rocks alternate sides of Polta's front, each pair further round: index 0 at -22 degrees, 1 at +29, 2 at -36 and so on.
    // The offset is the rock's float point in Polta's base frame.
    bool appearRockCircle(Polta* pPolta, const Vec3f& rCenter, f32 radius, s32 index, PoltaRock::Color color) {
        PoltaRock* const pRock = pPolta->mRockHolder->getDeadMember();
        if (pRock == nullptr) {
            return false;
        }

        pRock->setColor(color);

        Vec3f direction(pPolta->mFront);
        f32 sign = index % 2 ? 1.0f : -1.0f;
        f32 angle = sign * (::cRockCircleBaseAngle + ::cRockCircleAngleStep * (index + 1));
        MR::rotateVecDegree(&direction, pPolta->mGravity, angle);
        direction.orthogonalize(pPolta->mGravity);
        MR::normalizeOrZero(&direction);

        Vec3f position;
        position.scaleAdd(radius, direction, rCenter);
        Vec3f offset(radius * -MR::sinDegree(angle), 0.0f, radius * MR::cosDegree(angle));

        pRock->start(pPolta, position, offset);
        return true;
    }

    // Bomb boos come out cBombTeresaRootHeight above Polta's root.
    bool appearBombTeresaFromRoot(Polta* pPolta) {
        Vec3f position;
        position.scaleAdd(-::cBombTeresaRootHeight, pPolta->mGravity, pPolta->mPosition);
        BombTeresa* pBombTeresa = pPolta->mBombTeresaHolder->getDeadMember();
        if (pBombTeresa == nullptr) {
            return false;
        }

        pBombTeresa->appearShadow(position, Vec3f(0.0f, 0.0f, 0.0f));
        return true;
    }

    bool appearBombTeresaNormal(Polta* pPolta, const Vec3f& rPosition, const Vec3f& rVelocity) {
        BombTeresa* pBombTeresa = pPolta->mBombTeresaHolder->getDeadMember();
        if (pBombTeresa == nullptr) {
            return false;
        }

        pBombTeresa->appearNormal(rPosition, rVelocity);
        return true;
    }

    void disperseBombTeresa(Polta* pPolta) {
        pPolta->mBombTeresaHolder->disperseAll();
    }

    void breakGroundRock(Polta* pPolta) {
        pPolta->mGroundRockHolder->breakAll();
    }

    void killBombTeresa(Polta* pPolta) {
        pPolta->mBombTeresaHolder->killAll();
    }

    void killPoltaRock(Polta* pPolta) {
        pPolta->mRockHolder->killAll();
    }

    void killGroundRock(Polta* pPolta) {
        pPolta->mGroundRockHolder->killAll();
    }

    void setBodyHP(Polta* pPolta, s32 hp) {
        MR::startBva(pPolta, "BreakLevel");
        MR::setBvaFrameAndStop(pPolta, cNum_setBodyHP_f1 - hp);

        switch (hp) {
        case 3:
            MR::deleteEffect(pPolta, "DamageSmoke1");
            MR::deleteEffect(pPolta, "DamageSmoke2");
            break;
        case 2:
            MR::emitEffect(pPolta, "DamageSmoke1");
            MR::deleteEffect(pPolta, "DamageSmoke2");
            break;
        case 1:
            MR::emitEffect(pPolta, "DamageSmoke1");
            MR::emitEffect(pPolta, "DamageSmoke2");
            break;
        case 0:
        default:
            MR::deleteEffect(pPolta, "DamageSmoke1");
            MR::deleteEffect(pPolta, "DamageSmoke2");
            break;
        }
    }
};  // namespace PoltaFunction
