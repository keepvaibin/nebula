#include "Game/System/DrawBufferHolder.hpp"
#include "Game/System/DrawBufferGroup.hpp"
#include <algorithm>

bool DrawBufferHolder::isBufferGroupEmpty(s32 drawBufferType) const {
    return mBufferGroups[drawBufferType].mActiveExecutors.size() == 0;
}

// Each camera type's entry list is sized for every group of that type.
void DrawBufferHolder::initTable(const DrawBufferInitialTable* pInitialTable, s32 numGroups) {
    mBufferGroups.init(numGroups);

    s32 cameraTypeCounts[cDrawCameraTypeNum] = {};

    for (const DrawBufferInitialTable* pEntry = pInitialTable; pEntry != pInitialTable + numGroups; pEntry++) {
        const u32 drawType = pEntry->mDrawBufferType;
        cameraTypeCounts[pEntry->mDrawCameraType]++;

        mBufferGroups[drawType].setDrawCameraType(pEntry->mDrawCameraType);
        mBufferGroups[drawType].setLightType(pEntry->mLightType);
    }

    for (s32 i = 0; i < cDrawCameraTypeNum; i++) {
        if (cameraTypeCounts[i] > 0) {
            mExecuteLists[i].init(cameraTypeCounts[i]);
        }
    }
}

void DrawBufferHolder::allocateActorListBuffer() {
    for (auto& rGroup : mBufferGroups) {
        rGroup.allocateActorListBuffer();
    }
}

s32 DrawBufferHolder::registerDrawBuffer(LiveActor* pActor, s32 drawBufferType) {
    return mBufferGroups[drawBufferType].registerDrawBuffer(pActor);
}

void DrawBufferHolder::active(LiveActor* pActor, s32 drawBufferType, s32 executorIndex) {
    const bool isEmpty = isBufferGroupEmpty(drawBufferType);
    mBufferGroups[drawBufferType].active(pActor, executorIndex);

    if (isEmpty) {
        ExecutorList& rExecuteList = getExecuteList(drawBufferType);
        rExecuteList.mArray[rExecuteList.mCount++] = &mBufferGroups[drawBufferType];
    }
}

// A group that empties leaves its entry list; the last listed group takes its place.
void DrawBufferHolder::deactive(LiveActor* pActor, s32 drawBufferType, s32 executorIndex) {
    mBufferGroups[drawBufferType].deactive(pActor, executorIndex);

    if (isBufferGroupEmpty(drawBufferType)) {
        ExecutorList& rExecuteList = getExecuteList(drawBufferType);
        *std::find(rExecuteList.begin(), rExecuteList.end(), &mBufferGroups[drawBufferType]) = rExecuteList[rExecuteList.mCount - 1];
        rExecuteList.mCount--;
    }
}

void DrawBufferHolder::findLightInfo(LiveActor* pActor, s32 drawBufferType, s32 executorIndex) {
    mBufferGroups[drawBufferType].findLightInfo(pActor, executorIndex);
}

void DrawBufferHolder::entry(s32 drawCameraType) {
    for (auto& rGroup : mExecuteLists[drawCameraType]) {
        rGroup->entry();
    }
}

void DrawBufferHolder::drawOpa(s32 drawBufferType) const {
    if (isBufferGroupEmpty(drawBufferType)) {
        return;
    }

    mBufferGroups[drawBufferType].drawOpa();
}

void DrawBufferHolder::drawXlu(s32 drawBufferType) const {
    if (isBufferGroupEmpty(drawBufferType)) {
        return;
    }

    mBufferGroups[drawBufferType].drawXlu();
}

ExecutorList& DrawBufferHolder::getExecuteList(s32 drawBufferType) {
    return mExecuteLists[mBufferGroups[drawBufferType].mDrawCameraType];
}
