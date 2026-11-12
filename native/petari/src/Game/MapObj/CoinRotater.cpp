#include "Game/MapObj/CoinRotater.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

CoinRotater::CoinRotater(const char* pName) : NameObj(pName), mRotateY(), mWaterRotateY(), mHiSpeedRotateY() {
    lumacore::setClass(this, lumacore::kClassId< CoinRotater >);
    mRotateYMtx.identity();
    mHiSpeedRotateYMtx.identity();
    mWaterRotateMtx.identity();
    
    MR::connectToSceneMapObjMovement(this);
}

void CoinRotater::movement() {
    mRotateY += 8.0f;
    mRotateY = MR::repeat(mRotateY, 0.0f, 360.0f);

    mWaterRotateY += 4.0f;
    mWaterRotateY = MR::repeat(mWaterRotateY, 0.0f, 360.0f);

    mHiSpeedRotateY += 16.0f;
    mHiSpeedRotateY = MR::repeat(mHiSpeedRotateY, 0.0f, 360.0f);

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
