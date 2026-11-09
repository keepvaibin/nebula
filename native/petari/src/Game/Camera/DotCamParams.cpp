#include "Game/Camera/DotCamParams.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Table.hpp"

DotCamReaderInBin::DotCamReaderInBin(const void* pData) : mVersion(), mMapInfo() {
    init(pData);
}

bool DotCamReaderInBin::hasMoreChunk() const {
    return mMapIter.isValid() && mMapIter != mMapInfo.end();
}

void DotCamReaderInBin::nextToChunk() {
    if (mMapIter.isValid()) {
        mMapIter.mIndex++;
    }
}

bool DotCamReaderInBin::getValueInt(const char* pName, s32* pOut) {
    return mMapIter.getValue< s32 >(pName, pOut);
}

bool DotCamReaderInBin::getValueFloat(const char* pName, f32* pOut) {
    return mMapIter.getValue< f32 >(pName, pOut);
}

bool DotCamReaderInBin::getValueString(const char* pName, const char** pOut) {
    return mMapIter.getValue(pName, pOut);
}

DotCamReader::~DotCamReader() {
}

u32 DotCamReaderInBin::getVersion() const {
    return mVersion;
}

void DotCamReaderInBin::init(const void* pData) {
    mMapInfo.attach(pData);
    mMapInfo.begin().getValue("version", &mVersion);

    mMapIter = mMapInfo.begin();
}

bool DotCamReaderInBin::getValueVec(const char* pName, Vec3f* pOut) {
    MR::FormattedName key;

    f32 z;
    f32 y;
    f32 x;

    key.format("%s.X", pName);
    bool success = mMapIter.getValue< f32 >(key.c_str(), &x) & 1;

    key.format("%s.Y", pName);
    success &= mMapIter.getValue< f32 >(key.c_str(), &y);

    key.format("%s.Z", pName);
    success &= mMapIter.getValue< f32 >(key.c_str(), &z);

    if (success) {
        pOut->x = x;
        pOut->y = y;
        pOut->z = z;
    }

    return success;
}
