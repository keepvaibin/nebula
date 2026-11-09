#include "Game/Map/FileSelectIconID.hpp"

FileSelectIconID::FileSelectIconID() : mIsMii(false), mData(0) {
}

FileSelectIconID::FileSelectIconID(const FileSelectIconID& rOther) {
    mIsMii = rOther.mIsMii;
    mData = rOther.mData;
}

void FileSelectIconID::set(const FileSelectIconID& rOther) {
    mIsMii = rOther.mIsMii;
    mData = rOther.mData;
}

void FileSelectIconID::setMiiIndex(u32 miiIndex) {
    mData = miiIndex;
    mIsMii = true;
}

bool FileSelectIconID::isMii() const {
    return mIsMii;
}

u32 FileSelectIconID::getMiiIndex() const {
    return mData;
}

void FileSelectIconID::setFellowID(EFellowID fellowID) {
    mData = fellowID;
    mIsMii = false;
}

bool FileSelectIconID::isFellow() const {
    return !mIsMii;
}

FileSelectIconID::EFellowID FileSelectIconID::getFellowID() const {
    // A saved icon number past the five characters shows Mario.
    if (mData > static_cast< u32 >(Peach)) {
        return Mario;
    }

    return static_cast< EFellowID >(mData);
}
