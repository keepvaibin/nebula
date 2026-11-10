#include "Game/Screen/WipeHolderBase.hpp"
#include "Game/Screen/WipeLayoutBase.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstdio>
#include "Game/System/LumaCore.hpp"

WipeHolderBase::WipeHolderBase(u32 num, const char* pName)
    : NameObj(pName), mCurrentWipeLayout(), mWipeLayoutArray(new WipeLayoutBase*[num]), mWipeLayoutCount() {
    lumacore::setClass(this, lumacore::kClassId< WipeHolderBase >);
}

void WipeHolderBase::setCurrent(const char* pWipeName) {
    // A name there's no wipe of, from a mod's cutscene: the current one stays.
    WipeLayoutBase* pWipe = findWipe(pWipeName);
    if (pWipe == nullptr) {
        return;
    }

    if (mCurrentWipeLayout != nullptr) {
        mCurrentWipeLayout->forceOpen();
        mCurrentWipeLayout->kill();
    }

    mCurrentWipeLayout = pWipe;
    mCurrentWipeLayout->appear();
    mCurrentWipeLayout->forceOpen();
}

void WipeHolderBase::wipe(const char* pWipeName, s32 frame) {
    updateWipe(pWipeName);
    mCurrentWipeLayout->wipe(frame);
}

void WipeHolderBase::forceClose(const char* pWipeName) {
    updateWipe(pWipeName);
    mCurrentWipeLayout->forceClose();
}

void WipeHolderBase::forceOpen(const char* pWipeName) {
    updateWipe(pWipeName);
    mCurrentWipeLayout->forceOpen();
}

bool WipeHolderBase::isOpen() const {
    return mCurrentWipeLayout->isOpen();
}

bool WipeHolderBase::isClose() const {
    return mCurrentWipeLayout->isClose();
}

bool WipeHolderBase::isWipeIn() const {
    return mCurrentWipeLayout->isWipeIn();
}

bool WipeHolderBase::isWipeOut() const {
    return mCurrentWipeLayout->isWipeOut();
}

bool WipeHolderBase::isCurrent(const char* pWipeName) const {
    return MR::isEqualString(mCurrentWipeLayout->getWipeName(), pWipeName);
}

void WipeHolderBase::addWipeLayout(WipeLayoutBase* pWipeLayout) {
    pWipeLayout->initWithoutIter();

    if (mCurrentWipeLayout == nullptr) {
        mCurrentWipeLayout = pWipeLayout;
    }

    mWipeLayoutArray[mWipeLayoutCount] = pWipeLayout;
    mWipeLayoutCount++;
}

void WipeHolderBase::updateWipe(const char* pWipeName) {
    if (pWipeName == nullptr) {
        return;
    }

    if (isCurrent(pWipeName)) {
        return;
    }

    setCurrent(pWipeName);
}

WipeLayoutBase* WipeHolderBase::findWipe(const char* pWipeName) const {
    if (isCurrent(pWipeName)) {
        return mCurrentWipeLayout;
    }

    for (u32 i = 0; i < mWipeLayoutCount; i++) {
        if (MR::isEqualString(pWipeName, mWipeLayoutArray[i]->getWipeName())) {
            return mWipeLayoutArray[i];
        }
    }

    return nullptr;
}
