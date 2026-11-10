#include "Game/Map/OceanRingPipe.hpp"
#include "Game/Map/Flag.hpp"
#include "Game/Map/OceanRing.hpp"
#include "Game/Map/OceanRingPipeInside.hpp"
#include "Game/Map/OceanRingPipeOutside.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f1, "initPoints.f1", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f2, "initPoints.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f3, "initPoints.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f4, "initPoints.f4", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f5, "initPoints.f5", 0.0001f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f6, "initPoints.f6", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f7, "initPoints.f7", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f8, "initPoints.f8", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f9, "initPoints.f9", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f10, "initPoints.f10", 2.0f);

OceanRingPipe::OceanRingPipe(const OceanRing* pRing, f32 a, f32 b)
    : LiveActor("オーシャンリングの側面"), mOceanRing(pRing), mIsCameraInside(), mNumPoints(), mNumSegments(), mNumSides(8), mPositions(), mNormals(), mWidth(a), mWidthArg(b), mPipeInside(),
      mPipeOutside() {
    lumacore::setClass(this, lumacore::kClassId< OceanRingPipe >);
}

void OceanRingPipe::init(const TableIter& rIter) {
    MR::connectToSceneMapObjMovement(this);
    initRailRider(rIter);
    initPoints();
    MR::invalidateClipping(this);
    mPipeInside = new OceanRingPipeInside(this);
    mPipeInside->initWithoutIter();
    mPipeOutside = new OceanRingPipeOutside(this);
    mPipeOutside->initWithoutIter();

    if (MR::isName(mOceanRing, "オーシャンリング（旗）")) {
        f32 flagWidth = cNum_init_f1;
        f32 flagHeight = cNum_init_f2;
        f32 stickLength = cNum_init_f3;
        Vec3f side(1.0f, 0.0f, 0.0f);
        for (s32 i = 0; i < mNumSegments; i += 7) {
            Vec3f grav(0.0f, -1.0f, 0.0f);
            MR::calcGravityVector(this, mPositions[calcPointIndex(i, 0)], &grav, nullptr, 0);
            if (grav.y > -cNum_init_f4) {
                continue;
            }

            if (i & 1) {
                MR::setClippingFarMax(
                    MR::createMapFlag("旗", "FlagSurfing", &mPositions[calcPointIndex(i, 0)], side, stickLength, flagWidth, flagHeight, 2, 3));
            } else {
                MR::setClippingFarMax(MR::createMapFlag("旗", "FlagSurfing", &mPositions[calcPointIndex(i, mNumSides - 1)], side, stickLength, flagWidth,
                                                        flagHeight, 2, 3));
            }
        }
    }

    makeActorAppeared();
}

void OceanRingPipe::movement() {
    if (!MR::isValidMovement(this)) {
        return;
    }

    mPipeOutside->movement();
    if (WaterAreaFunction::getCameraWaterInfo()->mOceanRing == mOceanRing) {
        mIsCameraInside = true;
        mPipeInside->movement();
    } else {
        mIsCameraInside = false;
    }
}

void OceanRingPipe::initPoints() {
    mNumSegments = static_cast< s32 >(MR::getRailTotalLength(this) / cNum_initPoints_f1) + 1;
    mNumPoints = mNumSegments * mNumSides;

    // A rail shorter than one segment (one cross-section) steps its whole length, not a division by 0.
    f32 length = MR::getRailTotalLength(this) / (mNumSegments > 1 ? mNumSegments - 1 : 1);
    f32 angle = getAngle();

    mPositions = new Vec3f[mNumPoints];
    mNormals = new Vec3f[mNumPoints];

    s32 idx = 0;

    for (s32 i = 0; i < mNumSegments; i++) {
        Vec3f grav;
        MR::calcGravityVector(this, MR::getRailPos(this), &grav, nullptr, 0);

        Vec3f side;
        side.cross(MR::getRailDirection(this), -grav);
        MR::normalize(&side);

        Pos3f mtx;
        mtx.identity();
        mtx.setRotate(MR::getRailDirection(this), angle);

        f32 currDist = 0.0f;
        f32 nextDist = 0.0f;
        MR::calcDistanceToCurrentAndNextRailPoint(this, &currDist, &nextDist);
        f32 sumDist = currDist + nextDist;

        // A rail point's arg 1 overrides the pipe's width there; between points the width blends by distance.
        f32 currPointWidth = mWidthArg / cNum_initPoints_f2;
        f32 nextPointWidth = mWidthArg / cNum_initPoints_f3;
        MR::getCurrentRailPointArg1NoInit(this, &currPointWidth);
        MR::getNextRailPointArg1NoInit(this, &nextPointWidth);

        f32 widthScale = 1.0f;
        Vec3f normal(side);

        if (sumDist > 0.0f) {
            widthScale = ((currPointWidth * nextDist + nextPointWidth * currDist) / sumDist) / (mWidthArg / cNum_initPoints_f4);
            const f32 widthDiff = currPointWidth - nextPointWidth;

            // Where the width changes, the normal is square to the wall line from 300 behind to 300 ahead (and to gravity).
            if (MR::abs(widthDiff) > cNum_initPoints_f5) {
                Vec3f wallBehind(side);
                Vec3f wallAhead(side);
                wallBehind.scale(currPointWidth);
                wallAhead.scale(nextPointWidth);

                Vec3f railPos;
                MR::calcRailPosAtCoord(&railPos, this, MR::repeat(MR::getRailCoord(this) - cNum_initPoints_f6, 0.0f, MR::getRailTotalLength(this)));
                wallBehind.add(railPos);

                MR::calcRailPosAtCoord(&railPos, this, MR::repeat(MR::getRailCoord(this) + cNum_initPoints_f7, 0.0f, MR::getRailTotalLength(this)));
                wallAhead.add(railPos);

                wallBehind.sub(wallAhead);
                normal.cross(-grav, wallBehind);
                MR::normalize(&normal);
            }
        }

        for (s32 j = 0; j < mNumSides; j++) {
            Vec3f pos(side);
            pos.scale(mWidth * widthScale);
            pos.add(MR::getRailPos(this));

            mPositions[idx].set< f32 >(pos.x, pos.y, pos.z);
            mNormals[idx].set< f32 >(normal.x * cNum_initPoints_f8, normal.y * cNum_initPoints_f9, normal.z * cNum_initPoints_f10);
            idx++;

            mtx.mult(side, side);
            mtx.mult(normal, normal);
        }

        MR::moveCoord(this, length);
    }

}
