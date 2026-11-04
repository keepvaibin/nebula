#include "Game/Demo/DemoPlayerKeeper.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/JMapInfo.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"

DemoPlayerKeeper::DemoPlayerKeeper(const DemoExecutor* pExecutor) : mExecutor(pExecutor), mNumPlayerInfos(), mPlayerInfos() {
    JMapInfo* map = nullptr;
    mNumPlayerInfos = DemoFunction::createSheetParser(mExecutor, "Player", &map);
    mPlayerInfos = new DemoPlayerInfo[mNumPlayerInfos];
    for (int i = 0; i < mNumPlayerInfos; i++) {
        DemoPlayerInfo* playerinfo = &mPlayerInfos[i];
        MR::getCsvDataStrOrNULL(&playerinfo->mPartName, map, "PartName", i);
        MR::getCsvDataStrOrNULL(&playerinfo->mPosName, map, "PosName", i);
        MR::getCsvDataStrOrNULL(&playerinfo->mBckName, map, "BckName", i);
    }
}

DemoPlayerInfo::DemoPlayerInfo() : mPosName(nullptr), mPartName(nullptr), mBckName(nullptr) {
}

void DemoPlayerKeeper::update() {
    const DemoPlayerInfo* playerinfo;
    for (int i = 0; i < mNumPlayerInfos; i++) {
        playerinfo = &mPlayerInfos[i];
        if (MR::isDemoPartFirstStep(playerinfo->mPartName)) {
            executePlayer(playerinfo);
        }
    }
}

bool DemoPlayerKeeper::isExistPosName() const {
    for (int i = 0; i < mNumPlayerInfos; i++) {
        // A mod's cutscene may name a position the stage doesn't have.
        TPos3f found;
        if (mPlayerInfos[i].mPosName != nullptr && MR::tryFindNamePos(mPlayerInfos[i].mPosName, found.toMtxPtr())) {
            return true;
        }
    }
    return false;
}

void DemoPlayerKeeper::executePlayer(const DemoPlayerInfo* pPlayerInfo) const {
    if (pPlayerInfo->mPosName != nullptr) {
        MR::setPlayerPosOnGroundAndWait(pPlayerInfo->mPosName);
    }
    // A mod's cutscene may name an animation the player doesn't have.
    if (pPlayerInfo->mBckName != nullptr && MR::isExistBck(MarioAccess::getPlayerActor(), pPlayerInfo->mBckName)) {
        MR::startBckPlayer(pPlayerInfo->mBckName);
    }
}
