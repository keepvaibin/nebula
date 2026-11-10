#include "Game/MapObj/RailCoin.hpp"
#include "Game/AreaObj/MercatorTransformCube.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_placementMercatorRail_f1, "placementMercatorRail.f1", 10.0f);

namespace {
    class RailCoinMercatorDivInfo : public DivideMercatorRailPosInfo {
    public:
        RailCoinMercatorDivInfo(CoinGroup* pGroup) : mGroup(pGroup) {}

        virtual void setPosition(s32 idx, const Vec3f& rPos) {
            mGroup->setCoinTrans(idx, rPos);
        }

        CoinGroup* mGroup;
    };
};  // namespace

RailCoin::RailCoin(const char* pName) : CoinGroup(pName) {
}

void RailCoin::initCoinArray(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initRailRider(rIter);
}

void RailCoin::placementNormalRail() {
    f32 speed;
    f32 length = MR::getRailTotalLength(this);
    u32 coinCount = mCoinCount;

    if (mCoinCount <= 1) {
        speed = 0.0f;
    } else {
        if (MR::isLoopRail(this)) {
            speed = length / coinCount;
        } else {
            speed = length / (coinCount - 1);
        }
    }

    MR::moveCoordToStartPos(this);
    MR::setRailCoordSpeed(this, speed);

    for (u32 i = 0; i < coinCount; i++) {
        setCoinTrans(i, MR::getRailPos(this));
        MR::moveRailRider(this);
    }
}

void RailCoin::placementMercatorRail() {
    ::RailCoinMercatorDivInfo info(this);
    MR::getDivideMercatorRailPosition(&info, this, mCoinCount, cNum_placementMercatorRail_f1, 10);
}

namespace MR {
    NameObj* createRailCoin(const char* pName) {
        return new RailCoin(pName);
    }

    NameObj* createRailPurpleCoin(const char* pName) {
        RailCoin* pCoin = new RailCoin(pName);
        pCoin->mIsPurpleCoinGroup = true;
        return pCoin;
    }
};  // namespace MR

void RailCoin::placementCoin() {
    if (MR::isInAreaObj("MercatorCube", mPosition)) {
        placementMercatorRail();
    } else {
        placementNormalRail();
    }
}

const char* RailCoin::getCoinName() const {
    return mIsPurpleCoinGroup ? "パープルコイン(レール配置)" : "コイン(レール配置)";
}
