#include "Game/Enemy/RingBeam.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/AudioLib/AudAnmSoundObject.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>

namespace NrvRingBeam {
    NEW_NERVE(RingBeamNrvSpread, RingBeam, Spread);
};  // namespace NrvRingBeam

RingBeamShadowDrawer::RingBeamShadowDrawer(const LiveActor* pHost) : ShadowVolumeDrawer("影描画[リングビーム]") {
    mHost = pHost;
    mRadius = 0.0f;
}

void RingBeamShadowDrawer::drawShape(TypedDraw& rDraw) const {
    Pos3f MtxList[32];
    Pos3f baseMtxCopy;
    Mtx mtx;
    Vec3f temp;
    Vec3f temp2;
    Vec3f temp3;
    Vec3f listXDirScaledPos;
    Vec3f XDirScaled;
    Vec3f listXDirScaledNeg;
    Vec3f negYDirScaled;
    Vec3f XDir;
    Vec3f YDir;
    Vec3f Trans;
    Vec3f listXDir;
    Vec3f listYDir;
    f32 f1 = 0.2026834f;

    baseMtxCopy.setInline(mHost->getBaseMtx());
    baseMtxCopy.getXDir(XDir);
    baseMtxCopy.getYDir(YDir);
    baseMtxCopy.getTrans(Trans);
    NebulaMtxRotAxisRad(mtx, &YDir, f1);
    XDirScaled.scale(mRadius, XDir);
    f32 f2 = 0.0f;
    for (int i = 0; i < 32; i++) {
        temp.add(Trans, XDirScaled);
        MR::calcGravityVector(mHost, temp, &temp2, nullptr, 0);
        if (MR::normalizeOrZero(XDirScaled, &temp3)) {
            MtxList[i].setInline(baseMtxCopy);
        } else if (MR::isSameDirection(-temp2, temp3)) {
            MR::makeMtxUpSide(&MtxList[i], -temp2, -YDir);
        } else if (temp2.dot(YDir) < f2) {
            MR::makeMtxUpSide(&MtxList[i], -temp2, temp3);
        } else {
            MR::makeMtxUpSide(&MtxList[i], -temp2, -temp3);
        }
        // almost certainly an inline
        MtxList[i][0][3] = temp.x;
        MtxList[i][1][3] = temp.y;
        MtxList[i][2][3] = temp.z;

        NebulaMtxMultVec(mtx, &XDirScaled, &XDirScaled);
    }

    VertexP* vtx = rDraw.vertices< VertexP >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 64);
    for (int i = 0; i < 32; i++) {
        MtxList[i].getXDir(listXDir);
        MtxList[i].getTrans(Trans);
        listXDirScaledNeg.scale(-5.0f, listXDir);
        listXDirScaledPos.scale(5.0f, listXDir);
        temp.add(Trans, listXDirScaledNeg);
        temp2.add(Trans, listXDirScaledPos);
        *vtx++ = {temp2.x, temp2.y, temp2.z};
        *vtx++ = {temp.x, temp.y, temp.z};
    }

    vtx = rDraw.vertices< VertexP >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 64);
    for (int i = 0; i < 32; i++) {
        MtxList[i].getXDir(listXDir);
        MtxList[i].getYDir(listYDir);
        MtxList[i].getTrans(Trans);

        listXDirScaledNeg.scale(-5.0f, listXDir);
        listXDirScaledPos.scale(5.0f, listXDir);
        negYDirScaled.scale(500.0f, -listYDir);
        temp.add(Trans, listXDirScaledNeg);
        temp2.add(Trans, listXDirScaledPos);
        temp.add(negYDirScaled);
        temp2.add(negYDirScaled);
        *vtx++ = {temp.x, temp.y, temp.z};
        *vtx++ = {temp2.x, temp2.y, temp2.z};
    }

    vtx = rDraw.vertices< VertexP >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 64);
    for (int i = 0; i < 32; i++) {
        MtxList[i].getXDir(listXDir);
        MtxList[i].getYDir(listYDir);
        MtxList[i].getTrans(Trans);

        listXDirScaledNeg.scale(-5.0f, listXDir);
        negYDirScaled.scale(500.0f, -listYDir);
        temp.add(Trans, listXDirScaledNeg);
        temp2.add(Trans, listXDirScaledNeg);
        temp2.add(negYDirScaled);
        *vtx++ = {temp.x, temp.y, temp.z};
        *vtx++ = {temp2.x, temp2.y, temp2.z};
    }

    vtx = rDraw.vertices< VertexP >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 64);
    for (int i = 0; i < 32; i++) {
        MtxList[i].getXDir(listXDir);
        MtxList[i].getYDir(listYDir);
        MtxList[i].getTrans(Trans);

        listXDirScaledPos.scale(5.0f, listXDir);
        negYDirScaled.scale(500.0f, -listYDir);
        temp.add(Trans, listXDirScaledPos);
        temp2.add(Trans, listXDirScaledPos);
        temp2.add(negYDirScaled);
        *vtx++ = {temp2.x, temp2.y, temp2.z};
        *vtx++ = {temp.x, temp.y, temp.z};
    }
}

void RingBeamShadowDrawer::loadModelDrawMtx(TypedDraw& rDraw) const {
    rDraw.setWorld(nullptr);
};

bool RingBeamShadowDrawer::isDraw() const {
    return MR::isValidDraw(mHost);
}

RingBeam::RingBeam(const char* pName, LiveActor* pHost, bool isFollowGravity, bool hasShadow) : LiveActor(pName) {
    mHost = pHost;
    mSpeed = 20.0f;
    mLife = 100;
    mIsFollowGravity = isFollowGravity;
    mHasShadow = hasShadow;
    mShadowDrawer = nullptr;
    mShadowModel = nullptr;
}

void RingBeam::setSpeed(f32 speed) {
    mSpeed = speed;
}

void RingBeam::setLife(s32 life) {
    mLife = life;
}

void RingBeam::init(const TableIter& rIter) {
    Pos3f mtx;
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("JumpBeamerBeam", nullptr, false);
    MR::connectToSceneIndirectEnemy(this);
    initHitSensor(1);
    MR::addHitSensorEye(this, "Beam", 8, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    initEffectKeeper(1, nullptr);
    initSound(2, true);
    mSoundObject->setTrans(&mSoundPos);
    MR::invalidateClipping(this);
    initNerve(GET_NERVE(RingBeam, RingBeamNrvSpread));

    initPos(this);
    mtx.setInline(this->getBaseMtx());
    mtx.getZDir(mFront);
    mtx.getTrans(mCenter);
    mtx.getTrans(mSoundPos);
    if (mHasShadow) {
        mShadowModel = new ModelObj("影モデル", "JumpBeamerBeamShadow", nullptr, MR::DrawBufferType_IndirectEnemy, MR::MovementType_Enemy,
                           MR::CalcAnimType_Enemy, false);
        mShadowModel->initWithoutIter();
        mShadowModel->mPosition.set(this->mPosition);
        mShadowModel->mRotation.set(this->mRotation);
        mShadowModel->mScale.set(this->mScale);
    } else {
        mShadowDrawer = new RingBeamShadowDrawer(this);
    }
    mBloomModel = MR::createModelObjBloomModel("リングビームブルーム", "JumpBeamerBeamBloom", getBaseMtx());
    MR::invalidateClipping(mBloomModel);
    makeActorDead();
}

void RingBeam::appear() {
    LiveActor::appear();
    mBloomModel->appear();
}

void RingBeam::kill() {
    setNerve(GET_NERVE(RingBeam, RingBeamNrvSpread));
    initPos(mHost);
    setRadius(0.0f);
    LiveActor::kill();
    mBloomModel->kill();
}

void RingBeam::startBrk(const char* pchar) {
    MR::startBrk(this, pchar);
    if (mShadowModel) {
        MR::startBrk(mShadowModel, pchar);
    }
}

void RingBeam::setRadius(f32 radius) {
    Vec3f Ydir;
    Vec3f temp2;
    Vec3f temp6;

    if (radius >= 2000.0f) {
        radius = 2000.0f;
    }
    if (mShadowDrawer) {
        mShadowDrawer->mRadius = radius;
    }
    getSensor("Beam")->mRadius = radius;
    const char* str = "Spread";
    MR::startBckNoInterpole(this, str);
    MR::startBckNoInterpole(mBloomModel, str);
    radius = (radius / 2000.0f) * MR::getBckFrameMax(this);
    MR::setBckFrameAndStop(this, radius);
    MR::setBckFrameAndStop(mBloomModel, radius);
    if (mShadowModel) {
        Triangle triangle;
        MR::extractMtxYDir(mHost->getBaseMtx(), &Ydir);
        Ydir.scale(1000.0f);

        MR::getFirstPolyOnLineToMap(&temp2, &triangle, mCenter, mBeamGravity * 500.0f);

        temp2 -= mBeamGravity * 10.0f;
        MR::calcPerpendicFootToLine(&temp6, temp2, mHost->mPosition + Ydir, mHost->mPosition - Ydir);
        mShadowModel->mPosition.set(temp6);
        MR::startBckNoInterpole(mShadowModel, str);
        f32 length = (temp2 - temp6).length();
        if (0.0f <= length && length < 2000.0f) {
            MR::setBckFrameAndStop(mShadowModel, (length / 2000.0f) * MR::getBckFrameMax(mShadowModel));
        }
    }
}

void RingBeam::initPos(const LiveActor* actor) {
    Rot3f actorBaseMtxCopy;
    Vec3f baseMtxYdir;
    actorBaseMtxCopy.setInline(actor->getBaseMtx());
    actorBaseMtxCopy.getYDir(baseMtxYdir);
    mPosition.set(actor->mPosition + baseMtxYdir * 75.0f);
}

void RingBeam::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    Rot3f mtx;
    Vec3f temp;
    Vec3f temp2;
    Vec3f sensordiff = pReceiver->mPosition;
    sensordiff -= pSender->mPosition;
    MR::makeMtxTR((MtxPtr)&mtx, this);
    // this is 100% certainly an inline
    f32 x = mtx.mMtx[0][1];
    f32 y = mtx.mMtx[1][1];
    f32 z = mtx.mMtx[2][1];
    temp.set< f32 >(x, y, z);
    MR::vecKillElement(sensordiff, temp, &temp2);
    temp2.setLength(pSender->mRadius);

    Vec3f temp3 = temp2;
    temp3 -= sensordiff;
    f32 length = temp3.length();
    f32 receiverRadius = pReceiver->mRadius;

    if (!(length > 5.0f + receiverRadius) && MR::isSensorPlayerOrRide(pReceiver)) {
        MR::sendMsgEnemyAttackElectric(pReceiver, pSender);
    }
}

bool RingBeam::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    return false;
}

void RingBeam::exeSpread() {
    Pos3f mtx;

    if (MR::isFirstStep(this)) {
        if (!MR::isPowerStarGetDemoActive()) {
            MR::startSound(this, "SE_EM_JUMPRING_APPEAR");
        }
        const char* str = "Spread";
        MR::startBckNoInterpole(this, str);
        MR::startBckNoInterpole(mBloomModel, str);
        startBrk("Erase");
        MR::setBrkFrameAndStop(this, 0.0f);
        if (mShadowModel) {
            MR::setBrkFrameAndStop(mShadowModel, 0.0f);
        }
        Vec3f temp;
        MR::extractMtxZDir(mHost->getBaseMtx(), &mFront);
        MR::extractMtxYDir(mHost->getBaseMtx(), &temp);
        mCenter.set(mHost->mPosition + temp * 75.0f);
        MR::calcGravityVector(this, mCenter, &mBeamGravity, nullptr, 0);
    }

    Vec3f ec;
    if (mIsFollowGravity) {
        MR::calcGravityVector(this, mCenter + mFront * mSpeed, &mBeamGravity, nullptr, 0);
        MR::makeMtxUpFront(&mtx, -mBeamGravity, mFront);
        mtx.getZDir(mFront);
        Vec3f temp7;
        MR::extractMtxYDir(mHost->getBaseMtx(), &temp7);
        MR::calcPerpendicFootToLine(&ec, mCenter, mHost->mPosition + temp7, mHost->mPosition - temp7);
        mPosition = ec.copy();
    } else {
        ec = mPosition;
    }

    mCenter += mFront * mSpeed;
    setRadius((mCenter - ec).length());

    if (!MR::isPowerStarGetDemoActive()) {
        Vec3f temp12 = MR::getCamPos() - ec;
        if (!MR::isNearZero(temp12)) {
            f32 radius;
            f32 float2;
            Vec3f temp13;
            MR::extractMtxYDir(mHost->getBaseMtx(), &temp13);
            Vec3f temp15 = temp12 - temp13 * (temp12.dot(temp13));
            MR::normalizeOrZero(&temp15);
            MR::normalizeOrZero(&temp12);
            float2 = 1.0f - MR::abs(temp13.dot(temp12));
            radius = getSensor("Beam")->mRadius;
            mSoundPos = ec + temp15 * radius * float2;
        } else {
            mSoundPos = ec;
        }
        MR::startLevelSound(this, "SE_EM_LV_RINGBEAM_MOVE");
    }
    if (MR::isStep(this, mLife - (s32)MR::getBrkFrameMax(this))) {
        startBrk("Erase");
    }
    if (MR::isGreaterEqualStep(this, mLife)) {
        kill();
    }
}
