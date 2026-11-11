#include "Game/MapObj/CoinReplica.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/StageSwitch.hpp"
#include "Game/MapObj/Coin.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

CoinReplica::CoinReplica(const char* pName) : NameObj(pName), mCoin() {
    lumacore::setClass(this, lumacore::kClassId< CoinReplica >);
}

void CoinReplica::activeCoin() {
    if (mCoin != nullptr) {
        mCoin->requestActive();
    }
}

void CoinReplica::activeCoinWithGravity() {
    if (mCoin != nullptr) {
        mCoin->requestActiveWithGravity();
    }
}

void CoinReplica::deactiveCoin() {
    if (mCoin != nullptr) {
        mCoin->requestDeactive();
    }
}

void CoinReplica::removeCoin() {
    if (!MR::isDead(mCoin)) {
        mCoin->kill();
    }
}

void CoinReplica::init(const TableIter& rIter) {
    mCoin = static_cast< Coin* >(MR::createCoin(this, "コイン(レプリカ用)"));
    MR::initDefaultPos(mCoin, rIter);
    mCoin->initWithoutIter();
    mCoin->appearNonActive();

    // -1: the coin keeps no gravity once active.
    s32 gravityArg;
    MR::getPlacementArg0WithInit(rIter, &gravityArg);

    if (!MR::isExistStageSwitchA(rIter) && !MR::isExistStageSwitchB(rIter)) {
        return;
    }

    StageSwitchCtrl* pSwitchCtrl = MR::createStageSwitchCtrl(this, rIter);

    if (pSwitchCtrl->isValidSwitchA()) {
        MR::listenNameObjStageSwitchOnOffA(this, pSwitchCtrl,
                                           MR::Functor(this, gravityArg == -1 ? &CoinReplica::activeCoin : &CoinReplica::activeCoinWithGravity),
                                           MR::Functor(this, &CoinReplica::deactiveCoin));
    }

    if (pSwitchCtrl->isValidSwitchB()) {
        MR::listenNameObjStageSwitchOnB(this, pSwitchCtrl, MR::Functor(this, &CoinReplica::removeCoin));
    }
}
