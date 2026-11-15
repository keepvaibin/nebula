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
#include "Game/System/LumaCore.hpp"

namespace NrvRingBeam {
    NEW_NERVE(RingBeamNrvSpread, RingBeam, Spread);
};  // namespace NrvRingBeam

RingBeamShadowDrawer::RingBeamShadowDrawer(const LiveActor* pHost) : ShadowVolumeDrawer("影描画[リングビーム]") {
    lumacore::setClass(this, lumacore::kClassId< RingBeamShadowDrawer >);
    mHost = pHost;
    mRadius = 0.0f;
}

void RingBeamShadowDrawer::drawShape(TypedDraw& rDraw) const {
    Pos3f pointMtx[32];
    Pos3f baseMtx;
    Mtx stepRotation;
    Vec3f pointPos;
    Vec3f gravity;
    Vec3f radialDir;
    Vec3f edgeOffsetPos;
    Vec3f radial;
    Vec3f edgeOffsetNeg;
    Vec3f down;
    Vec3f xDir;
    Vec3f yDir;
    Vec3f center;
    Vec3f pointXDir;
    Vec3f pointYDir;
    // 2 pi / 31: point 31 lands on point 0 and closes the strips.
    f32 stepAngle = 0.2026834f;

    baseMtx.setInline(mHost->getBaseMtx());
    baseMtx.getXDir(xDir);
    baseMtx.getYDir(yDir);
    baseMtx.getTrans(center);
    NebulaMtxRotAxisRad(stepRotation, &yDir, stepAngle);
    radial.scale(mRadius, xDir);
    for (int i = 0; i < 32; i++) {
        pointPos.add(center, radial);
        MR::calcGravityVector(mHost, pointPos, &gravity, nullptr, 0);
        if (MR::normalizeOrZero(radial, &radialDir)) {
            pointMtx[i].setInline(baseMtx);
        } else if (MR::isSameDirection(-gravity, radialDir)) {
            MR::makeMtxUpSide(&pointMtx[i], -gravity, -yDir);
        } else if (gravity.dot(yDir) < 0.0f) {
            MR::makeMtxUpSide(&pointMtx[i], -gravity, radialDir);
        } else {
            MR::makeMtxUpSide(&pointMtx[i], -gravity, -radialDir);
        }
        pointMtx[i][0][3] = pointPos.x;
        pointMtx[i][1][3] = pointPos.y;
        pointMtx[i][2][3] = pointPos.z;

        NebulaMtxMultVec(stepRotation, &radial, &radial);
    }

    // Volume: the band 10 wide, the same band 500 down, then the -x and +x walls joining them.
    Vec3f edgeNeg;
    Vec3f edgePos;
    VertexP* vtx = rDraw.vertices< VertexP >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 64);
    for (int i = 0; i < 32; i++) {
        pointMtx[i].getXDir(pointXDir);
        pointMtx[i].getTrans(pointPos);
        edgeOffsetNeg.scale(-5.0f, pointXDir);
        edgeOffsetPos.scale(5.0f, pointXDir);
        edgeNeg.add(pointPos, edgeOffsetNeg);
        edgePos.add(pointPos, edgeOffsetPos);
        *vtx++ = {edgePos.x, edgePos.y, edgePos.z};
        *vtx++ = {edgeNeg.x, edgeNeg.y, edgeNeg.z};
    }

    vtx = rDraw.vertices< VertexP >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 64);
    for (int i = 0; i < 32; i++) {
        pointMtx[i].getXDir(pointXDir);
        pointMtx[i].getYDir(pointYDir);
        pointMtx[i].getTrans(pointPos);

        edgeOffsetNeg.scale(-5.0f, pointXDir);
        edgeOffsetPos.scale(5.0f, pointXDir);
        down.scale(500.0f, -pointYDir);
        edgeNeg.add(pointPos, edgeOffsetNeg);
        edgePos.add(pointPos, edgeOffsetPos);
        edgeNeg.add(down);
        edgePos.add(down);
        *vtx++ = {edgeNeg.x, edgeNeg.y, edgeNeg.z};
        *vtx++ = {edgePos.x, edgePos.y, edgePos.z};
    }

    Vec3f edgeTop;
    Vec3f edgeBottom;
    vtx = rDraw.vertices< VertexP >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 64);
    for (int i = 0; i < 32; i++) {
        pointMtx[i].getXDir(pointXDir);
        pointMtx[i].getYDir(pointYDir);
        pointMtx[i].getTrans(pointPos);

        edgeOffsetNeg.scale(-5.0f, pointXDir);
        down.scale(500.0f, -pointYDir);
        edgeTop.add(pointPos, edgeOffsetNeg);
        edgeBottom.add(pointPos, edgeOffsetNeg);
        edgeBottom.add(down);
        *vtx++ = {edgeTop.x, edgeTop.y, edgeTop.z};
        *vtx++ = {edgeBottom.x, edgeBottom.y, edgeBottom.z};
    }

    vtx = rDraw.vertices< VertexP >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 64);
    for (int i = 0; i < 32; i++) {
        pointMtx[i].getXDir(pointXDir);
        pointMtx[i].getYDir(pointYDir);
        pointMtx[i].getTrans(pointPos);

        edgeOffsetPos.scale(5.0f, pointXDir);
        down.scale(500.0f, -pointYDir);
        edgeTop.add(pointPos, edgeOffsetPos);
        edgeBottom.add(pointPos, edgeOffsetPos);
        edgeBottom.add(down);
        *vtx++ = {edgeBottom.x, edgeBottom.y, edgeBottom.z};
        *vtx++ = {edgeTop.x, edgeTop.y, edgeTop.z};
    }
}

void RingBeamShadowDrawer::loadModelDrawMtx(TypedDraw& rDraw) const {
    rDraw.setWorld(nullptr);
};

bool RingBeamShadowDrawer::isDraw() const {
    return MR::isValidDraw(mHost);
}

// No controller: the beam's own place and life name the draws.
void RingBeamShadowDrawer::calcDrawAnchor(Vec3f* pOut) const {
    pOut->set(mHost->mPosition);
}

u32 RingBeamShadowDrawer::calcDrawLives() const {
    return mHost->mMotionEpoch;
}

RingBeam::RingBeam(const char* pName, LiveActor* pHost, bool isFollowGravity, bool hasShadow) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< RingBeam >);
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
    mtx.getTrans(mEdgePos);
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

void RingBeam::startBrk(const char* pAnimName) {
    MR::startBrk(this, pAnimName);
    if (mShadowModel) {
        MR::startBrk(mShadowModel, pAnimName);
    }
}

void RingBeam::setRadius(f32 radius) {
    Vec3f hostAxis;
    Vec3f groundPos;
    Vec3f axisFoot;

    if (radius >= 2000.0f) {
        radius = 2000.0f;
    }
    if (mShadowDrawer) {
        mShadowDrawer->mRadius = radius;
    }
    getSensor("Beam")->mRadius = radius;
    const char* pAnimName = "Spread";
    MR::startBckNoInterpole(this, pAnimName);
    MR::startBckNoInterpole(mBloomModel, pAnimName);
    f32 frame = (radius / 2000.0f) * MR::getBckFrameMax(this);
    MR::setBckFrameAndStop(this, frame);
    MR::setBckFrameAndStop(mBloomModel, frame);
    if (mShadowModel) {
        // The shadow ring sits on the host's axis, sized by where the edge meets the ground.
        Triangle triangle;
        MR::extractMtxYDir(mHost->getBaseMtx(), &hostAxis);
        hostAxis.scale(1000.0f);

        // No ground within reach under the edge: there is nothing for the shadow ring to lie on.
        if (!MR::getFirstPolyOnLineToMap(&groundPos, &triangle, mEdgePos, mBeamGravity * 500.0f)) {
            MR::hideModelIfShown(mShadowModel);
            return;
        }

        MR::showModelIfHidden(mShadowModel);
        groundPos -= mBeamGravity * 10.0f;
        MR::calcPerpendicFootToLine(&axisFoot, groundPos, mHost->mPosition + hostAxis, mHost->mPosition - hostAxis);
        mShadowModel->mPosition.set(axisFoot);
        MR::startBckNoInterpole(mShadowModel, pAnimName);
        f32 length = (groundPos - axisFoot).length();
        if (0.0f <= length && length < 2000.0f) {
            MR::setBckFrameAndStop(mShadowModel, (length / 2000.0f) * MR::getBckFrameMax(mShadowModel));
        }
    }
}

void RingBeam::initPos(const LiveActor* pActor) {
    Rot3f baseMtx;
    Vec3f up;
    baseMtx.setInline(pActor->getBaseMtx());
    baseMtx.getYDir(up);
    mPosition.set(pActor->mPosition + up * 75.0f);
}

void RingBeam::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    Rot3f mtx;
    Vec3f beamUp;
    Vec3f ringPoint;
    Vec3f toReceiver = pReceiver->mPosition;
    toReceiver -= pSender->mPosition;
    MR::makeMtxTR(mtx.toMtxPtr(), this);
    f32 x = mtx.mMtx[0][1];
    f32 y = mtx.mMtx[1][1];
    f32 z = mtx.mMtx[2][1];
    beamUp.set< f32 >(x, y, z);
    // The ring point nearest the receiver, relative to the beam's center.
    MR::vecKillElement(toReceiver, beamUp, &ringPoint);
    ringPoint.setLength(pSender->mRadius);

    Vec3f receiverToRing = ringPoint;
    receiverToRing -= toReceiver;
    f32 length = receiverToRing.length();
    f32 receiverRadius = pReceiver->mRadius;

    if (!(length > 5.0f + receiverRadius) && MR::isSensorPlayerOrRide(pReceiver)) {
        MR::sendMsgEnemyAttackElectric(pReceiver, pSender);
    }
}

bool RingBeam::receiveMsgPlayerAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return false;
}

void RingBeam::exeSpread() {
    Pos3f mtx;

    if (MR::isFirstStep(this)) {
        if (!MR::isPowerStarGetDemoActive()) {
            MR::startSound(this, "SE_EM_JUMPRING_APPEAR");
        }
        const char* pAnimName = "Spread";
        MR::startBckNoInterpole(this, pAnimName);
        MR::startBckNoInterpole(mBloomModel, pAnimName);
        startBrk("Erase");
        MR::setBrkFrameAndStop(this, 0.0f);
        if (mShadowModel) {
            MR::setBrkFrameAndStop(mShadowModel, 0.0f);
        }
        Vec3f hostUp;
        MR::extractMtxZDir(mHost->getBaseMtx(), &mFront);
        MR::extractMtxYDir(mHost->getBaseMtx(), &hostUp);
        mEdgePos.set(mHost->mPosition + hostUp * 75.0f);
        MR::calcGravityVector(this, mEdgePos, &mBeamGravity, nullptr, 0);
    }

    Vec3f ringCenter;
    if (mIsFollowGravity) {
        MR::calcGravityVector(this, mEdgePos + mFront * mSpeed, &mBeamGravity, nullptr, 0);
        MR::makeMtxUpFront(&mtx, -mBeamGravity, mFront);
        mtx.getZDir(mFront);
        Vec3f hostUp;
        MR::extractMtxYDir(mHost->getBaseMtx(), &hostUp);
        MR::calcPerpendicFootToLine(&ringCenter, mEdgePos, mHost->mPosition + hostUp, mHost->mPosition - hostUp);
        mPosition = ringCenter.copy();
    } else {
        ringCenter = mPosition;
    }

    mEdgePos += mFront * mSpeed;
    setRadius((mEdgePos - ringCenter).length());

    if (!MR::isPowerStarGetDemoActive()) {
        // The sound sits on the ring point nearest the camera, pulled in as the view looks down the axis.
        Vec3f toCamera = MR::getCamPos() - ringCenter;
        if (!MR::isNearZero(toCamera)) {
            f32 radius;
            f32 sideFactor;
            Vec3f hostUp;
            MR::extractMtxYDir(mHost->getBaseMtx(), &hostUp);
            Vec3f toCameraFlat = toCamera - hostUp * (toCamera.dot(hostUp));
            MR::normalizeOrZero(&toCameraFlat);
            MR::normalizeOrZero(&toCamera);
            sideFactor = 1.0f - MR::abs(hostUp.dot(toCamera));
            radius = getSensor("Beam")->mRadius;
            mSoundPos = ringCenter + toCameraFlat * radius * sideFactor;
        } else {
            mSoundPos = ringCenter;
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
