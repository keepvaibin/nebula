#include "Game/System/ConfigDataMii.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/System/SaveBytes.hpp"

ConfigDataMii::ConfigDataMii() {
    initializeData();
}

void ConfigDataMii::setMiiOrIconId(const void* pMiiId, const u32* pIconId) {
    if (pMiiId != nullptr) {
        MR::copyMemory(&mMiiId, pMiiId, sizeof(mMiiId));

        mFlag |= Flag_MiiChosen;
        mIconId = IconId_Mii;
    } else {
        MR::fillMemory(&mMiiId, 0, sizeof(mMiiId));

        mIconId = *pIconId;
    }
}

bool ConfigDataMii::getIconId(u32* pIconId) const {
    *pIconId = mIconId;

    return mIconId != IconId_Mii;
}

bool ConfigDataMii::getMiiId(void* pMiiId) const {
    MR::copyMemory(pMiiId, &mMiiId, sizeof(mMiiId));

    return mIconId == IconId_Mii;
}

u32 ConfigDataMii::makeHeaderHashCode() const {
    return 0x2836E9;
}

u32 ConfigDataMii::getSignature() const {
    return 'MII ';
}

s32 ConfigDataMii::serialize(u8* pBuffer, u32 size) const {
    SaveWriter stream(pBuffer, static_cast< s32 >(size));

    stream.write(&mFlag, sizeof(mFlag));
    stream.write(&mMiiId, sizeof(mMiiId));
    stream.write(&mIconId, sizeof(mIconId));

    return stream.getPosition();
}

s32 ConfigDataMii::deserialize(const u8* pBuffer, u32 size) {
    initializeData();

    SaveReader stream(pBuffer, static_cast< s32 >(size));

    stream.read(&mFlag, sizeof(mFlag));
    stream.read(&mMiiId, sizeof(mMiiId));

    if (stream.getAvailable() == 0) {
        if ((mFlag & Flag_UseMii) != 0) {
            mIconId = IconId_Mii;
        }
    } else {
        stream.read(&mIconId, sizeof(mIconId));
    }

    return 0;
}

void ConfigDataMii::initializeData() {
    mFlag = 0;
    mIconId = IconId_Mario;

    MR::zeroMemory(&mMiiId, sizeof(mMiiId));
}
