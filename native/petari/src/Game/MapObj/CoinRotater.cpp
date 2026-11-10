#include "Game/MapObj/CoinRotater.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_movement_f1, "movement.f1", 8.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f2, "movement.f2", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f3, "movement.f3", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f4, "movement.f4", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f5, "movement.f5", 16.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f6, "movement.f6", 360.0f);

CoinRotater::CoinRotater(const char* pName) : NameObj(pName), mRotateY(), mWaterRotateY(), mHiSpeedRotateY() {
    mRotateYMtx.identity();
    mHiSpeedRotateYMtx.identity();
    mWaterRotateMtx.identity();
    
    MR::connectToSceneMapObjMovement(this);
}

void CoinRotater::movement() {
    mRotateY += cNum_movement_f1;
    mRotateY = MR::repeat(mRotateY, 0.0f, cNum_movement_f2);

    mWaterRotateY += cNum_movement_f3;
    mWaterRotateY = MR::repeat(mWaterRotateY, 0.0f, cNum_movement_f4);

    mHiSpeedRotateY += cNum_movement_f5;
    mHiSpeedRotateY = MR::repeat(mHiSpeedRotateY, 0.0f, cNum_movement_f6);

    MR::makeMtxRotateY(mRotateYMtx, mRotateY);
    MR::makeMtxRotateY(mHiSpeedRotateYMtx, mHiSpeedRotateY);
    MR::makeMtxRotateY(mWaterRotateMtx, mWaterRotateY);
}

namespace MR {
    void createCoinRotater() {
        MR::createSceneObj(SceneObj_CoinRotater);
    }

    Mtx34f& getCoinRotateYMatrix() {
        return MR::getSceneObj< CoinRotater >(SceneObj_CoinRotater)->mRotateYMtx;
    }

    Mtx34f& getCoinHiSpeedRotateYMatrix() {
        return MR::getSceneObj< CoinRotater >(SceneObj_CoinRotater)->mHiSpeedRotateYMtx;
    }

    Mtx34f& getCoinInWaterRotateYMatrix() {
        return MR::getSceneObj< CoinRotater >(SceneObj_CoinRotater)->mWaterRotateMtx;
    }
};  // namespace MR
