#include "Game/NPC/MiiDatabase.hpp"
#include "Game/Util/Array.hpp"
#include <MiiLib.h>

struct MiiDatabaseStoreData {
    MiiStoreData* mData;
    MR::AssignableArray< MiiStoreData* > mEntries;
};

MiiDatabase::~MiiDatabase() {
    delete[] _0;
    delete mStoreData->mData;
    delete mStoreData;
}

template MR::AssignableArray< MiiStoreData* >::~AssignableArray();
