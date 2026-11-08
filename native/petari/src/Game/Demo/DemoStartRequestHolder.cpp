#include "Game/Demo/DemoStartRequestHolder.hpp"
#include "Game/Demo/DemoStartRequestUtil.hpp"
#include "Game/NameObj/NameObj.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/StringUtil.hpp"

DemoStartInfo::DemoStartInfo() {
    mActor = nullptr;
    mLayout = nullptr;
    mNerveExecutor = nullptr;
    mNameObj = nullptr;
    mNerveExecutorHost = nullptr;
    mDemoExecutor = nullptr;
    mDemoName = nullptr;
    mPartName = nullptr;
    mDemoNerve = nullptr;
    mMovementType = MR::MovementControlType_Resume;
    mDemoType = DemoType_Programmable;
    mFrameType = CinemaFrameType_Frame;
    mPointerType = StarPointerType_Demo;
    mDeleteEffectType = DeleteEffectType_Keep;
}

void DemoStartRequestHolder::pushRequest(LiveActor* pActor, const char* pName) {
    const DemoStartInfo* pInfo = find(pActor, pName);
    if (pInfo == nullptr) {
        pInfo = findEmpty();
    }

    mRequests.push_back(pInfo);
}

void DemoStartRequestHolder::pushRequest(LayoutActor* pActor, const char* pName) {
    const DemoStartInfo* pInfo = find(pActor, pName);
    if (pInfo == nullptr) {
        pInfo = findEmpty();
    }

    mRequests.push_back(pInfo);
}

void DemoStartRequestHolder::pushRequest(NerveExecutor* pExecutor, const char* pName) {
    const DemoStartInfo* pInfo = find(pExecutor, pName);
    if (pInfo == nullptr) {
        pInfo = findEmpty();
    }

    mRequests.push_back(pInfo);
}

void DemoStartRequestHolder::pushRequest(NameObj* pObj, const char* pName) {
    const DemoStartInfo* pInfo = find(pObj, pName);
    if (pInfo == nullptr) {
        pInfo = findEmpty();
    }

    mRequests.push_back(pInfo);
}

void DemoStartRequestHolder::popRequest() {
    if (mRequestHead >= mRequests.size()) {
        return;
    }

    const DemoStartInfo* pCurrent = mRequests[mRequestHead];
    for (DemoStartInfo** pIter = mStartInfos.begin(); pIter != mStartInfos.end(); pIter++) {
        if (*pIter == pCurrent) {
            **pIter = DemoStartInfo();
            break;
        }
    }

    mRequestHead++;
    if (mRequestHead == mRequests.size()) {
        mRequests.clear();
        mRequestHead = 0;
    }
}

bool DemoStartRequestHolder::isExistRequest() const {
    return mRequestHead < mRequests.size();
}

const DemoStartInfo* DemoStartRequestHolder::getCurrentInfo() const {
    if (mRequestHead >= mRequests.size()) {
        return nullptr;
    }

    return mRequests[mRequestHead];
}

void DemoStartRequestHolder::registerStartDemoInfo(const DemoStartInfo& rInfo) {
    DemoStartInfo* pEmpty = findEmpty();
    *pEmpty = rInfo;
}

DemoStartInfo* DemoStartRequestHolder::find(const LiveActor* pActor, const char* pName) const {
    for (DemoStartInfo* const* pIter = mStartInfos.begin(); pIter != mStartInfos.end(); pIter++) {
        if ((*pIter)->mActor == pActor && MR::isEqualString((*pIter)->mDemoName, pName)) {
            return *pIter;
        }
    }

    return nullptr;
}

DemoStartInfo* DemoStartRequestHolder::find(const LayoutActor* pActor, const char* pName) const {
    for (DemoStartInfo* const* pIter = mStartInfos.begin(); pIter != mStartInfos.end(); pIter++) {
        if ((*pIter)->mLayout == pActor && MR::isEqualString((*pIter)->mDemoName, pName)) {
            return *pIter;
        }
    }

    return nullptr;
}

DemoStartInfo* DemoStartRequestHolder::find(const NerveExecutor* pExecutor, const char* pName) const {
    for (DemoStartInfo* const* pIter = mStartInfos.begin(); pIter != mStartInfos.end(); pIter++) {
        if ((*pIter)->mNerveExecutor == pExecutor && MR::isEqualString((*pIter)->mDemoName, pName)) {
            return *pIter;
        }
    }

    return nullptr;
}

DemoStartInfo* DemoStartRequestHolder::find(const NameObj* pObj, const char* pName) const {
    for (DemoStartInfo* const* pIter = mStartInfos.begin(); pIter != mStartInfos.end(); pIter++) {
        if ((*pIter)->mNameObj == pObj && MR::isEqualString((*pIter)->mDemoName, pName)) {
            return *pIter;
        }
    }

    return nullptr;
}

DemoStartInfo* DemoStartRequestHolder::findEmpty() {
    for (DemoStartInfo** pIter = mStartInfos.begin(); pIter != mStartInfos.end(); pIter++) {
        if (DemoStartRequestUtil::isEmpty(*pIter)) {
            **pIter = DemoStartInfo();
            return *pIter;
        }
    }

    DemoStartInfo* pInfo = new (NebulaArena::findFromRoot(this), 0) DemoStartInfo();
    mStartInfos.push_back(pInfo);
    return pInfo;
}

DemoStartRequestHolder::DemoStartRequestHolder() : mRequestHead(0) {
    mProxyObj = new NameObj("代理人");
    mStartInfos.init(16);
    mRequests.init(16);
    for (s32 i = 0; i < 16; i++) {
        mStartInfos.push_back(new DemoStartInfo());
    }
}
