#include "Game/Demo/DemoSubPartKeeper.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/Util.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstring>

DemoSubPartKeeper::DemoSubPartKeeper(const DemoExecutor* pExecutor) : mExecutor(pExecutor), mNumSubPartInfos(), mSubPartInfos() {
    Table* map = nullptr;
    mNumSubPartInfos = DemoFunction::createSheetParser(mExecutor, "SubPart", &map);
    mSubPartInfos = new DemoSubPartInfo[mNumSubPartInfos];
    for (int i = 0; i < mNumSubPartInfos; i++) {
        DemoSubPartInfo* subpart = &mSubPartInfos[i];
        MR::getCsvDataStrOrNULL(&subpart->mSubPartName, map, "SubPartName", i);
        MR::getCsvDataS32(&subpart->mSubPartTotalStep, map, "SubPartTotalStep", i);
        MR::getCsvDataStrOrNULL(&subpart->mMainPartName, map, "MainPartName", i);
        MR::getCsvDataS32(&subpart->mMainPartStep, map, "MainPartStep", i);
    }
}

DemoSubPartInfo::DemoSubPartInfo() : mSubPartName(nullptr), mSubPartTotalStep(1), mMainPartName(nullptr), mMainPartStep(1), mRestStep(-1) {
}

void DemoSubPartKeeper::update() {
    for (int i = 0; i < mNumSubPartInfos; i++) {
        DemoSubPartInfo* subpart = &mSubPartInfos[i];
        if (subpart->mRestStep > 0) {
            subpart->mRestStep -= 1;
        }
        if (MR::isDemoPartStep(subpart->mMainPartName, subpart->mMainPartStep)) {
            subpart->mRestStep = subpart->mSubPartTotalStep;
        }
    }
}

void DemoSubPartKeeper::end() {
    for (int i = 0; i < mNumSubPartInfos; i++) {
        mSubPartInfos[i].mRestStep = -1;
    }
}

bool DemoSubPartKeeper::isDemoPartActive(const char* pSubPartName) const {
    DemoSubPartInfo* subpart = findSubPart(pSubPartName);
    if (subpart == nullptr) {
        return false;
    } else {
        return subpart->mRestStep > 0;
    }
}

s32 DemoSubPartKeeper::getDemoPartStep(const char* pSubPartName) const {
    DemoSubPartInfo* subpart = findSubPart(pSubPartName);
    return subpart->mSubPartTotalStep - subpart->mRestStep;
}

s32 DemoSubPartKeeper::getDemoPartTotalStep(const char* pSubPartName) const {
    DemoSubPartInfo* subpart = findSubPart(pSubPartName);
    return subpart->mSubPartTotalStep;
}

DemoSubPartInfo* DemoSubPartKeeper::findSubPart(const char* pSubPartName) const {
    DemoSubPartInfo* subpart;
    s32 count = mNumSubPartInfos;
    for (int i = 0; i < count; i++) {
        subpart = &mSubPartInfos[i];
        if (MR::isEqualString(subpart->mSubPartName, pSubPartName)) {
            return subpart;
        }
    }
    return nullptr;
}
