#pragma once

#include "Game/Sound/Sound.hpp"
#include "Game/Sound/SoundData.hpp"
#include "Game/Sound/SoundGlobal.hpp"

// The archive's sound tree, read in place: sections, groups, items.
class SoundTree {
public:
    void init(const SoundArchiveFile* pFile);

protected:
    const SoundSection* section(SoundID id, u32 in) const;
    const SoundArchiveFile* mFile = nullptr;
    const SoundSection* mSections = nullptr;
    const SoundGroup* mGroups = nullptr;
    const SoundItem* mItems = nullptr;
    const char* mNames = nullptr;
};

// Each sound's type and values.
class SoundTable : public SoundGlobal< SoundTable >, public SoundTree {
public:
    SoundTable(bool setInstance) : SoundGlobal< SoundTable >(setInstance) {
    }

    // 0xff: no such sound.
    u8 getTypeID(SoundID id) const;
    const SoundItem* getData(SoundID id) const;
    // A stream's file in the store.
    const char* getStreamPath(const SoundItem* pItem) const {
        return pItem->mStreamPath != ~0u ? mNames + pItem->mStreamPath : nullptr;
    }

private:
    // The id's entry when its section and group are in the table (the entry itself may not be).
    const SoundItem* findItem(SoundID id) const;
};

// Each sound's name.
class SoundNameTable : public SoundGlobal< SoundNameTable >, public SoundTree {
public:
    SoundNameTable(bool setInstance) : SoundGlobal< SoundNameTable >(setInstance) {
    }

    // -1: no such section or group.
    int getNumGroups_inSection(u8 sectionID) const;
    int getNumItems_inGroup(u8 sectionID, u8 groupID) const;
    // "" for a section or group the names leave out, null for an item they leave out.
    const char* getName(SoundID id) const;
};
