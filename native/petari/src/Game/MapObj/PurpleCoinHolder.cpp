#include "Game/MapObj/PurpleCoinHolder.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/Coin.hpp"
#include "Game/MapObj/PurpleCoinStarter.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util.hpp"

namespace {
    static const s32 sMaxPurpleCoin = 256;
    // static const s32 sGetIncrementCount = _;
};  // namespace

PurpleCoinHolder::PurpleCoinHolder() : DeriveActorGroup< Coin >("\203\160\201\133\203\166\203\213\203\122\203\103\203\223\212\307\227\235" /* パープルコイン管理 */, ::sMaxPurpleCoin), mStarter() {
    MR::declarePowerStarCoin100();
    MR::createPurpleCoinCounter();
}

void PurpleCoinHolder::initAfterPlacement() {
    if (mStarter == nullptr) {
        MR::validatePurpleCoinCounter();
    }
}

void PurpleCoinHolder::start() {
    MR::validatePurpleCoinCounter();
}

void PurpleCoinHolder::registStarter(PurpleCoinStarter* pStarter) {
    mStarter = pStarter;
    mStarter->setHost(this);
}

namespace {
    PurpleCoinHolder* getPurpleCoinHolder() {
        return MR::getSceneObj< PurpleCoinHolder >(SceneObj_PurpleCoinHolder);
    }
};  // namespace

namespace MR {
    void createPurpleCoinHolder() {
        MR::createSceneObj(SceneObj_PurpleCoinHolder);
    }

    void addToPurpleCoinHolder(const NameObj* pObj, Coin* pCoin) {
        ::getPurpleCoinHolder()->registerActor(pCoin);
    }

    void registPurpleCoinStarter(PurpleCoinStarter* pStarter) {
        ::getPurpleCoinHolder()->registStarter(pStarter);
    }
};  // namespace MR
