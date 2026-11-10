#include "Game/Ride/Creeper.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Render/GameTexture.hpp"
#include <nebula_math.h>
#include "Game/System/NebulaInput.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHangUp_f1, "updateHangUp.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHangUp_f2, "updateHangUp.f2", 0.4f);

namespace NrvCreeper {
    NEW_NERVE(CreeperNrvFree, Creeper, Free);
    NEW_NERVE(CreeperNrvFreeInvalid, Creeper, FreeInvalid);
    NEW_NERVE(CreeperNrvHangStart, Creeper, HangStart);
    NEW_NERVE(CreeperNrvHangUp, Creeper, HangUp);
    NEW_NERVE(CreeperNrvHangDown, Creeper, HangDown);
};  // namespace NrvCreeper

namespace {
    NEBULA_TUNABLE(f32, sInterval, 50.0f);
    NEBULA_TUNABLE(s32, sHeadPointNo, 1);
    NEBULA_TUNABLE(f32, sDefaultBendRate, 100.0f);
    NEBULA_TUNABLE(f32, sDefaultJumpSpeedFront, 25.0f);
    NEBULA_TUNABLE(f32, sDefaultJumpSpeedUp, 40.0f);
    NEBULA_TUNABLE(f32, sFreeBackAccelRate, 0.05f);
    NEBULA_TUNABLE(f32, sPointFrictionFree, 0.9f);
    NEBULA_TUNABLE(f32, sPointFrictionHang, 0.7f);
    NEBULA_TUNABLE(f32, sHangStartRotateSpeed, 15.0f);
    NEBULA_TUNABLE(f32, sHangUpAccel, 5.0f);
    NEBULA_TUNABLE(f32, sHangUpSpeedMin, 5.0f);
    NEBULA_TUNABLE(f32, sHangUpSpeedMax, 15.0f);
    NEBULA_TUNABLE(f32, sHangDownGravity, 0.4f);
    NEBULA_TUNABLE(f32, sHangDownSpeedMax, 20.0f);
    NEBULA_TUNABLE(f32, sHangRotateSpeedRate, 1.5f);
    NEBULA_TUNABLE(f32, sHangStartCoordMin, 100.0f);
    NEBULA_TUNABLE(f32, sHangEndCoord, 80.0f);
    NEBULA_TUNABLE(s32, sAnimInterpoleFrame, 15);
    static const s32 sStepInvalidSpin = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(s32, sStepInvalid, 20);
    NEBULA_TUNABLE(f32, sDistanceNear, 200.0f);
    NEBULA_TUNABLE(f32, sDrawWidthX, 10.0f);
    NEBULA_TUNABLE(f32, sDrawWidthZ, 10.0f);

    static Color8 sColorPlusZ(0xFF, 0xFF, 0xFF, 0xFF);
    static Color8 sColorPlusX(0x96, 0x96, 0x96, 0xFF);
    static Color8 sColorMinusX(0xC8, 0xC8, 0xC8, 0xFF);
};  // namespace

CreeperPoint::CreeperPoint(const Vec3f& rPos, const Vec3f& rUp, const CreeperPoint* pPrevPoint)
    : mPosition(rPos), mNeutralPos(rPos), mVelocity(0.0f, 0.0f, 0.0f), mSide(1.0f, 0.0f, 0.0f), mUp(rUp), mFront(0.0f, 0.0f, 1.0f),
      mProjection(0.0f, 0.0f, 0.0f), mPrevPoint(pPrevPoint) {
    Vec3f front = mSide.cross(mUp);
    if (MR::isNearZero(front)) {
        MR::makeAxisUpFront(&mSide, &mFront, mUp, mFront);
    } else {
        MR::makeAxisUpSide(&mFront, &mSide, mUp, mSide);
    }

    if (mPrevPoint == nullptr) {
        return;
    }

    Vec3f posDiff = mPosition - pPrevPoint->mPosition;
    mProjection.x = pPrevPoint->mSide.dot(posDiff);
    mProjection.y = pPrevPoint->mUp.dot(posDiff);
    mProjection.z = pPrevPoint->mFront.dot(posDiff);
}

void CreeperPoint::updateFree() {
    Vec3f restoreVec(mNeutralPos - mPosition);

    mVelocity.add(restoreVec * ::sFreeBackAccelRate);
    mPosition.add(mVelocity);
    mVelocity.mult(::sPointFrictionFree);

    updateLocalAxis();

    mPosition.set(mUp);
    mPosition.scale(::sInterval);
    mPosition.add(mPrevPoint->mPosition);
}

void CreeperPoint::updateBend(bool bend, const Vec3f& bendDirection, f32 t, f32 bendRate) {
    mVelocity.mult(::sPointFrictionHang);

    if (bend) {
        mVelocity.add(bendDirection * t * bendRate);
    }

    mPosition = mPrevPoint->mSide * mProjection.x + mPrevPoint->mUp * mProjection.y + mPrevPoint->mFront * mProjection.z + mPrevPoint->mPosition;

    mPosition.add(mVelocity);

    updateLocalAxis();

    mPosition.set(mUp);
    mPosition.scale(::sInterval);
    mPosition.add(mPrevPoint->mPosition);
}

void CreeperPoint::updateLocalAxis() {
    mUp.set(mPosition);
    mUp.sub(mPrevPoint->mPosition);
    MR::normalize(&mUp);

    Vec3f front = mSide.cross(mUp);
    if (MR::isNearZero(front)) {
        MR::makeAxisUpFront(&mSide, &mFront, mUp, mFront);
    } else {
        MR::makeAxisUpSide(&mFront, &mSide, mUp, mSide);
    }
}

Creeper::Creeper(const char* pName)
    : LiveActor(pName), mNumPoints(), mPoints(), mRider(), mFront(0.0f, 0.0f, 1.0f), mUp(0.0f, 1.0f, 0.0f), mHangSpeed(), mCoord(),
      mBendRate(::sDefaultBendRate), mBendRateBee(-1.0f), mBendDirection(0.0f, 0.0f, 0.0f), mJumpSpeedFront(::sDefaultJumpSpeedFront),
      mJumpSpeedUp(::sDefaultJumpSpeedUp), mCameraInfo(), mClippingCenter(0.0f, 0.0f, 0.0f), mTexture(), mFlowerModel(), mLeafModel() {
    mTopMtx.identity();
}

void Creeper::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    MR::getPlacementArg0NoInit(rIter, &mBendRate);
    MR::getPlacementArg1NoInit(rIter, &mJumpSpeedFront);
    MR::getPlacementArg2NoInit(rIter, &mJumpSpeedUp);
    MR::getPlacementArg3NoInit(rIter, &mBendRateBee);

    mBendRate /= ::sDefaultBendRate;
    if (mBendRateBee > 0.0f) {
        mBendRateBee /= ::sDefaultBendRate;
    } else {
        mBendRateBee = mBendRate;
    }

    MR::connectToScene(this, MR::MovementType_Ride, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_Creeper);

    initHitSensor(3);
    MR::addHitSensorBinder(this, "bind", 8, cNum_init_f1, Vec3f(0.0f, 0.0f, 0.0f));
    initRailRider(rIter);

    mPosition.set(MR::getRailPos(this));
    initPoints();

    initNerve(GET_NERVE(Creeper, CreeperNrvFree));

    MR::initMultiActorCamera(this, rIter, &mCameraInfo, "掴まり");

    MR::initAndSetRailClipping(&mClippingCenter, this, cNum_init_f2, cNum_init_f3);

    initSound(4, false);

    mTexture = new GameTexture(MR::loadTexFromArc("Creeper", "Stalk.bti"));

    mFlowerModel = MR::createPartsModelNoSilhouettedMapObj(this, "花（つる花）", "CreeperFlower", mTopMtx.toMtxPtr());
    mLeafModel = MR::createPartsModelNoSilhouettedMapObj(this, "葉（つる花）", "CreeperLeaf", nullptr);

    MR::registerDemoSimpleCastAll(mFlowerModel);
    MR::registerDemoSimpleCastAll(mLeafModel);
    MR::copyTransRotateScale(this, mLeafModel);
    MR::startBck(mLeafModel, "Wait");
    mLeafModel->makeActorAppeared();
    makeActorAppeared();
}

void Creeper::exeFree() {
}

void Creeper::exeFreeInvalid() {
    if (MR::isNearPlayer(this, ::sDistanceNear) && MR::isGreaterStep(this, ::sStepInvalid)) {
        setNerve(GET_NERVE(Creeper, CreeperNrvFree));
    }
}

void Creeper::exeHangStart() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("GrowPlantCatch", ::sAnimInterpoleFrame);
    }

    if (!updateHangUp(::sHangStartRotateSpeed) && MR::isSpin(PLAYER_1)) {
        setNerve(GET_NERVE(Creeper, CreeperNrvHangUp));
    }
}

void Creeper::exeHangUp() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("GrowPlantHangUp", ::sAnimInterpoleFrame);
    }

    if (!updateHangUp(mHangSpeed * ::sHangRotateSpeedRate) && MR::isSpin(PLAYER_1)) {
        mHangSpeed += ::sHangUpAccel;
        MR::tryRumblePadMiddle(this, PLAYER_1);
    }
}

void Creeper::exeHangDown() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("GrowPlantHangDown", ::sAnimInterpoleFrame);
    }

    if (tryHangUp()) {
        return;
    }

    mHangSpeed += ::sHangDownGravity;
    mHangSpeed = MR::clamp(mHangSpeed, 0.0f, ::sHangDownSpeedMax);

    if (MR::isRailReachedNearGoal(this, ::sHangEndCoord)) {
        MR::endMultiActorCamera(this, mCameraInfo, "掴まり", true);
        MR::endBindAndPlayerJump(this, Vec3f(0.0f, 0.0f, 0.0f), 0);
        MR::startSound(mRider, "SE_PV_JUMP_S");
        MR::startSound(this, "SE_OJ_CREEPER_SWING");
        mRider = nullptr;
        setNerve(GET_NERVE(Creeper, CreeperNrvFreeInvalid));
    } else {
        MR::moveCoord(this, mHangSpeed);
        MR::startLevelSound(mRider, "SE_OJ_LV_CREEPER_DOWN");
        calcAndGetCurrentInfo(&mPosition, &mUp);
        MR::rotateVecDegree(&mFront, mUp, mHangSpeed * ::sHangRotateSpeedRate);
    }
}

void Creeper::initPoints() {
    s32 numPoints = MR::getRailTotalLength(this) / ::sInterval;
    mNumPoints = numPoints + 1;

    mPoints = new CreeperPoint*[mNumPoints];

    CreeperPoint* prevPoint = nullptr;

    Vec3f bendSum(0.0f, 0.0f, 0.0f);  // cumulative direction

    for (s32 i = 0; i < mNumPoints - 1; i++) {
        MR::setRailCoord(this, ::sInterval * i);
        mPoints[i] = new CreeperPoint(MR::getRailPos(this), MR::getRailDirection(this), prevPoint);
        bendSum.add(MR::getRailDirection(this));
        prevPoint = mPoints[i];
    }

    MR::moveCoordToEndPos(this);

    mPoints[mNumPoints - 1] = new CreeperPoint(MR::getRailPos(this), MR::getRailDirection(this), prevPoint);

    bendSum.add(MR::getRailDirection(this));
    mBendDirection.set(bendSum);
    MR::vecKillElement(mBendDirection, mGravity, &mBendDirection);
    MR::normalize(&mBendDirection);
}

void Creeper::control() {
    if (mRider == nullptr) {
        updatePoints();
        MR::moveCoordAndTransToNearestRailPos(this, *MR::getPlayerPos());
    } else {
        bendPoints();
        // A rail with no length is already at its end.
        const f32 railLength = MR::getRailTotalLength(this);
        mCoord = railLength > 0.0f ? MR::getRailCoord(this) / railLength : 1.0f;
    }

    mTopMtx.setXYZDir(getHeadPoint()->mSide, getHeadPoint()->mUp, getHeadPoint()->mFront);
    mTopMtx.setTrans(getHeadPoint()->mPosition);
}

bool Creeper::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isMsgAutoRushBegin(msg)) {
        if (isNerve(GET_NERVE(Creeper, CreeperNrvFreeInvalid)) || (!MR::isPlayerSwingAction() && MR::isOnGroundPlayer())) {
            return false;
        }

        mRider = MR::getSensorHost(pSender);
        MR::startSound(mRider, "SE_PV_CATCH");
        MR::startSound(mRider, "SE_PM_GRAB_OBJ");
        MR::invalidateClipping(this);
        MR::moveCoordAndTransToNearestRailPos(this, *MR::getPlayerPos());

        if (MR::getRailCoord(this) < ::sHangStartCoordMin) {
            MR::setRailCoord(this, ::sHangStartCoordMin);
        }

        MR::getPlayerFrontVec(&mFront);
        mHangSpeed = -mGravity.dot(*MR::getPlayerVelocity());

        MR::setRailDirectionToEnd(this);
        if (mHangSpeed > 0.0f) {
            mHangSpeed = MR::clamp(mHangSpeed, ::sHangUpSpeedMin, ::sHangUpSpeedMax);
            setNerve(GET_NERVE(Creeper, CreeperNrvHangStart));
        } else {
            mHangSpeed = 0.0f;
            MR::setRailDirectionToStart(this);
            setNerve(GET_NERVE(Creeper, CreeperNrvHangDown));
        }

        MR::tryRumblePadMiddle(this, PLAYER_1);

        MR::startMultiActorCameraNoTarget(this, mCameraInfo, "掴まり");
        return true;
    }

    if (MR::isMsgUpdateBaseMtx(msg)) {
        Pos3f mtx;
        MR::makeMtxUpFrontPos(&mtx, mUp, mFront, mPosition);
        MR::setBaseTRMtx(mRider, mtx);
        return true;
    }

    if (MR::isMsgRushCancel(msg)) {
        MR::endMultiActorCamera(this, mCameraInfo, "掴まり", true);
        MR::endBindAndPlayerJump(this, Vec3f(0.0f, 0.0f, 0.0f), 0);
        mRider = nullptr;
        setNerve(GET_NERVE(Creeper, CreeperNrvFreeInvalid));
        return true;
    }

    return false;
}

void Creeper::updatePoints() {
    for (s32 i = ::sHeadPointNo; i < mNumPoints; i++) {
        mPoints[i]->updateFree();
    }
}

void Creeper::bendPoints() {
    s32 numPoints = static_cast< s32 >(MR::getRailCoord(this) / ::sInterval) + 1;
    if (numPoints > mNumPoints) {
        numPoints = mNumPoints;
    }

    f32 bendRate = mBendRate;
    if (MR::isPlayerElementModeBee()) {
        bendRate = mBendRateBee;
    }

    for (s32 i = ::sHeadPointNo; i < numPoints; i++) {
        mPoints[i]->updateBend(true, mBendDirection, mCoord, bendRate);
    }

    for (s32 i = numPoints; i < mNumPoints; i++) {
        mPoints[i]->updateBend(false, mBendDirection, mCoord, bendRate);
    }
}

bool Creeper::tryHangUp() {
    if (MR::isSpin(PLAYER_1)) {
        mHangSpeed = ::sHangUpAccel;
        MR::reverseRailDirection(this);
        MR::tryRumblePadMiddle(this, PLAYER_1);
        setNerve(GET_NERVE(Creeper, CreeperNrvHangUp));
        return true;
    }

    return false;
}

bool Creeper::tryJump() {
    if (!MR::isRailReachedGoal(this)) {
        return false;
    }

    MR::moveCoordToEndPos(this);
    mCoord = 1.0f;
    for (s32 i = 0; i < 5; i++) {
        bendPoints();
    }

    calcAndGetCurrentInfo(&mPosition, &mUp);

    Vec3f launchFront = -mBendDirection;

    MR::vecKillElement(launchFront, mGravity, &launchFront);
    MR::normalizeOrZero(&launchFront);
    if (!MR::isNearZero(launchFront)) {
        MR::setPlayerFrontTargetVec(launchFront, 1);
    }

    Vec3f launch = launchFront * mJumpSpeedFront - mGravity * mJumpSpeedUp;

    MR::startBckPlayer("GrowPlantJump");
    MR::endMultiActorCamera(this, mCameraInfo, "掴まり", true);
    MR::endBindAndPlayerForceWeakGravityJump(this, launch);

    MR::setPlayerSwingInhibitTimer(::sStepInvalidSpin);
    MR::stopSound(mRider, "SE_OJ_CREEPER_UP_START");
    MR::startSound(mRider, "SE_PV_JUMP_JOY");
    MR::startSound(mRider, "SE_PM_JUMP_L");
    MR::startSound(this, "SE_OJ_CREEPER_FLIP");
    mRider = nullptr;
    setNerve(GET_NERVE(Creeper, CreeperNrvFreeInvalid));

    return true;
}

bool Creeper::updateHangUp(f32 rotateSpeed) {
    if (MR::isSpin(PLAYER_1)) {
        mHangSpeed += ::sHangUpAccel;
        MR::tryRumblePadMiddle(this, PLAYER_1);
    }

    if (MR::isFirstStep(this) && mHangSpeed >= cNum_updateHangUp_f1) {
        MR::startSound(mRider, "SE_OJ_CREEPER_UP_START");
    }

    mHangSpeed -= cNum_updateHangUp_f2;

    mHangSpeed = mHangSpeed >= ::sHangUpSpeedMax ? ::sHangUpSpeedMax : mHangSpeed;

    if (mHangSpeed < 0.0f) {
        mHangSpeed = 0.0f;
        MR::reverseRailDirection(this);
        setNerve(GET_NERVE(Creeper, CreeperNrvHangDown));
        return true;
    }

    MR::moveCoord(this, mHangSpeed);
    MR::startLevelSound(mRider, "SE_OJ_LV_CREEPER_UP");

    if (tryJump()) {
        return true;
    }

    calcAndGetCurrentInfo(&mPosition, &mUp);
    MR::rotateVecDegree(&mFront, mUp, rotateSpeed);
    return false;
}

void Creeper::calcAndGetCurrentInfo(Vec3f* pPosition, Vec3f* pUp) const {
    f32 t0 = MR::getRailCoord(this) / ::sInterval;
    s32 i = t0;
    f32 t = t0 - i;

    if (i < mNumPoints - 1) {
        s32 nextIdx = i + 1;
        *pPosition = mPoints[i]->mPosition * (1.0f - t) + mPoints[nextIdx]->mPosition * t;
        *pUp = mPoints[nextIdx]->mPosition - mPoints[i]->mPosition;
    } else {
        // A chain of one point has no segment: keep the caller's up.
        if (mNumPoints < 2) {
            pPosition->set(mPoints[0]->mPosition);
            return;
        }
        if (i > mNumPoints - 1) {
            i = mNumPoints - 1;
        }
        s32 prevIdx = i - 1;
        pPosition->set(mPoints[i]->mPosition);
        *pUp = mPoints[i]->mPosition - mPoints[prevIdx]->mPosition;
    }
    MR::normalize(pUp);
}

namespace {
    void sendVertex(VertexPCT*& rVtx, const Vec3f& rPos, const Vec3f& rSide, const Vec3f& rFront, f32 x, f32 y, u32 color, f32 texX, f32 texY) {
        *rVtx++ = {rPos.x + rSide.x * x + rFront.x * y, rPos.y + rSide.y * x + rFront.y * y, rPos.z + rSide.z * x + rFront.z * y, Rgba8(color), texX, texY};
    }

    void drawPolygon(VertexPCT*& rVtx, const Vec3f& rPos, const Vec3f& rSide, const Vec3f& rFront, f32 x1, f32 y1, f32 x2, f32 y2, u32 color1, u32 color2,
                     f32 texY) {
        ::sendVertex(rVtx, rPos, rSide, rFront, x1, y1, color1, 1.0f, texY);
        ::sendVertex(rVtx, rPos, rSide, rFront, x2, y2, color2, 0.0f, texY);
    }
};  // namespace

void Creeper::draw() const {
    if (!MR::isValidDraw(this)) {
        return;
    }

    const SiteMaterial material = loadMaterial();
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());

    f32 delta = mNumPoints > 1 ? 1.0f / (mNumPoints - 1) : 0.0f;

    VertexPCT* vtx = site.vertices< VertexPCT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, mNumPoints * 2);
    for (s32 i = 0; i < mNumPoints; i++) {
        ::drawPolygon(vtx, mPoints[i]->getPosition(), mPoints[i]->getSide(), mPoints[i]->getFront(), ::sDrawWidthX, -::sDrawWidthZ, 0.0f, ::sDrawWidthZ,
                      ::sColorPlusX, ::sColorPlusZ, delta * ((mNumPoints - 1) - i));
    }

    vtx = site.vertices< VertexPCT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, mNumPoints * 2);
    for (s32 i = 0; i < mNumPoints; i++) {
        ::drawPolygon(vtx, mPoints[i]->getPosition(), mPoints[i]->getSide(), mPoints[i]->getFront(), 0.0f, ::sDrawWidthZ, -::sDrawWidthX, -::sDrawWidthZ,
                      ::sColorPlusZ, ::sColorMinusX, delta * ((mNumPoints - 1) - i));
    }

    vtx = site.vertices< VertexPCT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, mNumPoints * 2);
    for (s32 i = 0; i < mNumPoints; i++) {
        ::drawPolygon(vtx, mPoints[i]->getPosition(), mPoints[i]->getSide(), mPoints[i]->getFront(), -::sDrawWidthX, -::sDrawWidthZ, ::sDrawWidthX,
                      -::sDrawWidthZ, ::sColorMinusX, ::sColorPlusX, delta * ((mNumPoints - 1) - i));
    }
}

SiteMaterial Creeper::loadMaterial() const {
    // runtime/src/render/materials/tex_modulate.hlsl: the texture times the vertex colour, its alpha the texture's; drawn where
    // the alpha is above 0, depth tested and written, back faces culled.
    SiteMaterial material;
    material.init(NEBULA_SHADER_TEX_MODULATE, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, true, NEBULA_CULL_BACK);
    material.constants[0] = 1.0f;
    material.constants[1] = 0.0f;
    material.setTexture(0, mTexture);
    return material;
}
