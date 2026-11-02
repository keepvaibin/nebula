#pragma once

#include "JSystem/JAudio2/JAISound.hpp"
#include "JSystem/JAudio2/JASGlobal.hpp"
#include "Game/Util/ByteView.hpp"

// The sound tables (BST, BSTN) in host order, as the importer cooks the audio archive (native_data.rs baa).
struct JAUSoundTableItem {
    u8 mPriority;
    u8 mVolume;
    u16 mResourceId;
    u8 _4[0];
};

struct JAUSoundTableSe {
    u8 mPriority;
    u8 mVolume;
    u16 mAudibleSw;
    u16 mSoundSw;
};

struct JAUSoundTableBgm {
    u8 mPriority;
    u8 mVolume;
    u16 mResourceId;
    u16 mChordResId;
    u16 mSoundSw;
};

struct JAUSoundTableStream {
    u8 mPriority;
    u8 mVolume;
    u16 mChannelCtrl;
    u32 mStreamFileNameOffset;
};

template < typename Root, typename Section, typename Group, typename Item >
struct JAUSoundTable_ {
    JAUSoundTable_() {
        mData = nullptr;
        mRoot = nullptr;
    }

    void reset() {
        mData = nullptr;
        mRoot = nullptr;
    }

    void init(const void* pData) {
        mData = pData;
        if (readBytes< u32 >(mData) != Root::magicNumber()) {
            mData = nullptr;
        } else {
            mRoot = (Root*)((u8*)mData + *((const u32*)mData + 3));
        }
    }

    Section* getSection(int index) const {
        if (index < 0) {
            return nullptr;
        }
        if ((u32)index >= mRoot->mSectionNumber) {
            return nullptr;
        }
        u32 offset = mRoot->mSectionOffsets[index];
        if (offset == 0) {
            return nullptr;
        }
        return (Section*)((u8*)mData + offset);
    }

    Group* getGroup(const Section* section, int index) const {
        if (index < 0) {
            return nullptr;
        }
        if ((u32)index >= section->mNumGroups) {
            return nullptr;
        }
        u32 offset = section->getGroupOffset(index);
        if (offset == 0) {
            return nullptr;
        }
        return (Group*)((u8*)mData + offset);
    }

    Item* getItem(const Group* group, int index) const {
        u32 offset = group->getItemOffset(index);
        if (offset == 0) {
            return nullptr;
        }
        return (Item*)((u8*)mData + offset);
    }

    const void* mData;
    Root* mRoot;
};

struct JAUSoundTableRoot {
    static inline u32 magicNumber() {
        return 'BST ';
    }
    u32 mSectionNumber;
    u32 mSectionOffsets[0];
};

struct JAUSoundTableSection {
    int getGroupOffset(int index) const {
        if (index < 0) {
            return 0;
        }
        if (index >= mNumGroups) {
            return 0;
        }
        return mGroupOffsets[index];
    }

    u32 mNumGroups;
    u32 mGroupOffsets[0];
};

struct JAUSoundTableGroup {
    u8 getTypeID(int index) const {
        if (index < 0) {
            return 0;
        }
        if (index >= mNumItems) {
            return -1;
        }
        return mItemOffsets[index] >> 24;  // the type: the offset word's top byte
    }

    u32 getItemOffset(int index) const {
        if (index < 0) {
            return 0;
        }
        if (index >= mNumItems) {
            return 0;
        }
        return mItemOffsets[index] & 0xffffff;
    }

    u32 mNumItems;
    u32 mGlobalOffset;
    u32 mItemOffsets[0];
};

struct JAUSoundTable : public JASGlobalInstance< JAUSoundTable > {
    JAUSoundTable(bool setInstance) : JASGlobalInstance< JAUSoundTable >(setInstance) {
    }
    ~JAUSoundTable() {
    }

    void init(void const*);
    u8 getTypeID(JAISoundID) const;
    JAUSoundTableItem* getData(JAISoundID) const;
    int getNumGroups_inSection(u8) const;
    int getNumItems_inGroup(u8, u8) const;

    JAUSoundTableItem* getItem(const JAUSoundTableGroup* group, int index) const {
        return (JAUSoundTableItem*)mTable.getItem(group, index);
    }

    const void* getResource() const {
        return mTable.mData;
    }
    bool isValid() const {
        return mTable.mData != nullptr;
    }

    JAUSoundTable_< JAUSoundTableRoot, JAUSoundTableSection, JAUSoundTableGroup, void > mTable;
};

struct JAUSoundNameTableRoot {
    static inline u32 magicNumber() {
        return 'BSTN';
    }
    u32 mSectionNumber;
    u32 mSectionOffsets[0];
};

struct JAUSoundNameTableSection {
    int getGroupOffset(int index) const {
        if (index < 0) {
            return 0;
        }
        if (index >= mNumGroups) {
            return 0;
        }
        return mGroupOffsets[index];
    }

    u32 mNumGroups;
    u32 mGlobalOffset;
    u32 mGroupOffsets[0];
};

struct JAUSoundNameTableGroup {
    u32 getItemOffset(int index) const {
        if (index < 0) {
            return 0;
        }
        if (index >= mNumItems) {
            return 0;
        }
        return mItemOffsets[index];
    }

    u32 mNumItems;
    u32 mCategoryNameOffset;
    u32 mItemOffsets[0];
};

struct JAUSoundNameTable : public JASGlobalInstance< JAUSoundNameTable > {
    JAUSoundNameTable(bool setInstance) : JASGlobalInstance< JAUSoundNameTable >(setInstance) {
    }
    ~JAUSoundNameTable() {
    }
    int getNumGroups_inSection(u8) const;
    int getNumItems_inGroup(u8, u8) const;
    void init(void const*);
    const char* getName(JAISoundID) const;
    const char* getGroupName(JAISoundID) const;

    const char* getItem(const JAUSoundNameTableGroup* group, int index) const {
        return (const char*)mTable.getItem(group, index);
    }

    JAUSoundTable_< JAUSoundNameTableRoot, JAUSoundNameTableSection, JAUSoundNameTableGroup, char > mTable;
};
