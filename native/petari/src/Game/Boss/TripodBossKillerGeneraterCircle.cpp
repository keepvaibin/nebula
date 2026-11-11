#include "Game/Boss/TripodBossKillerGeneraterCircle.hpp"
#include "Game/Boss/TripodBossKillerGenerater.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

struct SetUpData {
    const char* mName;
    const f32* mAngleTable;
    s32 mAngleNum;
    f32 mRadius;
    f32 mCirclePitch;
    f32 mCannonPitch;
    f32 mHeight;
    u32 mActiveLabel;
    bool mHasCollision;
};

namespace {
    NEBULA_TUNABLE_LIST(f32, sUpperHorizonAngleTable, , {42.0f, 96.0f, 140.0f, 180.0f, -140.0f, -96.0f, -42.0f});
    NEBULA_TUNABLE_LIST(f32, sUnderHorizonAngleTable, , {78.0f, 120.0f, 160.0f, -160.0f, -120.0f, -78.0f});
    NEBULA_TUNABLE_LIST(f32, sBottomHorizonAngleTable, , {0.0f, 120.0f, 240.0f});
    static const SetUpData sSetUpDataTable[] = {
        {"TripodBossUpperKillerCannon", sUpperHorizonAngleTable, ARRAY_SIZE(sUpperHorizonAngleTable), 2150.0f, 49.0f, -29.0f, 975.0f, 1, 0x1000000},
        {"TripodBossUnderKillerCannon", sUnderHorizonAngleTable, ARRAY_SIZE(sUnderHorizonAngleTable), 2150.0f, 5.8f, 39.2f, 975.0f, 0, 0x1000000},
        {"TripodBossBottomKillerCannon", sBottomHorizonAngleTable, ARRAY_SIZE(sBottomHorizonAngleTable), 500.0f, -50.0f, -30.0f, -1300.0f, 0, 0}};

    const SetUpData* getSetUpData(const char* pName) {
        for (u32 i = 0; i < ARRAY_SIZE(sSetUpDataTable); i++) {
            if (MR::isEqualString(pName, sSetUpDataTable[i].mName)) {
                return &sSetUpDataTable[i];
            }
        }

        return nullptr;
    }
};  // namespace

TripodBossKillerGeneraterCircle::TripodBossKillerGeneraterCircle(const char* pName)
    : NameObj(pName), mGenerater(), mPosition(0.0f, 0.0f, 0.0f), mAngleTable(), mAngleNum(), mHeight(), mRadius(), mCirclePitch(), mCannonPitch() {
    lumacore::setClass(this, lumacore::kClassId< TripodBossKillerGeneraterCircle >);
}

void TripodBossKillerGeneraterCircle::init(const TableIter& rIter) {
    const char* objName;
    MR::getObjectName(&objName, rIter);

    const SetUpData* dataPtr = ::getSetUpData(objName);

    MR::getPlacementTrans(rIter, &mPosition);
    MR::getPlacementRotate(rIter, &mRotation);

    mAngleTable = dataPtr->mAngleTable;
    mAngleNum = dataPtr->mAngleNum;
    mRadius = dataPtr->mRadius;
    mCirclePitch = dataPtr->mCirclePitch;
    mCannonPitch = dataPtr->mCannonPitch;
    mHeight = dataPtr->mHeight;

    mGenerater = new TripodBossKillerGenerater[mAngleNum];

    for (s32 i = 0; i < mAngleNum; i++) {
        mGenerater[i].setActiveLebel(dataPtr->mActiveLabel);
        mGenerater[i].setHasCollision(dataPtr->mHasCollision);
        mGenerater[i].init(rIter);
    }

    placementGenerater();
}

void TripodBossKillerGeneraterCircle::placementGenerater() {
    for (s32 i = 0; i < mAngleNum; i++) {
        Pos3f mtx;
        mtx.identity();
        mtx.setEulerX(-MR::toRadian(mCannonPitch));
        mtx.setTrans(0.0f, 0.0f, mRadius);

        Pos3f cannonMtx;
        cannonMtx.identity();
        cannonMtx.setRotate(-MR::toRadian(mCirclePitch), MR::toRadian(mAngleTable[i]), 0.0f);
        cannonMtx.concat(cannonMtx, mtx);
        MR::addTransMtx(cannonMtx, Vec3f(0.0f, mHeight, 0.0f));

        Pos3f trMtx;
        MR::makeMtxTR(trMtx, mPosition, mRotation);
        cannonMtx.concat(trMtx, cannonMtx);

        mGenerater[i].setLocalMatrix(cannonMtx);
    }
}
