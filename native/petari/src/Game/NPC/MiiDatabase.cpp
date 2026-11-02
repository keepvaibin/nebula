#include "Game/NPC/MiiDatabase.hpp"
#include "Game/Util/Array.hpp"
#include <JSystem/JKernel/JKRFileFinder.hpp>
#include <RVLFaceLib.h>

struct MiiDatabaseStoreData {
    /* 0x00 */ MiiStoreData* mData;
    /* 0x04 */ MR::AssignableArray< MiiStoreData* > mEntries;
};

MiiDatabase::~MiiDatabase() {
    delete[] _0;
    delete mStoreData->mData;
    delete mStoreData;
}

template MR::AssignableArray< MiiStoreData* >::~AssignableArray();
