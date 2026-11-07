#include "Game/Demo/DemoStartRequestHolder.hpp"
#include "Game/Demo/DemoStartRequestUtil.hpp"
#include "Game/NameObj/NameObj.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/StringUtil.hpp"

DemoStartInfo::DemoStartInfo() {
    _0 = nullptr;
    _4 = nullptr;
    _8 = nullptr;
    _C = nullptr;
    _10 = nullptr;
    mDemoExecutor = nullptr;
    mDemoName = nullptr;
    _1C = nullptr;
    _20 = nullptr;
    _24 = 0;
    mDemoType = DemoType_0;
    mFrameType = CinemaFrameType_0;
    mPointerType = StarPointerType_0;
    mDeleteEffectType = DeleteEffectType_0;
}

DemoStartInfo& DemoStartInfo::operator=(const DemoStartInfo& rOther) {
    _0 = rOther._0;
    _4 = rOther._4;
    _8 = rOther._8;
    _C = rOther._C;
    _10 = rOther._10;
    mDemoExecutor = rOther.mDemoExecutor;
    mDemoName = rOther.mDemoName;
    _1C = rOther._1C;
    _20 = rOther._20;
    _24 = rOther._24;
    mDemoType = rOther.mDemoType;
    mFrameType = rOther.mFrameType;
    mPointerType = rOther.mPointerType;
    mDeleteEffectType = rOther.mDeleteEffectType;
    return *this;
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
        if ((*pIter)->_0 == pActor && MR::isEqualString((*pIter)->mDemoName, pName)) {
            return *pIter;
        }
    }

    return nullptr;
}

DemoStartInfo* DemoStartRequestHolder::find(const LayoutActor* pActor, const char* pName) const {
    for (DemoStartInfo* const* pIter = mStartInfos.begin(); pIter != mStartInfos.end(); pIter++) {
        if ((*pIter)->_4 == pActor && MR::isEqualString((*pIter)->mDemoName, pName)) {
            return *pIter;
        }
    }

    return nullptr;
}

DemoStartInfo* DemoStartRequestHolder::find(const NerveExecutor* pExecutor, const char* pName) const {
    for (DemoStartInfo* const* pIter = mStartInfos.begin(); pIter != mStartInfos.end(); pIter++) {
        if ((*pIter)->_8 == pExecutor && MR::isEqualString((*pIter)->mDemoName, pName)) {
            return *pIter;
        }
    }

    return nullptr;
}

DemoStartInfo* DemoStartRequestHolder::find(const NameObj* pObj, const char* pName) const {
    for (DemoStartInfo* const* pIter = mStartInfos.begin(); pIter != mStartInfos.end(); pIter++) {
        if ((*pIter)->_C == pObj && MR::isEqualString((*pIter)->mDemoName, pName)) {
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
