#include "Game/MapObj/CircleCoinGroup.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

CircleCoinGroup::CircleCoinGroup(const char* pName) : CoinGroup(pName), mCoinRadius(200.0f) {
    lumacore::setClass(this, lumacore::kClassId< CircleCoinGroup >);
}

void CircleCoinGroup::initCoinArray(const TableIter& rIter) {
    MR::getPlacementArg2NoInit(rIter, &mCoinRadius);
    MR::initDefaultPos(this, rIter);
}

void CircleCoinGroup::placementCoin() {
    Pos3f mtx;
    MR::makeMtxTR(mtx, this);

    Vec3f front, side;
    mtx.getXDir(side);
    mtx.getZDir(front);

    const Vec3f center(*getPosition());
    f32 angle = 0.0f;
    f32 interval = (TWO_PI / mCoinCount);

    for (s32 i = 0; i < mCoinCount; i++) {
        f32 c = mCoinRadius * MR::cos(angle);
        f32 s = mCoinRadius * MR::sin(angle);

        setCoinTrans(i, front * c + side * s + center);
        angle += interval;
    }
}

namespace MR {
    NameObj* createCircleCoinGroup(const char* pName) {
        return new CircleCoinGroup(pName);
    }

    NameObj* createCirclePurpleCoinGroup(const char* pName) {
        CircleCoinGroup* pGroup = new CircleCoinGroup(pName);
        pGroup->mIsPurpleCoinGroup = true;
        return pGroup;
    }
};  // namespace MR

const char* CircleCoinGroup::getCoinName() const {
    return mIsPurpleCoinGroup ? "パープルコイン(円形配置)" : "コイン(円形配置)";
}
