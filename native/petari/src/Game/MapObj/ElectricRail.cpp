#include "Game/MapObj/ElectricRail.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/AudioLib/AudAnmSoundObject.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/ShadowVolumeDrawer.hpp"
#include "Game/MapObj/ElectricRailHolder.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace {
    const char* cSensorNameTable[] = {"body0", "body1", "body2", "body3", "body4", "body5", "body6", "body7"};
    // const f32 cStepInterval;
    // const f32 cPointSensorRadius;
    // const f32 cRailRadius;
    // const f32 cSeparateInterval;
    // const f32 cSeparateTexRate;
    // const f32 cRailShadowWidth;
    // const f32 cRailDisappearFrame;
    // const f32 cShadowVolumeHeight;
};  // namespace

namespace NrvElectricRail {
    NEW_NERVE(ElectricRailNrvWait, ElectricRail, Wait);
    NEW_NERVE(ElectricRailNrvDisappear, ElectricRail, Disappear);
    NEW_NERVE(ElectricRailNrvDisappeared, ElectricRail, Disappeared);
};  // namespace NrvElectricRail

ElectricRailPoint::~ElectricRailPoint() {
}

ElectricRailPoint::ElectricRailPoint(const char* pName) : LiveActor(pName), mHasShadow(true), mIsActive(true) {
}

void ElectricRailPoint::init(const TableIter& rIter) {
    initModelManagerWithAnm("ElectricRailPoint", nullptr, 0);
    MR::connectToSceneMapObjDecoration(this);

    if (mHasShadow) {
        MR::initShadowVolumeSphere(this, 35.0);

        MR::onCalcShadowOneTime(this, 0);
        MR::onCalcShadowDropGravityOneTime(this, 0);
    }

    initHitSensor(1);
    MR::addHitSensorMapObj(this, "body", 8, 35.0f, Vec3f(0.0f, 0.0f, 0.0f));

    makeActorAppeared();
}

void ElectricRailPoint::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (mIsActive) {
        MR::sendMsgEnemyAttackElectric(pReceiver, pSender);
    } else {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

ElectricRailSeparator::ElectricRailSeparator() : _0(gZeroVec), mSide(gZeroVec), mUp(gZeroVec), mFront(gZeroVec), _30(gZeroVec) {
}

void ElectricRailSeparator::setup(const Vec3f& rVec1, const Vec3f& rVec2, const Vec3f& rVec3) {
    _30.set(rVec3);
    _0.set(rVec1);
    mFront.set(rVec2);

    mFront.negate();

    Vec3f upVec = -rVec3;

    if (MR::isSameDirection(upVec, mFront)) {
        MR::makeAxisCrossPlane(&mSide, &mUp, mFront);
    } else {
        MR::makeAxisFrontUp(&mSide, &mUp, mFront, upVec);
    }
}

ElectricRailShadowDrawer::ElectricRailShadowDrawer(const LiveActor* pActor, ElectricRailSeparator* pSeparator, s32 count)
    : ShadowVolumeDrawer("影描画[電撃レールボリューム]"), mHost(pActor) {
    mMesh.init(count * 8 + 2);
    writeShadowVolumeShape(pSeparator, count);
    mMesh.send();
}

void ElectricRailShadowDrawer::loadModelDrawMtx(TypedDraw& rDraw) const {
    rDraw.setWorld(nullptr);
}

void ElectricRailShadowDrawer::drawShape(TypedDraw& rDraw) const {
    mMesh.draw(rDraw);
}

bool ElectricRailShadowDrawer::isDraw() const {
    return MR::isValidDraw(mHost);
}

void ElectricRailShadowDrawer::writeShadowVolumeShape(ElectricRailSeparator* pSeparator, s32 val) {
    VertexP* vtx = mMesh.strip(val * 4 + 2);

    ElectricRailSeparator* separator;
    Vec3f vec24, vec30, vec3C, vec48;

    for (s32 i = 0; i < val; i++) {
        separator = &pSeparator[i];

        vec3C.scale(20.0f, separator->mSide);

        vec48.scale(300.0f, separator->_30);

        vec24.sub(separator->_0, vec3C);

        vec30.add(vec24, vec48);

        *vtx++ = {vec24.x, vec24.y, vec24.z};
        *vtx++ = {vec30.x, vec30.y, vec30.z};
    }

    for (s32 i = 0; i < val; i++) {
        separator = &pSeparator[val - i - 1];

        vec3C.scale(20.0f, separator->mSide);

        vec48.scale(300.0f, separator->_30);

        vec24.add(separator->_0, vec3C);

        vec30.add(vec24, vec48);

        *vtx++ = {vec24.x, vec24.y, vec24.z};
        *vtx++ = {vec30.x, vec30.y, vec30.z};
    }

    vec3C.scale(20.0f, pSeparator->mSide);

    vec48.scale(300.0f, pSeparator->_30);

    vec24.sub(pSeparator->_0, vec3C);

    vec30.add(vec24, vec48);

    *vtx++ = {vec24.x, vec24.y, vec24.z};
    *vtx++ = {vec30.x, vec30.y, vec30.z};

    vtx = mMesh.strip(val * 2);

    for (s32 i = 0; i < val; i++) {
        separator = &pSeparator[i];

        vec3C.scale(20.0f, separator->mSide);

        vec24.add(separator->_0, vec3C);

        vec30.sub(separator->_0, vec3C);

        *vtx++ = {vec24.x, vec24.y, vec24.z};
        *vtx++ = {vec30.x, vec30.y, vec30.z};
    }

    vtx = mMesh.strip(val * 2);

    for (s32 i = 0; i < val; i++) {
        separator = &pSeparator[val - i - 1];

        vec3C.scale(20.0f, separator->mSide);

        vec48.scale(300.0f, separator->_30);

        vec24.add(separator->_0, vec3C);
        vec24.add(vec24, vec48);

        vec30.sub(separator->_0, vec3C);
        vec30.add(vec30, vec48);

        *vtx++ = {vec24.x, vec24.y, vec24.z};
        *vtx++ = {vec30.x, vec30.y, vec30.z};
    }
}

ElectricRail::ElectricRail(const char* name)
    : ElectricRailDrawn(name), mPoints(), mPointCount(), _94(), mSeparators(), mSeparatorCount(), mRailHeight(1),
      mEaseIn(30.0f), mShadowDrawer(), mIsCalcGravity() {
}

void ElectricRail::init(const TableIter& rIter) {
    s32 railType = 0;
    MR::getPlacementArg3NoInit(rIter, &railType);
    ElectricRailFunction::registerRail(this, static_cast< ElectricRailType >(railType));

    MR::connectToSceneMapObjMovement(this);

    initMapToolInfo(rIter);
    initRailRider(rIter);

    f32 radius;
    MR::calcRailClippingInfo(&mPosition, &radius, this, 100.0f, 500.0f);
    MR::setClippingTypeSphere(this, radius, &mPosition);

    MR::getPlacementArg4NoInit(rIter, &mIsCalcGravity);

    if (mIsCalcGravity && !MR::calcGravityVectorOrZero(this, mPosition, &mGravity, nullptr, 0)) {
        MR::calcDropShadowVector(this, mPosition, &mGravity, nullptr, 0);
    }

    initSensor();

    MR::setClippingFar200m(this);

    initPoints();
    initSeparators();
    initMesh();
    initShadow(rIter);
    initSound(4, 1);

    mSoundObject->setTrans(_94);

    if (MR::tryRegisterDemoCast(this, rIter)) {
        for (s32 i = 0; i < mPointCount; i++) {
            MR::tryRegisterDemoCast(&mPoints[i], rIter);
        }

        MR::registerDemoActionNerve(this, GET_NERVE(ElectricRail, ElectricRailNrvDisappear), nullptr);
    }

    initNerve(GET_NERVE(ElectricRail, ElectricRailNrvWait));

    if (MR::isExistStageSwitchSleep(rIter)) {
        MR::useStageSwitchSleep(this, rIter);
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void ElectricRail::initAfterPlacement() {
    if (!MR::isValidSwitchA(this)) {
        return;
    }

    if (!MR::isOnSwitchA(this)) {
        return;
    }

    MR::invalidateHitSensors(this);

    for (s32 i = 0; i < mPointCount; i++) {
        mPoints[i].mIsActive = false;
    }

    setNerve(GET_NERVE(ElectricRail, ElectricRailNrvDisappeared));
}

void ElectricRail::drawWith(MaterialDraw& rMaterial) const {
    if (!MR::isValidDraw(this)) {
        return;
    }

    TypedDraw site(rMaterial, nullptr, MR::getCameraViewMtx());
    if (isNerve(GET_NERVE(ElectricRail, ElectricRailNrvWait))) {
        mMesh.draw(site);
    } else {
        drawRail(site, mEaseIn);
    }
}

void ElectricRail::makeActorAppeared() {
    LiveActor::makeActorAppeared();

    for (s32 i = 0; i < mPointCount; i++) {
        mPoints[i].makeActorAppeared();
    }
}

void ElectricRail::makeActorDead() {
    LiveActor::makeActorDead();

    for (s32 i = 0; i < mPointCount; i++) {
        mPoints[i].makeActorDead();
    }
}

void ElectricRail::disappear() {
    if (isNerve(GET_NERVE(ElectricRail, ElectricRailNrvWait))) {
        setNerve(GET_NERVE(ElectricRail, ElectricRailNrvDisappear));
    }
}

void ElectricRail::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorPlayer(pReceiver)) {
        return;
    }

    if (MR::isPlayerSquat()) {
        return;
    }

    MR::sendMsgEnemyAttackElectric(pReceiver, pSender);
}

void ElectricRail::initMapToolInfo(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    MR::getPlacementArg0NoInit(rIter, &mRailHeight);

    if (MR::useStageSwitchReadA(this, rIter)) {
        MR::listenStageSwitchOnA(this, MR::Functor(this, &ElectricRail::disappear));
    }
}

void ElectricRail::initSensor() {
    initHitSensor(mRailHeight);

    _94 = new Vec3f[mRailHeight];

    for (s32 i = 0; i < mRailHeight; i++) {
        MR::addHitSensorPosMapObj(this, ::cSensorNameTable[i], 8, ElectricRailFunction::getHitSensorRadius(), &_94[i], Vec3f(0.0f, 0.0f, 0.0f));
    }

    updateHitSensorPos();
}

void ElectricRail::initPoints() {
    ElectricRailPoint* point;
    s32 railPointNum = MR::getRailPointNum(this);
    mPointCount = 0;

    s32 curRail = 0;

    while (curRail < railPointNum) {
        s32 curRailPointArg = -1;

        bool ret = MR::getRailPointArg0NoInit(this, curRail, &curRailPointArg);

        if (!ret) {
            mPointCount++;
        }

        curRail++;
    }

    mPointCount *= mRailHeight;
    mPoints = new ElectricRailPoint[mPointCount];

    s32 curPointIdx = 0;
    s32 curRailPoint = 0;
    s32 flag = 0;

    while (curRailPoint < MR::getRailPointNum(this)) {
        s32 tempArg = -1;
        bool ret = MR::getRailPointArg0NoInit(this, curRailPoint, &tempArg);

        if (!ret) {
            point = &mPoints[curPointIdx];
            Vec3f pos;
            MR::calcRailPointPos(&pos, this, curRailPoint);
            point->mPosition.set(pos);
            curPointIdx++;

            if (mRailHeight > 1) {
                Vec3f outGrav;
                calcGravity(&outGrav, pos);
                outGrav.scale(-100.0f);

                s32 curHeight = 1;

                while (curHeight < mRailHeight) {
                    point = &mPoints[curPointIdx];
                    pos.add(outGrav);
                    point->mPosition.set(pos);
                    point->mHasShadow = flag;
                    curPointIdx++;
                    curHeight++;
                }
            }
        }

        curRailPoint++;
    }

    s32 curPoint = 0;

    while (curPoint < mPointCount) {
        mPoints[curPoint].initWithoutIter();
        curPoint++;
    }
}

void ElectricRail::initSeparators() {
    mSeparatorCount = static_cast< s32 >(MR::getRailTotalLength(this) / 200.0f) + 1;
    mSeparators = new ElectricRailSeparator[mSeparatorCount];

    Vec3f railPos, gravity;

    for (s32 i = 0; i < mSeparatorCount - 1; i++) {
        MR::setRailCoord(this, i * 200.0f);

        railPos.set(MR::getRailPos(this));

        calcGravity(&gravity, railPos);

        if (MR::isNearZero(gravity)) {
            gravity.set< f32 >(0.0f, -1.0f, 0.0f);
        }

        mSeparators[i].setup(railPos, MR::getRailDirection(this), gravity);
    }

    if (MR::isLoopRail(this)) {
        MR::moveCoordToStartPos(this);
    } else {
        MR::moveCoordToEndPos(this);
    }

    railPos.set(MR::getRailPos(this));

    calcGravity(&gravity, railPos);

    mSeparators[mSeparatorCount - 1].setup(railPos, MR::getRailDirection(this), gravity);

    MR::moveCoordToStartPos(this);
}

void ElectricRail::initMesh() {
    mMesh.init(mSeparatorCount * 4 * mRailHeight);
    drawPlane(30.0, 30.0, -30.0, -30.0);
    drawPlane(-30.0, 30.0, 30.0, -30.0);
    mMesh.send();
}

void ElectricRail::drawRail(TypedDraw& rDraw, f32 a1) const {
    drawPlaneGX(rDraw, a1, a1, -a1, -a1);
    drawPlaneGX(rDraw, -a1, a1, a1, -a1);
}

void ElectricRail::drawPlane(f32 a1, f32 a2, f32 a3, f32 a4) {
    const Vec3f* pUp;

    for (s32 i = 0; i < mRailHeight; i++) {
        VertexPT* vtx = mMesh.strip(2 * mSeparatorCount);

        for (s32 j = 0; j < mSeparatorCount; j++) {
            ElectricRailSeparator* s = &mSeparators[j];
            Vec3f v21(s->_0);

            f32 v15 = 0.5f * j;

            pUp = &s->mUp;
            Vec3f v20(*pUp);
            v20.scale(100.0f * i);
            v21.add(v21, v20);

            *vtx++ = {((v21.x + (s->mSide.x * a1)) + (pUp->x * a2)), ((v21.y + (s->mSide.y * a1)) + (pUp->y * a2)),
                           ((v21.z + (s->mSide.z * a1)) + (pUp->z * a2)), v15, 0.0f};

            *vtx++ = {((v21.x + (s->mSide.x * a3)) + (pUp->x * a4)), ((v21.y + (s->mSide.y * a3)) + (pUp->y * a4)),
                           ((v21.z + (s->mSide.z * a3)) + (pUp->z * a4)), v15, 1.0f};
        }
    }
}

void ElectricRail::drawPlaneGX(TypedDraw& rDraw, f32 a1, f32 a2, f32 a3, f32 a4) const {
    const Vec3f* pUp;

    for (s32 i = 0; i < mRailHeight; i++) {
        VertexPT* vtx = rDraw.vertices< VertexPT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 2 * mSeparatorCount);

        for (s32 j = 0; j < mSeparatorCount; j++) {
            ElectricRailSeparator* s = &mSeparators[j];
            Vec3f v21(s->_0);
            f32 v15 = 0.5f * j;

            pUp = &s->mUp;
            Vec3f v20(*pUp);
            v20.scale(100.0f * i);
            v21.add(v21, v20);

            *vtx++ = {((v21.x + (s->mSide.x * a1)) + (pUp->x * a2)), ((v21.y + (s->mSide.y * a1)) + (pUp->y * a2)), ((v21.z + (s->mSide.z * a1)) + (pUp->z * a2)), v15, 0.0f};

            *vtx++ = {((v21.x + (s->mSide.x * a3)) + (pUp->x * a4)), ((v21.y + (s->mSide.y * a3)) + (pUp->y * a4)), ((v21.z + (s->mSide.z * a3)) + (pUp->z * a4)), v15, 1.0f};
        }
    }
}

void ElectricRail::initShadow(const TableIter& rIter) {
    f32 arg1;
    MR::getPlacementArg1WithInit(rIter, &arg1);

    f32 arg2;
    MR::getPlacementArg2WithInit(rIter, &arg2);

    if (!(0.0f < arg1 || 0.0f < arg2)) {
        return;
    }

    bool flag = 0.0f < arg2;

    f32 dropLength;

    if (flag) {
        MR::initShadowController(this, 2);
        dropLength = arg2;
    } else {
        MR::initShadowController(this, 3);
        dropLength = arg1;
    }

    MR::addShadowVolumeCylinder(this, "start", 20.0);
    MR::addShadowVolumeCylinder(this, "end", 20.0);

    Vec3f startPos;
    MR::calcRailStartPos(&startPos, this);

    Vec3f endPos;
    MR::calcRailEndPos(&endPos, this);

    MR::setShadowDropPosition(this, "start", startPos);
    MR::setShadowDropPosition(this, "end", endPos);

    MR::setShadowDropLength(this, "start", dropLength);
    MR::setShadowDropLength(this, "end", dropLength);

    MR::onCalcShadowDropPrivateGravity(this, "start");
    MR::onCalcShadowDropPrivateGravity(this, "end");

    if (flag) {
        mShadowDrawer = new ElectricRailShadowDrawer(this, mSeparators, mSeparatorCount);
    } else {
        MR::addShadowVolumeLine(this, "line", this, "start", 20.0, this, "end", 20.0);
    }
}

void ElectricRail::updateHitSensorPos() {
    MR::calcNearestRailPos(_94, this, *MR::getPlayerCenterPos());

    if (mRailHeight <= 1) {
        return;
    }

    Vec3f gravity;
    calcGravity(&gravity, *_94);
    gravity.scale(-100.0);

    for (s32 i = 1; i < mRailHeight; i++) {
        _94[i].add(_94[i - 1], gravity);
    }
}

void ElectricRail::calcGravity(Vec3f* pOut, const Vec3f& rA2) const {
    if (mIsCalcGravity) {
        pOut->set(mGravity);
    } else if (!MR::calcGravityVectorOrZero(this, rA2, pOut, nullptr, 0)) {
        MR::calcDropShadowVector(this, rA2, pOut, nullptr, 0);
    }
}

void ElectricRail::exeDisappear() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_ELEC_RAIL_VANISH");
        MR::invalidateHitSensors(this);

        for (s32 i = 0; i < mPointCount; i++) {
            mPoints[i].mIsActive = false;
        }
    }

    mEaseIn = MR::calcNerveEaseInValue(this, 20, 30.0, 0.0);

    if (MR::isStep(this, 20)) {
        setNerve(GET_NERVE(ElectricRail, ElectricRailNrvDisappeared));
    }
}

void ElectricRail::exeWait() {
    updateHitSensorPos();
    MR::startLevelSound(this, "SE_OJ_LV_ELEC_RAIL_HAM");
}

void ElectricRail::exeDisappeared() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);
    }
}
