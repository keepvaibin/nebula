#include "Game/System/DrawBufferGroup.hpp"
#include "Game/System/DrawBufferExecuter.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Render/Model.hpp"
#include <algorithm>

DrawBufferGroup::DrawBufferGroup() : mExecutors(), mActiveExecutors(), mDrawCameraType(0), mLightType(-1), mLightLoadType(-1) {
}

s32 DrawBufferGroup::registerDrawBuffer(LiveActor* pActor) {
    const char* pModelName = MR::getModelResName(pActor);
    s32 idx = findExecuterIndex(pModelName);

    if (idx < 0) {
        DrawBufferExecuter* exec = new DrawBufferExecuter(pModelName, MR::getModel(pActor));

        idx = mExecutors.size();
        mExecutors.push_back(exec);

        if (mLightLoadType == -1) {
            exec->onExecuteLight(mLightType);
        }
    }

    mExecutors[idx]->mDrawBufferCount++;

    return idx;
}

void DrawBufferGroup::allocateActorListBuffer() {
    for (auto& item : mExecutors) {
        item->allocateActorListBuffer();
    }
}

void DrawBufferGroup::active(LiveActor* pActor, s32 index) {
    bool isEmpty = isExecutorEmpty(index);
    mExecutors[index]->add(pActor);

    if (isEmpty) {
        mActiveExecutors.push_back(mExecutors[index]);
    }
}

void DrawBufferGroup::deactive(LiveActor* pActor, s32 index) {
    mExecutors[index]->remove(pActor);
    if (isExecutorEmpty(index)) {
        mActiveExecutors[std::find(mActiveExecutors.begin(), mActiveExecutors.begin() + mActiveExecutors.size(), mExecutors[index]) -
                         mActiveExecutors.begin()] = mActiveExecutors[mActiveExecutors.size() - 1];
        mActiveExecutors.mCount--;
    }
}

void DrawBufferGroup::findLightInfo(LiveActor* pActor, s32 index) {
    MR::initActorLightInfoLightType(pActor, mLightType);
    mExecutors[index]->findLightInfo(pActor);

    if (mLightLoadType != -1) {
        for (u32 i = 0; i < mExecutors.size(); i++) {
            mExecutors[i]->onExecuteLight(mLightType);
        }

        mLightLoadType = -1;
    }

    mExecutors[index]->offExecuteLight();
}

void DrawBufferGroup::entry() {
    if (mActiveExecutors.size() != 0) {
        for (auto& item : mActiveExecutors) {
            item->calcViewAndEntry();
        }
    }
}

void DrawBufferGroup::drawOpa() const {
    if (mActiveExecutors.size() != 0) {
        if (mLightLoadType != -1) {
            MR::loadLight(mLightLoadType);
        }
        for (auto& item : mActiveExecutors) {
            item->drawOpa();
        }
    }
}

void DrawBufferGroup::drawXlu() const {
    if (mActiveExecutors.size() != 0) {
        if (mLightLoadType != -1) {
            MR::loadLight(mLightLoadType);
        }
        for (auto& item : mActiveExecutors) {
            item->drawXlu();
        }
    }
}

void DrawBufferGroup::setDrawCameraType(s32 type) {
    mDrawCameraType = type;
}

void DrawBufferGroup::setLightType(s32 type) {
    mLightType = type;
    mLightLoadType = type;
}

s32 DrawBufferGroup::findExecuterIndex(const char* pName) const {
    for (u32 i = 0; i < mExecutors.size(); i++) {
        if (MR::isEqualString(mExecutors[i]->mName, pName)) {
            return i;
        }
    }

    return -1;
}
