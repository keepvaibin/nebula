#include "Game/MapObj/ElectricRail.hpp"
#include <nebula_format.h>
#include "Game/Render/FrameList.hpp"
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
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 500.0f);

namespace {
    // One per rail level; arg 0 (the level count) is at most this many.
    const char* const cSensorNameTable[] = {"body0", "body1", "body2", "body3", "body4", "body5", "body6", "body7"};

    NEBULA_TUNABLE(f32, cLevelHeight, 100.0f);
    NEBULA_TUNABLE(f32, cPointSensorRadius, 35.0f);
    NEBULA_TUNABLE(f32, cRailRadius, 30.0f);
    NEBULA_TUNABLE(f32, cSeparateInterval, 200.0f);
    NEBULA_TUNABLE(f32, cSeparateTexRate, 0.5f);
    NEBULA_TUNABLE(f32, cRailShadowWidth, 20.0f);
    NEBULA_TUNABLE(s32, cRailDisappearFrame, 20);
    NEBULA_TUNABLE(f32, cShadowVolumeHeight, 300.0f);

    // One level's strip across the rail from (side0, up0) to (side1, up1) at each separator, u running along the rail.
    void writePlaneStrip(VertexPT* pVtx, const ElectricRailSeparator* pSeparators, s32 separatorCount, s32 level, f32 side0, f32 up0, f32 side1,
                         f32 up1) {
        for (s32 j = 0; j < separatorCount; j++) {
            const ElectricRailSeparator& rSeparator = pSeparators[j];
            const Vec3f& rSide = rSeparator.mSide;
            const Vec3f& rUp = rSeparator.mUp;
            f32 u = cSeparateTexRate * j;

            Vec3f center(rSeparator.mPos);
            Vec3f lift(rUp);
            lift.scale(cLevelHeight * level);
            center.add(center, lift);

            *pVtx++ = {((center.x + (rSide.x * side0)) + (rUp.x * up0)), ((center.y + (rSide.y * side0)) + (rUp.y * up0)),
                       ((center.z + (rSide.z * side0)) + (rUp.z * up0)), u, 0.0f};
            *pVtx++ = {((center.x + (rSide.x * side1)) + (rUp.x * up1)), ((center.y + (rSide.y * side1)) + (rUp.y * up1)),
                       ((center.z + (rSide.z * side1)) + (rUp.z * up1)), u, 1.0f};
        }
    }

    void putVertex(VertexP*& rVtx, const Vec3f& rPos) {
        *rVtx++ = {rPos.x, rPos.y, rPos.z};
    }

    // A shadow wall edge: the rail's right or left edge, then the same point pushed down the volume.
    void putWallEdge(VertexP*& rVtx, const ElectricRailSeparator& rSeparator, bool isRight) {
        Vec3f side;
        side.scale(cRailShadowWidth, rSeparator.mSide);
        Vec3f drop;
        drop.scale(cShadowVolumeHeight, rSeparator.mOffsetDir);

        Vec3f edge;
        if (isRight) {
            edge.add(rSeparator.mPos, side);
        } else {
            edge.sub(rSeparator.mPos, side);
        }

        Vec3f dropped;
        dropped.add(edge, drop);
        putVertex(rVtx, edge);
        putVertex(rVtx, dropped);
    }
};  // namespace

namespace NrvElectricRail {
    NEW_NERVE(ElectricRailNrvWait, ElectricRail, Wait);
    NEW_NERVE(ElectricRailNrvDisappear, ElectricRail, Disappear);
    NEW_NERVE(ElectricRailNrvDisappeared, ElectricRail, Disappeared);
};  // namespace NrvElectricRail

ElectricRailPoint::ElectricRailPoint(const char* pName) : LiveActor(pName), mHasShadow(true), mIsActive(true) {
}

void ElectricRailPoint::init(const TableIter& /*rIter*/) {
    initModelManagerWithAnm("ElectricRailPoint", nullptr, false);
    MR::connectToSceneMapObjDecoration(this);

    if (mHasShadow) {
        MR::initShadowVolumeSphere(this, cPointSensorRadius);
        MR::onCalcShadowOneTime(this, nullptr);
        MR::onCalcShadowDropGravityOneTime(this, nullptr);
    }

    initHitSensor(1);
    MR::addHitSensorMapObj(this, "body", 8, cPointSensorRadius, Vec3f(0.0f, 0.0f, 0.0f));

    makeActorAppeared();
}

// Shocks while the rail is live; once it has vanished the post is only solid.
void ElectricRailPoint::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (mIsActive) {
        MR::sendMsgEnemyAttackElectric(pReceiver, pSender);
    } else {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

ElectricRailSeparator::ElectricRailSeparator() : mPos(gZeroVec), mSide(gZeroVec), mUp(gZeroVec), mFront(gZeroVec), mOffsetDir(gZeroVec) {
}

// A frame at `rPos` facing back along `rRailDir`, up against `rOffsetDir` (gravity) where it can be.
void ElectricRailSeparator::setup(const Vec3f& rPos, const Vec3f& rRailDir, const Vec3f& rOffsetDir) {
    mOffsetDir.set(rOffsetDir);
    mPos.set(rPos);
    mFront.set(rRailDir);
    mFront.negate();

    Vec3f up = -rOffsetDir;

    if (MR::isSameDirection(up, mFront)) {
        MR::makeAxisCrossPlane(&mSide, &mUp, mFront);
    } else {
        MR::makeAxisFrontUp(&mSide, &mUp, mFront, up);
    }
}

ElectricRailShadowDrawer::ElectricRailShadowDrawer(const LiveActor* pActor, ElectricRailSeparator* pSeparators, s32 separatorCount)
    : ShadowVolumeDrawer("影描画[電撃レールボリューム]"), mHost(pActor) {
    mMesh.init(separatorCount * 8 + 2);
    writeShadowVolumeShape(pSeparators, separatorCount);
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

// A box swept along the rail and pushed down the gravity: the walls as one strip (along the left edge, back along the
// right, closed at the start), then the rail-side cap and the far cap.
void ElectricRailShadowDrawer::writeShadowVolumeShape(ElectricRailSeparator* pSeparators, s32 separatorCount) {
    VertexP* vtx = mMesh.strip(separatorCount * 4 + 2);

    for (s32 i = 0; i < separatorCount; i++) {
        putWallEdge(vtx, pSeparators[i], false);
    }

    for (s32 i = separatorCount - 1; i >= 0; i--) {
        putWallEdge(vtx, pSeparators[i], true);
    }

    putWallEdge(vtx, pSeparators[0], false);

    vtx = mMesh.strip(separatorCount * 2);

    for (s32 i = 0; i < separatorCount; i++) {
        const ElectricRailSeparator& rSeparator = pSeparators[i];
        Vec3f side;
        side.scale(cRailShadowWidth, rSeparator.mSide);

        Vec3f right;
        right.add(rSeparator.mPos, side);
        Vec3f left;
        left.sub(rSeparator.mPos, side);
        putVertex(vtx, right);
        putVertex(vtx, left);
    }

    vtx = mMesh.strip(separatorCount * 2);

    for (s32 i = separatorCount - 1; i >= 0; i--) {
        const ElectricRailSeparator& rSeparator = pSeparators[i];
        Vec3f side;
        side.scale(cRailShadowWidth, rSeparator.mSide);
        Vec3f drop;
        drop.scale(cShadowVolumeHeight, rSeparator.mOffsetDir);

        Vec3f right;
        right.add(rSeparator.mPos, side);
        right.add(right, drop);
        Vec3f left;
        left.sub(rSeparator.mPos, side);
        left.add(left, drop);
        putVertex(vtx, right);
        putVertex(vtx, left);
    }
}

ElectricRail::ElectricRail(const char* pName)
    : ElectricRailDrawn(pName), mPoints(nullptr), mPointCount(0), mSensorPositions(nullptr), mSeparators(nullptr), mSeparatorCount(0), mRailHeight(1),
      mEaseIn(cRailRadius), mShadowDrawer(nullptr), mIsCalcGravity(false) {
}

// Args: 0 levels stacked up the gravity (posts and sensors on each), 1 line shadow drop length, 2 volume shadow drop
// length, 3 the holder's rail type, 4 one gravity for the whole rail (taken at its center). Rail points with arg 0 get
// no post.
void ElectricRail::init(const TableIter& rIter) {
    s32 railType = 0;
    MR::getPlacementArg3NoInit(rIter, &railType);
    ElectricRailFunction::registerRail(this, static_cast< ElectricRailType >(railType));

    MR::connectToSceneMapObjMovement(this);

    initMapToolInfo(rIter);
    initRailRider(rIter);

    f32 radius;
    MR::calcRailClippingInfo(&mPosition, &radius, this, cNum_init_f1, cNum_init_f2);
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
    initSound(4, true);

    mSoundObject->setTrans(mSensorPositions);

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

// Switch A already on: starts vanished.
void ElectricRail::initAfterPlacement() {
    if (!MR::isValidSwitchA(this) || !MR::isOnSwitchA(this)) {
        return;
    }

    deactivate();
    setNerve(GET_NERVE(ElectricRail, ElectricRailNrvDisappeared));
}

// Waiting, the prebuilt mesh; vanishing, the narrowing rail written each frame.
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

void ElectricRail::deactivate() {
    MR::invalidateHitSensors(this);

    for (s32 i = 0; i < mPointCount; i++) {
        mPoints[i].mIsActive = false;
    }
}

// Ducking under it is safe.
void ElectricRail::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorPlayer(pReceiver) || MR::isPlayerSquat()) {
        return;
    }

    MR::sendMsgEnemyAttackElectric(pReceiver, pSender);
}

void ElectricRail::initMapToolInfo(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    MR::getPlacementArg0NoInit(rIter, &mRailHeight);
    if (mRailHeight < 1) {
        mRailHeight = 1;
    }

    if (MR::useStageSwitchReadA(this, rIter)) {
        MR::listenStageSwitchOnA(this, MR::Functor(this, &ElectricRail::disappear));
    }
}

void ElectricRail::initSensor() {
    initHitSensor(mRailHeight);

    mSensorPositions = new Vec3f[mRailHeight];

    for (s32 i = 0; i < mRailHeight; i++) {
        // The sensor keeps its name: the table's for the first eight, a name made here (in this actor's arena) past them.
        const char* pName;
        if (i < static_cast< s32 >(ARRAY_SIZE(cSensorNameTable))) {
            pName = cSensorNameTable[i];
        } else {
            char* const pMade = new char[16];
            nebula_snprintf(pMade, 16, "body%d", static_cast< int >(i));
            pName = pMade;
        }
        MR::addHitSensorPosMapObj(this, pName, 8, ElectricRailFunction::getHitSensorRadius(), &mSensorPositions[i], Vec3f(0.0f, 0.0f, 0.0f));
    }

    updateHitSensorPos();
}

// A post on every level of each rail point without arg 0; only the bottom ones cast a shadow.
void ElectricRail::initPoints() {
    s32 railPointNum = MR::getRailPointNum(this);

    s32 postPointCount = 0;
    for (s32 i = 0; i < railPointNum; i++) {
        s32 noPost = -1;
        if (!MR::getRailPointArg0NoInit(this, i, &noPost)) {
            postPointCount++;
        }
    }

    mPointCount = postPointCount * mRailHeight;
    mPoints = new ElectricRailPoint[mPointCount];

    s32 pointIdx = 0;
    for (s32 i = 0; i < railPointNum; i++) {
        s32 noPost = -1;
        if (MR::getRailPointArg0NoInit(this, i, &noPost)) {
            continue;
        }

        Vec3f pos;
        MR::calcRailPointPos(&pos, this, i);
        mPoints[pointIdx++].mPosition.set(pos);

        if (mRailHeight > 1) {
            Vec3f levelStep;
            calcGravity(&levelStep, pos);
            levelStep.scale(-cLevelHeight);

            for (s32 level = 1; level < mRailHeight; level++) {
                ElectricRailPoint& rPoint = mPoints[pointIdx++];
                pos.add(levelStep);
                rPoint.mPosition.set(pos);
                rPoint.mHasShadow = false;
            }
        }
    }

    for (s32 i = 0; i < mPointCount; i++) {
        mPoints[i].initWithoutIter();
    }
}

// A frame every interval along the rail, and one at its end (its start again on a loop).
void ElectricRail::initSeparators() {
    mSeparatorCount = static_cast< s32 >(MR::getRailTotalLength(this) / cSeparateInterval) + 1;
    mSeparators = new ElectricRailSeparator[mSeparatorCount];

    Vec3f railPos;
    Vec3f gravity;

    for (s32 i = 0; i < mSeparatorCount - 1; i++) {
        MR::setRailCoord(this, i * cSeparateInterval);
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
    drawPlane(cRailRadius, cRailRadius, -cRailRadius, -cRailRadius);
    drawPlane(-cRailRadius, cRailRadius, cRailRadius, -cRailRadius);
    mMesh.send();
}

// Two crossed planes of half-width `radius` along each level.
void ElectricRail::drawRail(TypedDraw& rDraw, f32 radius) const {
    drawPlaneQuad(rDraw, radius, radius, -radius, -radius);
    drawPlaneQuad(rDraw, -radius, radius, radius, -radius);
}

void ElectricRail::drawPlane(f32 side0, f32 up0, f32 side1, f32 up1) {
    for (s32 level = 0; level < mRailHeight; level++) {
        writePlaneStrip(mMesh.strip(2 * mSeparatorCount), mSeparators, mSeparatorCount, level, side0, up0, side1, up1);
    }
}

void ElectricRail::drawPlaneQuad(TypedDraw& rDraw, f32 side0, f32 up0, f32 side1, f32 up1) const {
    for (s32 level = 0; level < mRailHeight; level++) {
        writePlaneStrip(rDraw.vertices< VertexPT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 2 * mSeparatorCount), mSeparators, mSeparatorCount, level, side0, up0,
                        side1, up1);
    }
}

// A positive arg 2 drops the swept volume shadow; else a positive arg 1 drops a line shadow between the end posts.
void ElectricRail::initShadow(const TableIter& rIter) {
    f32 lineDropLength;
    MR::getPlacementArg1WithInit(rIter, &lineDropLength);

    f32 volumeDropLength;
    MR::getPlacementArg2WithInit(rIter, &volumeDropLength);

    if (!(0.0f < lineDropLength || 0.0f < volumeDropLength)) {
        return;
    }

    bool isVolume = 0.0f < volumeDropLength;
    f32 dropLength;

    if (isVolume) {
        MR::initShadowController(this, 2);
        dropLength = volumeDropLength;
    } else {
        MR::initShadowController(this, 3);
        dropLength = lineDropLength;
    }

    MR::addShadowVolumeCylinder(this, "start", cRailShadowWidth);
    MR::addShadowVolumeCylinder(this, "end", cRailShadowWidth);

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

    if (isVolume) {
        mShadowDrawer = new ElectricRailShadowDrawer(this, mSeparators, mSeparatorCount);
    } else {
        MR::addShadowVolumeLine(this, "line", this, "start", cRailShadowWidth, this, "end", cRailShadowWidth);
    }
}

// The bottom sensor follows the rail point nearest the player; the others stack a level apart above it.
void ElectricRail::updateHitSensorPos() {
    MR::calcNearestRailPos(mSensorPositions, this, *MR::getPlayerCenterPos());

    if (mRailHeight <= 1) {
        return;
    }

    Vec3f levelStep;
    calcGravity(&levelStep, *mSensorPositions);
    levelStep.scale(-cLevelHeight);

    for (s32 i = 1; i < mRailHeight; i++) {
        mSensorPositions[i].add(mSensorPositions[i - 1], levelStep);
    }
}

void ElectricRail::calcGravity(Vec3f* pOut, const Vec3f& rPos) const {
    if (mIsCalcGravity) {
        pOut->set(mGravity);
    } else if (!MR::calcGravityVectorOrZero(this, rPos, pOut, nullptr, 0)) {
        MR::calcDropShadowVector(this, rPos, pOut, nullptr, 0);
    }
}

void ElectricRail::exeDisappear() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_ELEC_RAIL_VANISH");
        deactivate();
    }

    mEaseIn = MR::calcNerveEaseInValue(this, cRailDisappearFrame, cRailRadius, 0.0f);

    if (MR::isStep(this, cRailDisappearFrame)) {
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
