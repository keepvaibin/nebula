#include "Game/Camera/CameraParamChunkID.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstring>

CameraParamChunkID::CameraParamChunkID() {
    mZoneID = 0;
    mName = nullptr;
}

CameraParamChunkID::CameraParamChunkID(const CameraParamChunkID& other) {
    mZoneID = other.mZoneID;
    copyName(other.mName);
}

// Zone first, then name; an unnamed id is greater than every named one.
bool CameraParamChunkID::operator>(const CameraParamChunkID& other) const {
    if (mName == nullptr) {
        return other.mName != nullptr;
    }

    if (other.mName == nullptr) {
        return false;
    }

    if (mZoneID != other.mZoneID) {
        return mZoneID > other.mZoneID;
    }

    const char* pLhs = mName;
    const char* pRhs = other.mName;

    while (*pLhs != '\0' && *pLhs == *pRhs) {
        pLhs++;
        pRhs++;
    }

    return static_cast< u8 >(*pLhs) > static_cast< u8 >(*pRhs);
}

bool CameraParamChunkID::operator==(const CameraParamChunkID& other) const {
    if (mName == nullptr) {
        return other.mName == nullptr;
    }

    if (other.mName == nullptr) {
        return false;
    }

    return mZoneID == other.mZoneID && MR::isEqualString(mName, other.mName);
}

bool CameraParamChunkID::equals(s32 zoneID, const char* pName) const {
    if (mName != nullptr) {
        return zoneID == mZoneID && MR::isEqualString(mName, pName);
    }

    return false;
}

char* CameraParamChunkID::getBuffer(u32 length) {
    return new char[length];
}

void CameraParamChunkID::createCubeID(s32 zoneID, u16 id) {
    mZoneID = static_cast< s8 >(zoneID);

    setName(MR::FormattedName("c:%04x", id));
}

void CameraParamChunkID::createGroupID(s32 zoneID, const char* pName, u32 index, u32 subIndex) {
    mZoneID = static_cast< s8 >(zoneID);

    setName(MR::FormattedName("g:%s:%d:%d", pName, index, subIndex));
}

void CameraParamChunkID::createOtherID(s32 zoneID, const char* pName) {
    mZoneID = static_cast< s8 >(zoneID);

    setName(MR::FormattedName("o:%s", pName));
}

void CameraParamChunkID::createEventID(s32 zoneID, const char* pName) {
    mZoneID = static_cast< s8 >(zoneID);

    setName(MR::FormattedName("e:%s", pName));
}

void CameraParamChunkID::createStartID(s32 zoneID, u16 id) {
    mZoneID = static_cast< s8 >(zoneID);

    setName(MR::FormattedName("s:%04x", id));
}

void CameraParamChunkID::setName(const MR::FormattedName& rName) {
    copyName(rName.c_str());
}

void CameraParamChunkID::copyName(const char* pName) {
    const u32 size = strlen(pName) + 1;
    char* const pBuffer = getBuffer(size);

    memcpy(pBuffer, pName, size);
    mName = pBuffer;
}
