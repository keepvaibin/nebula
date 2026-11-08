#include "Game/System/ConfigDataMisc.hpp"
#include "Game/System/NebulaCalendar.hpp"
#include "Game/System/SaveBytes.hpp"

namespace {
    // The saved flag byte.
    enum Flag {
        Flag_LastLoadedMario = 0x1,
        Flag_CompleteEndingMario = 0x2,
        Flag_CompleteEndingLuigi = 0x4,
    };
}  // namespace

ConfigDataMisc::ConfigDataMisc() {
    initializeData();
}

bool ConfigDataMisc::isLastLoadedMario() const {
    return (mFlag & ::Flag_LastLoadedMario) != 0;
}

void ConfigDataMisc::setLastLoadedMario(bool lastLoadedMario) {
    if (lastLoadedMario) {
        mFlag |= ::Flag_LastLoadedMario;
    } else {
        mFlag &= ~::Flag_LastLoadedMario;
    }
}

void ConfigDataMisc::onCompleteEndingMario() {
    mFlag |= ::Flag_CompleteEndingMario;
}

void ConfigDataMisc::onCompleteEndingLuigi() {
    mFlag |= ::Flag_CompleteEndingLuigi;
}

bool ConfigDataMisc::isOnCompleteEndingMario() {
    return (mFlag & ::Flag_CompleteEndingMario) != 0;
}

bool ConfigDataMisc::isOnCompleteEndingLuigi() {
    return (mFlag & ::Flag_CompleteEndingLuigi) != 0;
}

NebulaCalendar::SaveTime ConfigDataMisc::getLastModified() const {
    return mLastModified;
}

void ConfigDataMisc::updateLastModified() {
    mLastModified = NebulaCalendar::now();
}

u32 ConfigDataMisc::makeHeaderHashCode() const {
    return 0x1;
}

u32 ConfigDataMisc::getSignature() const {
    return 'MISC';
}

s32 ConfigDataMisc::serialize(u8* pBuffer, u32 size) const {
    SaveWriter stream(pBuffer, size);

    const u8 flag = mFlag;
    stream.write(&flag, sizeof(flag));

    const s64 lastModified = mLastModified;
    stream.write(&lastModified, sizeof(lastModified));

    return stream.getPosition();
}

s32 ConfigDataMisc::deserialize(const u8* pBuffer, u32 size) {
    initializeData();

    SaveReader stream(pBuffer, size);

    u8 flag;
    stream.read(&flag, sizeof(flag));
    mFlag = flag;

    // Saves from before the timestamp end after the flag byte.
    if (stream.getAvailable() == 0) {
        mLastModified = 0;
    } else {
        s64 lastModified;
        stream.read(&lastModified, sizeof(lastModified));
        mLastModified = NebulaCalendar::fromSaved(lastModified);
    }

    return 0;
}

void ConfigDataMisc::initializeData() {
    mFlag = ::Flag_LastLoadedMario;
    mLastModified = 0;
}
