#pragma once


struct MiiDatabaseStoreData;

class MiiDatabase {
public:
    ~MiiDatabase();

private:
    u8* _0;
    MiiDatabaseStoreData* mStoreData;
};
