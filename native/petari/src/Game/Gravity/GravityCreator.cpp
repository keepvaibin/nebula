#include "Game/Gravity.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_settingFromSRT_f1, "settingFromSRT.f1", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_settingFromSRT_f2, "settingFromSRT.f2", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_settingFromPlacementArgs_f1, "settingFromPlacementArgs.f1", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_settingFromSRT_f3, "settingFromSRT.f3", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_settingFromSRT_f4, "settingFromSRT.f4", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_settingFromPlacementArgs_f2, "settingFromPlacementArgs.f2", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_settingFromSRT_f5, "settingFromSRT.f5", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_settingFromSRT_f6, "settingFromSRT.f6", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_settingFromSRT_f7, "settingFromSRT.f7", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_settingFromSRT_f8, "settingFromSRT.f8", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_settingFromSRT_f9, "settingFromSRT.f9", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_settingFromPlacementArgs_f3, "settingFromPlacementArgs.f3", 360.0f);

GravityCreator::GravityCreator() {
}

PlanetGravity* GravityCreator::createFromPlacement(const TableIter& rIter) {
    PlanetGravity* instance = createInstance();

    if (instance) {
        Vec3f trans(0, 0, 0);
        Vec3f rotate(0, 0, 0);
        Vec3f scale(1, 1, 1);
        MR::getPlacementTrans(rIter, &trans);
        MR::getPlacementRotate(rIter, &rotate);
        MR::getPlacementScale(rIter, &scale);
        settingFromSRT(trans, rotate, scale);

        s32 objArg0;
        s32 objArg1;
        s32 objArg2;
        MR::getPlacementArg0WithInit(rIter, &objArg0);
        MR::getPlacementArg1WithInit(rIter, &objArg1);
        MR::getPlacementArg2WithInit(rIter, &objArg2);
        settingFromPlacementArgs(objArg0, objArg1, objArg2);

        settingFromPlacementOtherParam(rIter);

        MR::settingGravityParamFromPlacement(instance, rIter);
        getGravity()->updateIdentityMtx();
        MR::registerGravity(instance);
    }

    return instance;
}

PlanetGravity* CubeGravityCreator::createInstance() {
    mGravityInstance = new CubeGravity();
    return mGravityInstance;
}

void CubeGravityCreator::settingFromSRT(const Vec3f& rTrans, const Vec3f& rRotate, const Vec3f& rScale) {
    Pos3f mtx;
    MR::makeMtxTR(mtx.toMtxPtr(), rTrans, rRotate);

    Vec3f upVec;
    Vec3f scale;
    scale.set(rScale * cNum_settingFromSRT_f1);
    mtx.getYDir(upVec);

    // The placement point is the bottom face's centre.
    mtx.setTrans(rTrans + upVec * scale.y);
    MR::preScaleMtx(mtx.toMtxPtr(), scale);

    mGravityInstance->setCube(mtx);
}

void CubeGravityCreator::settingFromPlacementArgs(s32 xFaces, s32 yFaces, s32 zFaces) {
    // Per axis: bit 0 turns on the positive face, bit 1 the negative one.
    mGravityInstance->mActiveFaces = static_cast< u8 >((xFaces & 3) | (yFaces & 3) << 2 | (zFaces & 3) << 4);
}

PlanetGravity* CubeGravityCreator::getGravity() {
    return mGravityInstance;
}

PlanetGravity* DiskGravityCreator::createInstance() {
    mGravityInstance = new DiskGravity();
    return mGravityInstance;
}

void DiskGravityCreator::settingFromSRT(const Vec3f& rTrans, const Vec3f& rRotate, const Vec3f& rScale) {
    Pos3f mtx;
    MR::makeMtxTR(mtx.toMtxPtr(), rTrans, rRotate);

    Vec3f sideVec, upVec;
    mtx.getXDir(sideVec);
    mtx.getYDir(upVec);

    mGravityInstance->setLocalPosition(rTrans);
    mGravityInstance->setLocalDirection(upVec);
    mGravityInstance->setSideDirection(sideVec);
    mGravityInstance->setRadius(cNum_settingFromSRT_f2 * MR::getMaxElement(rScale));
}

void DiskGravityCreator::settingFromPlacementArgs(s32 bothSides, s32 edgeGravity, s32 validDegree) {
    mGravityInstance->setBothSide(bothSides != 0);
    mGravityInstance->setEnableEdgeGravity(edgeGravity != 0);

    // A negative angle is the whole disk.
    if (validDegree >= 0) {
        mGravityInstance->setValidDegree(validDegree);
    } else {
        mGravityInstance->setValidDegree(cNum_settingFromPlacementArgs_f1);
    }
}

PlanetGravity* DiskGravityCreator::getGravity() {
    return mGravityInstance;
}

PlanetGravity* DiskTorusGravityCreator::createInstance() {
    mGravityInstance = new DiskTorusGravity();
    return mGravityInstance;
}

void DiskTorusGravityCreator::settingFromSRT(const Vec3f& rTrans, const Vec3f& rRotate, const Vec3f& rScale) {
    Pos3f mtx;
    MR::makeMtxTR(mtx.toMtxPtr(), rTrans, rRotate);

    Vec3f upVec;
    mtx.getYDir(upVec);

    mGravityInstance->setPosition(rTrans);
    mGravityInstance->setDirection(upVec);
    mGravityInstance->setRadius(cNum_settingFromSRT_f3 * MR::getMaxElement(rScale));
}

void DiskTorusGravityCreator::settingFromPlacementArgs(s32 bothSides, s32 edgeType, s32 diskRadius) {
    mGravityInstance->setBothSide(bothSides != 0);

    // Unknown edge types pull from both edges.
    if (edgeType == DiskTorusGravity::EdgeType_DisableBoth || edgeType == DiskTorusGravity::EdgeType_DisableOuter ||
        edgeType == DiskTorusGravity::EdgeType_DisableInner) {
        mGravityInstance->setEdgeType(edgeType);
    } else {
        mGravityInstance->setEdgeType(DiskTorusGravity::EdgeType_EnableBoth);
    }

    mGravityInstance->setDiskRadius(diskRadius);
}

PlanetGravity* DiskTorusGravityCreator::getGravity() {
    return mGravityInstance;
}

PlanetGravity* ConeGravityCreator::createInstance() {
    mGravityInstance = new ConeGravity();
    return mGravityInstance;
}

void ConeGravityCreator::settingFromSRT(const Vec3f& rTrans, const Vec3f& rRotate, const Vec3f& rScale) {
    Pos3f localMtx;
    MR::makeMtxTRS(localMtx.toMtxPtr(), rTrans, rRotate, rScale * cNum_settingFromSRT_f4);
    mGravityInstance->setLocalMatrix(localMtx);
}

void ConeGravityCreator::settingFromPlacementArgs(s32 enableBottom, s32 topCutPermille, s32) {
    mGravityInstance->setEnableBottom(enableBottom != 0);
    mGravityInstance->setTopCutRate(topCutPermille / cNum_settingFromPlacementArgs_f2);
}

PlanetGravity* ConeGravityCreator::getGravity() {
    return mGravityInstance;
}

PlanetGravity* PlaneGravityCreator::createInstance() {
    mGravityInstance = new ParallelGravity();
    return mGravityInstance;
}

void PlaneGravityCreator::settingFromSRT(const Vec3f& rTrans, const Vec3f& rRotate, const Vec3f& /*rScale*/) {
    Pos3f mtx;
    MR::makeMtxRotate(mtx.toMtxPtr(), rRotate);

    Vec3f upVec;
    mtx.getYDir(upVec);
    mGravityInstance->setPlane(upVec, rTrans);
}

PlanetGravity* PlaneGravityCreator::getGravity() {
    return mGravityInstance;
}

PlanetGravity* PlaneInBoxGravityCreator::createInstance() {
    mGravityInstance = new ParallelGravity();
    mGravityInstance->setRangeType(ParallelGravity::RangeType_Box);
    return mGravityInstance;
}

void PlaneInBoxGravityCreator::settingFromSRT(const Vec3f& rTrans, const Vec3f& rRotate, const Vec3f& rScale) {
    Pos3f mtx;
    Vec3f scale = rScale * cNum_settingFromSRT_f5;
    MR::makeMtxTR(mtx.toMtxPtr(), rTrans, rRotate);

    Vec3f upVec;
    mtx.getYDir(upVec);

    // The placement point is the box's bottom face centre.
    mtx.setTrans(rTrans + upVec * scale.y);
    MR::preScaleMtx(mtx.toMtxPtr(), scale);

    mGravityInstance->setPlane(upVec, rTrans);
    mGravityInstance->setRangeBox(mtx);
}

PlanetGravity* PlaneInBoxGravityCreator::getGravity() {
    return mGravityInstance;
}

void PlaneInBoxGravityCreator::settingFromPlacementArgs(s32 baseDistance, s32 distanceAxis, s32) {
    mGravityInstance->setBaseDistance(baseDistance);

    switch (distanceAxis) {
    case 0:
        mGravityInstance->setDistanceCalcType(ParallelGravity::DistanceCalcType_X);
        break;
    case 1:
        mGravityInstance->setDistanceCalcType(ParallelGravity::DistanceCalcType_Y);
        break;
    case 2:
        mGravityInstance->setDistanceCalcType(ParallelGravity::DistanceCalcType_Z);
        break;
    default:
        mGravityInstance->setDistanceCalcType(ParallelGravity::DistanceCalcType_Default);
        break;
    }
}

PlanetGravity* PlaneInCylinderGravityCreator::createInstance() {
    mGravityInstance = new ParallelGravity();
    mGravityInstance->setRangeType(ParallelGravity::RangeType_Cylinder);
    return mGravityInstance;
}

PlanetGravity* PlaneInCylinderGravityCreator::getGravity() {
    return mGravityInstance;
}

void PlaneInCylinderGravityCreator::settingFromSRT(const Vec3f& rTrans, const Vec3f& rRotate, const Vec3f& rScale) {
    Pos3f mtx;
    MR::makeMtxTR(mtx.toMtxPtr(), rTrans, rRotate);

    Vec3f upVec;
    mtx.getYDir(upVec);
    mGravityInstance->setPlane(upVec, rTrans);
    mGravityInstance->setRangeCylinder(cNum_settingFromSRT_f6 * rScale.x, cNum_settingFromSRT_f7 * rScale.y);
}

void PlaneInCylinderGravityCreator::settingFromPlacementArgs(s32 baseDistance, s32, s32) {
    mGravityInstance->setBaseDistance(baseDistance);
}

PlanetGravity* PointGravityCreator::createInstance() {
    mGravityInstance = new PointGravity();
    return mGravityInstance;
}

void PointGravityCreator::settingFromSRT(const Vec3f& rTrans, const Vec3f& /*rRotate*/, const Vec3f& rScale) {
    mGravityInstance->mDistant = cNum_settingFromSRT_f8 * rScale.x;
    mGravityInstance->setOrig(rTrans);
}

PlanetGravity* PointGravityCreator::getGravity() {
    return mGravityInstance;
}

PlanetGravity* SegmentGravityCreator::createInstance() {
    mGravityInstance = new SegmentGravity();
    return mGravityInstance;
}

void SegmentGravityCreator::settingFromSRT(const Vec3f& rTrans, const Vec3f& rRotate, const Vec3f& rScale) {
    Pos3f mtx;
    MR::makeMtxTRS(mtx.toMtxPtr(), rTrans, rRotate, rScale);

    Vec3f sideVec, upVec;
    mtx.getXDir(sideVec);
    mtx.getYDir(upVec);

    mGravityInstance->setGravityPoint(0, rTrans);
    mGravityInstance->setGravityPoint(1, rTrans + upVec * cNum_settingFromSRT_f9);
    mGravityInstance->setSideVector(sideVec);
}

void SegmentGravityCreator::settingFromPlacementArgs(s32 validEdges, s32 validSideDegree, s32) {
    // 0 neither end, 1 the start, 2 the end, anything else both.
    switch (validEdges) {
    case 0:
        mGravityInstance->setEdgeValid(0, false);
        mGravityInstance->setEdgeValid(1, false);
        break;
    case 1:
        mGravityInstance->setEdgeValid(0, true);
        mGravityInstance->setEdgeValid(1, false);
        break;
    case 2:
        mGravityInstance->setEdgeValid(0, false);
        mGravityInstance->setEdgeValid(1, true);
        break;
    default:
        mGravityInstance->setEdgeValid(0, true);
        mGravityInstance->setEdgeValid(1, true);
        break;
    }

    // A negative angle is all the way round.
    if (validSideDegree >= 0) {
        mGravityInstance->setValidSideDegree(validSideDegree);
    } else {
        mGravityInstance->setValidSideDegree(cNum_settingFromPlacementArgs_f3);
    }
}

PlanetGravity* SegmentGravityCreator::getGravity() {
    return mGravityInstance;
}

PlanetGravity* WireGravityCreator::createInstance() {
    mGravityInstance = new WireGravity();
    return mGravityInstance;
}

void WireGravityCreator::settingFromPlacementOtherParam(const TableIter& rIter) {
    mRailRider = new RailRider(rIter);
    s32 numPointsInBetween = 20;
    MR::getPlacementArg0NoInit(rIter, &numPointsInBetween);

    // A count below 0 (a placement other than the unset -1) is no points in between: numPoints is unsigned below.
    if (numPointsInBetween < 0) {
        numPointsInBetween = 0;
    }

    // Points from the rail's start at even intervals. The rail's end is not one: the last slot stays empty.
    u32 numPoints = numPointsInBetween + 2;
    f32 railInterval = mRailRider->getTotalLength() / (numPoints - 1);
    mGravityInstance->setPointListSize(numPoints);

    mRailRider->setCoord(0.0f);
    mRailRider->setSpeed(railInterval);

    for (u32 i = 0; i < numPoints - 1; i++) {
        mGravityInstance->addPoint(mRailRider->mCurPos);
        mRailRider->move();
    }
}

PlanetGravity* WireGravityCreator::getGravity() {
    return mGravityInstance;
}
