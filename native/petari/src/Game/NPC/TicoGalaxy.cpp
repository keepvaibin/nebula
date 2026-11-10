#include "Game/NPC/TicoGalaxy.hpp"
#include "Game/MapObj/StarPieceDirector.hpp"
#include "Game/NPC/TicoStarRing.hpp"
#include "Game/System/GameEventFlagTable.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    static const char* sInfoMessageID = "InformationGalaxy";
};  // namespace

TicoGalaxy::TicoGalaxy(const char* pName) : TicoFat(pName), mGalaxyName() {
    lumacore::setClass(this, lumacore::kClassId< TicoGalaxy >);
}

void TicoGalaxy::init(const TableIter& rIter) {
    TicoFat::init(rIter);

    mEatInterval = 2;

    if (MR::isOnTicoGalaxyAlreadyTalk(mTicoSeedId)) {
        MR::forwardNode(mMsgCtrl);
        MR::forwardNode(mMsgCtrl);
    }

    mStarRing = new TicoStarRing("スターリングチコ");
    mStarRing->initWithoutIter();
}

void TicoGalaxy::initAfterPlacement() {
    mStarRing->initialize(this, MR::getPairedGroupMember(this), mGalaxyName);
    TicoFat::initAfterPlacement();
}

void TicoGalaxy::shootStarPiece() {
    s32 count = 5;

    if (mStarPieceLeft <= 5) {
        count = mStarPieceLeft;
    }

    if (MR::giftStarPieceToTarget(getSensor("Mouth"), count)) {
        mCurrentFed += count;
        MR::tryRumblePadVeryWeak(this, PLAYER_1);
    }
}

void TicoGalaxy::startEat() {
    TicoFat::startEat();
    MR::setTicoGalaxyAlreadyTalk(mTicoSeedId, true);
}

bool TicoGalaxy::enableAppear() {
    return MR::isAppearGalaxy(mGalaxyName);
}

void TicoGalaxy::initStarPieceSaveData(const TableIter& rIter) {
    MR::getPlacementArg7WithInit(rIter, &mTicoSeedId);
    mGalaxyName = GameEventFlagTable::getExclamationGalaxyNameFromIndex(mTicoSeedId);
    mStarPieceNeed = GameEventFlagTable::getStarPieceNumToOpenExclamationGalaxy(mGalaxyName);
    s32 starPieceNum = MR::getStarPieceNumGivingToTicoGalaxy(mTicoSeedId);
    mStarPieceLeft = (mStarPieceNeed - starPieceNum <= 0) ? 0 : mStarPieceNeed - starPieceNum;
}

void TicoGalaxy::addStarPieceSaveData(s32 num) {
    MR::addStarPieceGivingToTicoGalaxy(mTicoSeedId, num);
}

void TicoGalaxy::disappear(bool isJustFed) {
    if (isJustFed) {
        MR::onGameEventFlagGalaxyOpen(mGalaxyName);
        MR::callAppearAllGroupMember(this);
    } else {
        mStarRing->appear();
    }
}

void TicoGalaxy::appearInformation() const {
    MR::appearInformationMessage(MR::getGameMessageDirect(::sInfoMessageID), true);
    MR::setInformationMessageReplaceString(MR::getGalaxyNameShortOnCurrentLanguage(mGalaxyName), 0);
}
