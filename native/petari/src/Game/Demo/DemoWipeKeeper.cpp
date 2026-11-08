#include "Game/Demo/DemoWipeKeeper.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/Screen/SceneWipeHolder.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/ObjUtil.hpp"

DemoWipeInfo::DemoWipeInfo() : mPartName(), mWipeName("フェードワイプ"), mWipeType(), mWipeFrame(-1) {
}

DemoWipeKeeper::DemoWipeKeeper(DemoExecutor* pExecutor) : DemoSheetKeeperBase(pExecutor) {
    Table* pMap = nullptr;
    const s32 count = DemoFunction::createSheetParser(mExecutor, getTypeString(), &pMap);
    mInfo.init(count);

    for (s32 i = 0; i < count; i++) {
        DemoWipeInfo info;
        MR::getCsvDataStrOrNULL(&info.mPartName, pMap, "PartName", i);
        MR::getCsvDataStrOrNULL(&info.mWipeName, pMap, "WipeName", i);
        MR::getCsvDataS32(&info.mWipeType, pMap, "WipeType", i);
        MR::getCsvDataS32(&info.mWipeFrame, pMap, "WipeFrame", i);
        mInfo.push_back(info);
    }
}

void DemoWipeKeeper::start() {
}

void DemoWipeKeeper::update() {
    DemoSheetKeeperInfoHolder< DemoWipeInfo >::update();
}

void DemoWipeKeeper::executeType(const DemoWipeInfo* pInfo) {
    if (!MR::isDemoPartFirstStep(pInfo->mPartName)) {
        return;
    }

    // The sheet's WipeType column.
    switch (pInfo->mWipeType) {
    case 0:
        SceneWipeHolderFunction::openWipe(pInfo->mWipeName, pInfo->mWipeFrame);
        break;
    case 1:
        SceneWipeHolderFunction::closeWipe(pInfo->mWipeName, pInfo->mWipeFrame);
        break;
    case 2:
        SceneWipeHolderFunction::forceOpenWipe(pInfo->mWipeName);
        break;
    case 3:
        SceneWipeHolderFunction::forceCloseWipe(pInfo->mWipeName);
        break;
    default:
        break;
    }
}

inline const char* DemoWipeKeeper::getTypeString() const {
    return "Wipe";
}

inline const char* DemoWipeKeeper::getName() const {
    return "ワイプ";
}

#include "Game/Demo/DemoSheetKeeperInfoHolder.hpp"
