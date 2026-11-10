#include "Game/Enemy/Jiraira.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeExplode_f1, "exeExplode.f1", 800.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeExplode_f2, "exeExplode.f2", 1200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeExplode_f3, "exeExplode.f3", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeExplode_f4, "exeExplode.f4", 8.0f);

namespace {
    NEBULA_TUNABLE(s32, hPreRecoverTime, 188);
    static const s32 hBeginExplodeTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(s32, hExplodeTime, 8);
    static const s32 hExplodeInvalidTime = NebulaFrameClock::stepsOfSeconds(2);
};  // namespace

namespace NrvJiraira {
    NEW_NERVE(HostTypeNrvWait, Jiraira, Wait);
    NEW_NERVE(HostTypeNrvStepped, Jiraira, Stepped);
    NEW_NERVE(HostTypeNrvPreRecover, Jiraira, PreRecover);
    NEW_NERVE(HostTypeNrvRecover, Jiraira, Recover);
    NEW_NERVE(HostTypeNrvSteppedExplode, Jiraira, Explode);
    NEW_NERVE(HostTypeNrvExplode, Jiraira, Explode);
};  // namespace NrvJiraira

Jiraira::Jiraira(const char* pName) : LiveActor(pName), mBodyCollision(), mExplodeRadius(500.0f) {
}

void Jiraira::init(const TableIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        MR::initDefaultPos(this, rIter);
        s32 explodeRadius = mExplodeRadius;
        MR::getPlacementArg1NoInit(rIter, &explodeRadius);
        mExplodeRadius = explodeRadius;
        MR::useStageSwitchWriteDead(this, rIter);
    }
    initModelManagerWithAnm("Jiraira", nullptr, false);
    MR::connectToSceneCollisionEnemy(this);
    initHitSensor(2);
    f32 scaleY = mScale.y;
    Vec3f offset;
    offset.x = 0.0f;
    offset.y = cNum_init_f1;
    offset.z = 0.0f;
    MR::addHitSensorMapObjMoveCollision(this, "body", 16, scaleY * cNum_init_f2, offset);
    Vec3f offset2;
    offset2.x = 0.0f;
    offset2.y = cNum_init_f3 * scaleY;
    offset2.z = 0.0f;
    MR::addHitSensorEnemyAttack(this, "explode", 16, (mExplodeRadius * scaleY), offset2);
    MR::initCollisionPartsAutoEqualScaleOne(this, "Jiraira", getSensor("body"), MR::getJointMtx(this, "Jiraira"));
    CollisionParts* colPart = MR::createCollisionPartsFromLiveActor(this, "Button", getSensor("body"), MR::getJointMtx(this, "Button"),
                                                                    MR::CollisionScaleType_NotUsingScale);
    mBodyCollision = colPart;
    MR::validateCollisionParts(colPart);
    initSound(6, false);
    initEffectKeeper(0, nullptr);
    initNerve(GET_NERVE(Jiraira, HostTypeNrvWait));
    MR::invalidateClipping(this);
    appear();
}

void Jiraira::appear() {
    LiveActor::appear();
    getSensor("explode")->invalidate();
}

void Jiraira::kill() {
    LiveActor::kill();
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }
}

void Jiraira::control() {
    mBodyCollision->setMtx();
}

void Jiraira::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
        MR::startBrk(this, "Wait");
    }

    if (MR::isOnPlayer(getSensor("body"))) {
        setNerve(GET_NERVE(Jiraira, HostTypeNrvStepped));
    }
}

void Jiraira::exeStepped() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Stepped");
        MR::startBrk(this, "Stepped");
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::startSound(this, "SE_OJ_JIRAIRA_STEPPED");
    }

    MR::startLevelSound(this, "SE_OJ_LV_JIRAIRA_CHARGE");

    if (MR::isGreaterStep(this, ::hBeginExplodeTime)) {
        setNerve(GET_NERVE(Jiraira, HostTypeNrvSteppedExplode));
    }
}

void Jiraira::exeExplode() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "Explosion");
        MR::startSound(this, "SE_OJ_JIRAIRA_EXPLODE");
        MR::startBck(this, "Down");
        MR::startBrk(this, "Down");
        getSensor("explode")->validate();
        MR::tryRumblePadAndCameraDistanceStrong(this, cNum_exeExplode_f1, cNum_exeExplode_f2, cNum_exeExplode_f3);
    }

    if (MR::isStep(this, ::hExplodeTime)) {
        getSensor("explode")->invalidate();
    } else if (MR::isLessStep(this, ::hExplodeTime)) {
        getSensor("explode")->mRadius = mExplodeRadius * getNerveStep() / cNum_exeExplode_f4;
    }

    if (MR::isGreaterStep(this, ::hPreRecoverTime)) {
        setNerve(GET_NERVE(Jiraira, HostTypeNrvPreRecover));
    }
}

void Jiraira::exePreRecover() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Down");
        MR::startBrk(this, "RecoveryLoop");
    }

    MR::startLevelSound(this, "SE_OJ_LV_JIRAIRA_RECOVERING");

    if (MR::isGreaterStep(this, ::hExplodeInvalidTime)) {
        setNerve(GET_NERVE(Jiraira, HostTypeNrvRecover));
    }
}

void Jiraira::exeRecover() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Recovery");
        MR::startBrk(this, "Recovery");
        MR::startSound(this, "SE_OJ_JIRAIRA_RECOVER");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Jiraira, HostTypeNrvWait));
    }
}

void Jiraira::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (getSensor("explode") == pSender &&
        !MR::isExistMapCollisionExceptActor(pSender->mPosition, pReceiver->mPosition - pSender->mPosition, pReceiver->mHost)) {
        if (MR::isSensorPlayer(pReceiver)) {
            MR::sendMsgEnemyAttackExplosion(pReceiver, pSender);
        } else if (MR::isSensorEnemy(pReceiver)) {
            if (!isNerve(GET_NERVE(Jiraira, HostTypeNrvSteppedExplode))) {
                MR::sendMsgEnemyAttackExplosion(pReceiver, pSender);
            }
        } else if (MR::isSensorMapObj(pReceiver)) {
            if (!isNerve(GET_NERVE(Jiraira, HostTypeNrvSteppedExplode))) {
                MR::sendMsgEnemyAttackExplosion(pReceiver, pSender);
            }
        }
    }
}

bool Jiraira::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (isNerve(GET_NERVE(Jiraira, HostTypeNrvExplode)) || isNerve(GET_NERVE(Jiraira, HostTypeNrvSteppedExplode)) ||
        isNerve(GET_NERVE(Jiraira, HostTypeNrvPreRecover))) {
        return false;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        if (!isNerve(GET_NERVE(Jiraira, HostTypeNrvStepped))) {
            setNerve(GET_NERVE(Jiraira, HostTypeNrvStepped));
        }

        return true;
    }

    return false;
}

bool Jiraira::receiveMsgEnemyAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (isNerve(GET_NERVE(Jiraira, HostTypeNrvExplode)) || isNerve(GET_NERVE(Jiraira, HostTypeNrvSteppedExplode))) {
        return false;
    }

    if (MR::isMsgExplosionAttack(msg)) {
        setNerve(GET_NERVE(Jiraira, HostTypeNrvExplode));
        return true;
    }

    return false;
}
