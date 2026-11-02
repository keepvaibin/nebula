#pragma once

#include "Game/Speaker/SpkTable.hpp"
#include "Game/Speaker/SpkWave.hpp"
#include "Game/System/AssetFolder.hpp"

class SpkData {
public:
    SpkData(AssetFolder*);

    void loadTable(u16);
    void loadWave(u16);
    bool isValid() const;

    SpkTable* getSpkTable() {
        return &mTable;
    }

    SpkWave* getWave() {
        return &mWave;
    }

    SpkTable mTable;
    SpkWave mWave;
    AssetFolder* mArchive;
};
