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

OceanRingPipe::~OceanRingPipe() {
}

OceanRingPipe::OceanRingPipe(const OceanRing* pRing, f32 a, f32 b)
    : LiveActor("オーシャンリングの側面"), mOceanRing(pRing), mIsCameraInside(), mNumPoints(), mNumSegments(), mNumSides(8), mPositions(), mNormals(), mWidth(a), mWidthArg(b), mPipeInside(),
      mPipeOutside() {
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
        f32 f30 = 300.0f;
        f32 f29 = 200.0f;
        f32 f28 = 500.0f;
        Vec3f vec(1.0f, 0.0f, 0.0f);
        for (s32 i = 0; i < mNumSegments; i += 7) {
            Vec3f grav(0.0f, -1.0f, 0.0f);
            MR::calcGravityVector(this, mPositions[calcPointIndex(i, 0)], &grav, nullptr, 0);
            if (grav.y > -0.9f) {
                continue;
            }

            if (i & 1) {
                MR::setClippingFarMax(MR::createMapFlag("旗", "FlagSurfing", &mPositions[calcPointIndex(i, 0)], vec, f28, f30, f29, 2, 3, -1.0f));
            } else {
                MR::setClippingFarMax(MR::createMapFlag("旗", "FlagSurfing", &mPositions[calcPointIndex(i, mNumSides - 1)], vec, f28, f30, f29, 2, 3, -1.0f));
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
    mNumSegments = static_cast< s32 >(MR::getRailTotalLength(this) / 300.0f) + 1;
    mNumPoints = mNumSegments * mNumSides;

    f32 length = MR::getRailTotalLength(this) / (mNumSegments - 1);
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

        f32 flt = mWidthArg / 100.0f;
        f32 flt2 = mWidthArg / 100.0f;
        MR::getCurrentRailPointArg1NoInit(this, &flt);
        MR::getNextRailPointArg1NoInit(this, &flt2);

        f32 f3 = 1.0f;
        Vec3f v1(side);

        if (sumDist > 0.0f) {
            f3 = ((flt * nextDist + flt2 * currDist) / sumDist) / (mWidthArg / 100.0f);
            f32 fff = flt - flt2;

            if (MR::abs(fff) > 0.0001f) {
                Vec3f v2(side);
                Vec3f v3(side);
                v2.scale(flt);
                v3.scale(flt2);

                Vec3f railPos;
                MR::calcRailPosAtCoord(&railPos, this, MR::repeat(MR::getRailCoord(this) - 300.0f, 0.0f, MR::getRailTotalLength(this)));
                v2.add(railPos);

                MR::calcRailPosAtCoord(&railPos, this, MR::repeat(MR::getRailCoord(this) + 300.0f, 0.0f, MR::getRailTotalLength(this)));
                v3.add(railPos);

                v2.sub(v3);
                v1.cross(-grav, v2);
                MR::normalize(&v1);
            }
        }

        for (s32 j = 0; j < mNumSides; j++) {
            Vec3f v4(side);
            v4.scale(mWidth * f3);
            v4.add(MR::getRailPos(this));

            mPositions[idx].set< f32 >(v4.x, v4.y, v4.z);
            mNormals[idx].set< f32 >(v1.x * 2.0f, v1.y * 2.0f, v1.z * 2.0f);
            idx++;

            mtx.mult(side, side);
            mtx.mult(v1, v1);
        }

        MR::moveCoord(this, length);
    }

}
