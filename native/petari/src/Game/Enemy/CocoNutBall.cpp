#include "Game/Enemy/CocoNutBall.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/CocoNut.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MathUtil.hpp"

namespace {
    const Vec cReboundVelocity = {0.0f, 15.0f, 5.0f};
};  // namespace

namespace NrvCocoNutBall {
    NEW_NERVE(CocoNutBallNrvThrow, CocoNutBall, Throw);
    NEW_NERVE(CocoNutBallNrvHitBackToHost, CocoNutBall, HitBackToHost);
    NEW_NERVE(CocoNutBallNrvHitBackToPlayer, CocoNutBall, HitBackToPlayer);
    NEW_NERVE(CocoNutBallNrvRebound, CocoNutBall, Rebound);
    NEW_NERVE(CocoNutBallNrvFreeze, CocoNutBall, Freeze);
    NEW_NERVE(CocoNutBallNrvFreezeRelease, CocoNutBall, FreezeRelease);
};  // namespace NrvCocoNutBall

CocoNutBall::CocoNutBall(const char* pName)
    : LiveActor(pName), mHost(), mHitBackGravity(0.0f, -1.0f, 0.0f), mRallyNum(), mFreezeStep(), mFreezePos(gZeroVec), mFreezeVelocity(gZeroVec), mIsThrowRight(), mIsHeightLimited(), mHeightMax(450.0f), mHeightMin(),
      mUp(0.0f, 1.0f, 0.0f), mKillDistance(10000.0f), mIsRallyBgm() {
}

inline void CocoNutBall::initSensors(const char* const hit1, const char* const hit2) {
    initHitSensor(2);
    MR::addHitSensor(this, hit1, ATYPE_COCO_NUT, 8, 40.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensor(this, hit2, ATYPE_COCO_NUT, 8, 500.0f, Vec3f(0.0f, 0.0f, 0.0f));

}

void CocoNutBall::init(const TableIter& rIter) {
    initModelManagerWithAnm(CocoNut::getModelName(), nullptr, false);
    MR::connectToSceneNoSilhouettedMapObjStrongLight(this);
    MR::initLightCtrl(this);
    initSensors("body", "bind");
    initBinder(60.0f, 0.0f, 0);
    initEffectKeeper(0, "CocoNut");
    MR::initStarPointerTarget(this, 150.0f, Vec3f(0, 0, 0));
    initSound(4, false);
    MR::initShadowVolumeCylinder(this, 60.0f);
    MR::invalidateClipping(this);
    initNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvThrow));
    makeActorDead();
}

void CocoNutBall::appear() {
    LiveActor::appear();
    mRallyNum = 0;
    MR::onBind(this);
    setNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvThrow));
}

void CocoNutBall::kill() {
    MR::forceDeleteEffect(this, "CocoNutBlur");
    MR::forceDeleteEffect(this, "CocoNutLight");
    MR::startSound(this, "SE_OJ_COCONUT_BALL_BREAK");

    if (mRallyNum > 0) {
        MR::sendArbitraryMsg(ACTMES_RUSH_END, mHost->getSensor("body"), getSensor("body"));
    }

    LiveActor::kill();
    MR::emitEffect(this, CocoNut::getBreakEffectName());
}

void CocoNutBall::appearAndThrow(const Vec3f& rAppearPos, f32 f1) {
    mPosition.set(rAppearPos);
    MR::calcGravity(this);
    mUp.negate(mGravity);
    setVelocityToPlayer(15.0f, f1);
    mIsThrowRight = MR::isHalfProbability();
    appear();
}

void CocoNutBall::hitBackToPlayer() {
    f32 rand = MR::getRandom(12.5f, 17.5f);

    if (mIsThrowRight) {
        rand = -rand;
        mIsThrowRight = false;
    } else {
        mIsThrowRight = true;
    }

    setVelocityToPlayer(3.0f * mRallyNum + 15.0f, rand);
    setNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvHitBackToPlayer));
}

void CocoNutBall::demoBreak(const Vec3f& rPos) {
    mPosition.set(rPos);
    mRotation.zero();
    mVelocity.zero();

    Pos3f TRMtx;
    MR::makeMtxTR(TRMtx.toMtxPtr(), this);
    MR::setBaseTRMtx(this, TRMtx);

    MR::emitEffect(this, "Hit");
    MR::startSound(this, "SE_OJ_COCONUT_BALL_BREAK");
    MR::emitEffect(this, CocoNut::getBreakEffectName());
}

void CocoNutBall::calcAndSetBaseMtx() {
    if (!MR::isNearZero(mVelocity)) {
        Vec3f velocityDirection;
        MR::normalize(mVelocity, &velocityDirection);
        Pos3f baseTRMtx;
        MR::makeMtxFrontUpPos(&baseTRMtx, velocityDirection, mUp, mPosition);
        MR::setBaseTRMtx(this, baseTRMtx);
    }
}

bool CocoNutBall::isSensorBody(HitSensor* pSensor) const {
    return pSensor == getSensor("body");
}

void CocoNutBall::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isSensorBody(pSender) && MR::isSensorPlayer(pReceiver)) {
        if (isNerveTrowToOrFreeze() && MR::sendArbitraryMsg(ACTMES_ENEMY_ATTACK_FLIP_VERYWEAK, pReceiver, pSender)) {
            setNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvRebound));
            return;
        }
    }

    if (MR::isSensorEnemy(pReceiver) && isSensorBody(pSender)) {
        if (pReceiver->mHost == mHost) {
            if (isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvHitBackToHost))) {
                if (MR::sendMsgEnemyAttack(pReceiver, pSender)) {
                    MR::emitEffect(this, "Hit");
                    kill();
                }
            }

            return;
        }

        if (isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvThrow)) || isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvHitBackToHost)) ||
            isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvHitBackToPlayer))) {
            if (MR::sendMsgEnemyAttack(pReceiver, pSender)) {
                kill();
            }
        }
    }
}

bool CocoNutBall::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgPlayerSpinAttack(msg)) {
        if (isValidReceivePunch()) {
            setNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvHitBackToHost));
            return true;
        }
    } else if (isSensorBody(pReceiver) && MR::isMsgStarPieceReflect(msg)) {
        return true;
    }

    return false;
}

HitSensor* CocoNutBall::isBindedAny() const {
    if (MR::isBindedGround(this)) {
        return MR::getGroundSensor(this);
    }

    if (MR::isBindedWall(this)) {
        return MR::getWallSensor(this);
    }

    if (MR::isBindedRoof(this)) {
        return MR::getRoofSensor(this);
    }

    return nullptr;
}

bool CocoNutBall::isNerveTrowToOrFreeze() const {
    return isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvThrow)) || isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvHitBackToPlayer)) ||
           isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvFreeze)) || isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvFreezeRelease));
}

bool CocoNutBall::isValidReceivePunch() const {
    if (MR::isDead(this) || (!isNerveTrowToOrFreeze() && !isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvRebound)))) {
        return false;
    } else {
        return mPosition.distance(*MR::getPlayerCenterPos()) < 400.0f;
    }
}

void CocoNutBall::calcHitBackVelocitAndGravity() {
    bool hitBackRight = isHitBackRight();
    bool hitBackFront = isHitBackFront();
    Vec3f hitBackDstPos;
    calcHitBackDstPos(&hitBackDstPos, hitBackRight, hitBackFront);

    Vec3f dir;
    dir.sub(hitBackDstPos, mPosition);
    Vec3f cross;
    Vec3f scaled;
    scaled.scale(mUp.dot(dir), mUp);
    MR::vecKillElement(dir, mUp, &dir);
    f32 f1 = dir.length() / 42.0f;
    MR::normalize(&dir);
    cross.cross(mUp, dir);
    MR::normalize(&cross);
    mHitBackGravity.scale(2.2f, mGravity);
    Vec3f scaled2;
    scaled2.scale(42.0f, dir);
    Vec3f verticalVelocity;
    verticalVelocity = (scaled.scaleInline(2.0f) - mHitBackGravity.scaleInline(f1).scaleInline(f1)).scaleInline(1.0f / (2.0f * f1));
    mVelocity.add(scaled2, verticalVelocity);

    if (!hitBackFront) {
        f32 scaleFactor = (hitBackRight ? 1.2f : -1.2f);
        Vec3f scaled4;
        scaled4.scale(scaleFactor * f1 * f1 / (f1 * 2.0f), cross);
        mVelocity.add(scaled4);
        scaled4.scale(scaleFactor, cross);
        mHitBackGravity.sub(scaled4);
    }
}

bool CocoNutBall::isHitBackRight() const {
    Vec3f vec1;
    vec1.sub(mHost->mPosition, *MR::getPlayerPos());

    Vec3f vec2;
    vec2.sub(mPosition, *MR::getPlayerPos());

    MR::vecKillElement(vec1, mUp, &vec1);
    MR::vecKillElement(vec2, mUp, &vec2);

    MR::normalize(&vec1);
    MR::normalize(&vec2);

    Vec3f cross = vec1.cross(vec2);

    return 0.0f < mUp.dot(cross);
}

bool CocoNutBall::isHitBackFront() const {
    Vec3f vec1;
    vec1.sub(*MR::getPlayerPos(), mHost->mPosition);

    Vec3f vec2;
    vec2.sub(mPosition, *MR::getPlayerPos());

    MR::vecKillElement(vec1, mUp, &vec1);
    MR::vecKillElement(vec2, mUp, &vec2);

    MR::normalize(&vec1);
    MR::normalize(&vec2);

    return vec1.dot(vec2) < -0.95f;
}

void CocoNutBall::calcHitBackDstPos(Vec3f* pOut, bool a1, bool a2) {
    Vec3f vec1(0.0f, 0.0f, 0.0f);
    if (!a2) {
        Vec3f cross;
        Vec3f vec2;

        vec2.sub(mHost->mPosition, *MR::getPlayerPos());

        MR::vecKillElement(vec2, mUp, &vec2);
        MR::normalize(&vec2);

        cross.cross(mUp, vec2);
        MR::vecKillElement(cross, mUp, &cross);
        MR::normalize(&cross);

        vec1.scale(a1 ? 150.0f : -150.0f, cross);
    }

    Vec3f scaled;

    scaled.scale(100.0f, mUp);
    vec1.add(scaled);

    pOut->add(mHost->getSensor("body")->mPosition, vec1);
}

bool CocoNutBall::tryToKill(bool alwaysKill) {
    HitSensor* bindedSensor = isBindedAny();

    if (bindedSensor != nullptr) {
        if (bindedSensor->isType(ATYPE_PUNCH_BOX) && !isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvRebound))) {
            MR::sendMsgEnemyAttack(bindedSensor, getSensor("body"));
        }

        kill();
        return true;
    }

    if (alwaysKill) {
        kill();
        return true;
    }

    return false;
}

void CocoNutBall::setVelocityToPlayer(f32 f1, f32 f2) {
    Vec3f vec1;

    if (!mIsHeightLimited) {
        vec1.scale(120.0f, mUp);
        vec1.add(*MR::getPlayerPos());
    } else {
        vec1.set(*MR::getPlayerPos());
    }

    Pos3f rotate;
    rotate.makeRotate(mUp, MR::toRadian(f2));
    Vec3f vec2;
    vec2.sub(vec1, mPosition);
    rotate.mult33(vec2);
    vec1.add(mPosition, vec2);

    if (mIsHeightLimited) {
        f32 minY = mHeightMin + mHost->mPosition.y;
        f32 val2 = mHeightMax + mHost->mPosition.y;

        vec1.y = MR::max(vec1.y, minY);

        f32 flt = 120.0f;
        bool v1 = false;
        while (vec1.y < val2 - flt) {
            vec2.sub(vec1, mPosition);
            vec2.scale(1.5f);

            if (!MR::getFirstPolyOnLineToMap(nullptr, nullptr, mPosition, vec2)) {
                v1 = true;
                vec1.y += flt;
                break;
            }

            vec1.y += 60.0f;
        }

        if (!v1) {
            vec1.y = val2;
        }
    }

    vec2.sub(vec1, mPosition);
    mVelocity.setLength(vec2, f1);
}

void CocoNutBall::freeze() {
    mFreezeStep = 0;
    mFreezePos.set(mPosition);
    mFreezeVelocity.set(mVelocity);
    setNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvFreeze));
}

bool CocoNutBall::isFreezable() {
    return !MR::isHiddenModel(this) && MR::isStarPointerPointing2POnPressButton(this, "弱", true, false);
}

void CocoNutBall::processApproachToPlayer() {
    if (MR::changeShowModelFlagSyncNearClipping(this, 300.0f)) {
        MR::emitEffect(this, "CocoNutLight");
    } else {
        MR::deleteEffect(this, "CocoNutLight");
    }

    if (isNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvFreezeRelease)) || MR::isGreaterStep(this, 20)) {
        if (isFreezable()) {
            freeze();
            return;
        }
    }

    if (tryToKill(!MR::isNear(this, mHost, mKillDistance))) {
        return;
    }
}

void CocoNutBall::exeThrow() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "SpinX");
        MR::emitEffect(this, "CocoNutLight");

        if (mIsRallyBgm) {
            MR::startSound(this, "SE_BM_OTAKING_SPIT_RALLY_BALL");
        } else {
            MR::startSound(this, "SE_OJ_COCONUT_BALL_SPIT_OUT");
        }
    }

    processApproachToPlayer();
}

void CocoNutBall::exeHitBackToHost() {
    if (MR::isFirstStep(this)) {
        MR::setBckRate(this, 1.0f);
        MR::deleteEffect(this, "Touch");
        MR::emitEffect(this, "CocoNutBlur");
        MR::emitEffect(this, "CocoNutLight");
        MR::emitEffect(this, "SpinHitMark");
        MR::offBind(this);
        MR::startSound(this, "SE_OJ_COCONUT_BALL_VOLLEY");
        MR::startSpinHitSound(this);

        if (mIsRallyBgm && MR::isPlayingStageBgm()) {
            if (mRallyNum == 0) {
                MR::startSystemME("ME_RALLY_COMBO_FIRST");
            } else {
                MR::startSystemME("ME_RALLY_COMBO_SECOND");
            }
        }

        mRallyNum++;
        calcHitBackVelocitAndGravity();
    }

    if (MR::isStep(this, 1)) {
        MR::stopScene(6);
    }

    if (MR::isStep(this, 2)) {
        MR::shakeCameraWeak();
    }

    if (MR::isGreaterEqualStep(this, 1)) {
        mVelocity.add(mHitBackGravity);
    }

    if (tryToKill(!MR::isNearPlayer(this, 5000.0f))) {
        return;
    }
}

void CocoNutBall::exeHitBackToPlayer() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "Hit");
        MR::startSound(this, "SE_OJ_COCONUT_BALL_VOLLEY");

        if (mIsRallyBgm && MR::isPlayingStageBgm()) {
            MR::startSystemME("ME_RALLY_COMBO_SECOND");
        }

        MR::onBind(this);
    }

    processApproachToPlayer();
}

void CocoNutBall::exeRebound() {
    if (MR::isFirstStep(this)) {
        MR::setBckRate(this, 0.25f);
        MR::deleteEffect(this, "CocoNutBlur");
        MR::deleteEffect(this, "CocoNutLight");

        Vec3f vec1;
        vec1.sub(mPosition, *MR::getPlayerPos());

        Pos3f pos;
        pos.identity();

        MR::makeMtxUpFront(&pos, mUp, vec1);

        mVelocity.set(::cReboundVelocity);

        pos.mult33(mVelocity);
    }

    Vec3f twoGravity;
    twoGravity.scale(2.0f, mGravity);
    mVelocity.add(twoGravity);

    if (tryToKill(!MR::isNearPlayer(this, 5000.0f))) {
        return;
    }
}

void CocoNutBall::exeFreeze() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();

        MR::deleteEffect(this, "CocoNutBlur");
        MR::deleteEffect(this, "CocoNutLight");
        MR::emitEffect(this, "Touch");

        if (mFreezeStep == 0) {
            MR::startPointerHitSound();
        }
    }

    mFreezeStep++;
    MR::startPointerFreezeLevelSound(this);

    f32 cos = MR::cosDegree(MR::repeatDegree(mFreezeStep * 75.0f));

    f32 scaleFactor = ((7.5f * cos) * (20 - getNerveStep())) / 20.0f;

    Vec3f camXDirScaled;
    camXDirScaled.set(MR::getCamXdir());
    camXDirScaled.scale(scaleFactor);
    mPosition.add(mFreezePos, camXDirScaled);

    if (MR::changeShowModelFlagSyncNearClipping(this, 300.0f)) {
        MR::emitEffect(this, "Touch");
    } else {
        MR::deleteEffect(this, "Touch");
    }

    if (isFreezable()) {
        // resets nerve step every frame i guess
        setNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvFreeze));
    } else if (MR::isStep(this, 20)) {
        setNerve(GET_NERVE(CocoNutBall, CocoNutBallNrvFreezeRelease));
    }
}

void CocoNutBall::exeFreezeRelease() {
    if (MR::isFirstStep(this)) {
        mPosition.set(mFreezePos);
        mVelocity.set(mFreezeVelocity);
        MR::deleteEffect(this, "Touch");
        MR::emitEffect(this, "CocoNutLight");
    }

    processApproachToPlayer();
}
