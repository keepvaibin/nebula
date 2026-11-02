#pragma once

#include "JSystem/JKernel/JKRFileLoader.hpp"

class JKRArcFinder;

#define RARC_MAGIC 0x52415243
#define NULL_MAGIC 0
#define QUESTIONMARK_MAGIC 0x3F3F3F3F

#define MAX_NAME_LENGTH 0x100

class JKRArchive : public JKRFileLoader {
public:
    enum EMountDirection { MOUNT_DIRECTION_1 = 1 };

    enum EMountMode { MOUNT_MODE_0 = 0, MOUNT_MODE_MEM = 1 };

    enum EFileFlag {
        FILE_FLAG_FILE_SHIFT = 0,
        FILE_FLAG_FOLDER_SHIFT = 1,
        FILE_FLAG_COMPRESSED_SHIFT = 2,
        FILE_FLAG_MRAM_SHIFT = 4,
        FILE_FLAG_ARAM_SHIFT = 5,
        FILE_FLAG_DVD_SHIFT = 6,
        FILE_FLAG_IS_YAZ0_SHIFT = 7,

        FILE_FLAG_FILE = 1 << FILE_FLAG_FILE_SHIFT,
        FILE_FLAG_FOLDER = 1 << FILE_FLAG_FOLDER_SHIFT,
        FILE_FLAG_COMPRESSED = 1 << FILE_FLAG_COMPRESSED_SHIFT,
        FILE_FLAG_MRAM = 1 << FILE_FLAG_MRAM_SHIFT,
        FILE_FLAG_ARAM = 1 << FILE_FLAG_ARAM_SHIFT,
        FILE_FLAG_DVD = 1 << FILE_FLAG_DVD_SHIFT,
        FILE_FLAG_IS_YAZ0 = 1 << FILE_FLAG_IS_YAZ0_SHIFT
    };

    // An archive in host order, as the importer cooks it (native_data.rs rarc_host); a disc one panics.
    static void panicIfNotCooked(const void* pHeader);

    struct RarcHeader {
        /* 0x00 */ u32 mMagic;
        /* 0x04 */ u32 mFileSize;
        /* 0x08 */ u32 mHeaderSize;
        /* 0x0C */ u32 mFileDataOffset;
        /* 0x10 */ u32 mTotalDataSize;
        /* 0x14 */ u32 mMRamDataSize;
        /* 0x18 */ u32 mARamDataSize;
        /* 0x1C */ u32 _1C;
    };

    struct RarcInfoBlock {
        /* 0x00 */ u32 mNrDirs;
        /* 0x04 */ u32 mDirOffset;
        /* 0x08 */ u32 mNrFiles;
        /* 0x0C */ u32 mFileOffset;
        /* 0x10 */ u32 mStringTableSize;
        /* 0x14 */ u32 mStringTableOffset;
        /* 0x18 */ u16 mNextAvailableFileID;
        /* 0x1A */ u16 mFileIDIsIndex;
        /* 0x1C */ u32 _1C;
    };

    struct SDIFileEntry {
        /* 0x00 */ u16 mFileID;
        /* 0x02 */ u16 mHash;
        // The flags-and-name word in host order: the flags in its top byte (the low 24 bits are the name's offset).
        /* 0x04 */ u32 mNameOffset : 24;
        /* 0x07 */ u32 mFlag : 8;
        union {
            /* 0x08 */ u32 mDataOffset;
            /* 0x08 */ u32 mDirIndex;
        };
        /* 0x0C */ u32 mDataSize;
        // A host pointer does not fit the 4-byte slot (and entries are stepped by their 20-byte size): the archive keeps
        // each entry's loaded data in its own table (JKRArchive::fileData).
        /* 0x10 */ u32 mFileDataSlot;
    };

    struct SDIDirEntry {
        /* 0x0 */ u32 mID;
        /* 0x4 */ u32 mNameOffset;
        /* 0x8 */ u16 mHash;
        /* 0xA */ u16 mNrFiles;
        /* 0xC */ u32 mFirstFileIndex;
    };

    struct SDirEntry {
        /* 0x0 */ u8 mFileFlag;
        /* 0x1 */ u8 _1;
        /* 0x2 */ u16 mFileID;
        /* 0x4 */ char* mName;
    };

    class CArcName {
    public:
        inline CArcName() {
        }

        CArcName(char const* data) {
            this->store(data);
        }
        CArcName(char const** data, char endChar) {
            *data = this->store(*data, endChar);
        }

        void store(const char*);
        const char* store(const char*, char);

        /* 0x0 */ u16 mHash;
        /* 0x2 */ u16 mLength;
        /* 0x4 */ char mName[256];
    };

    JKRArchive();
    JKRArchive(s32, EMountMode);
    virtual ~JKRArchive();

    virtual bool becomeCurrent(const char*);
    virtual void* getResource(const char*);
    virtual void* getResource(u32, const char*);
    virtual u32 readResource(void*, u32, const char*);
    virtual u32 readResource(void*, u32, u32, const char*);
    virtual void removeResourceAll();
    virtual bool removeResource(void*);
    virtual bool detachResource(void*);
    virtual s32 getResSize(const void*) const;
    virtual s32 countFile(const char*) const;
    JKRArcFinder* getFirstFile(const char*) const;
    virtual void* fetchResource(SDIFileEntry*, u32*) = 0;
    virtual void* fetchResource(void*, u32, SDIFileEntry*, u32*) = 0;

    // The importer expands every member and clears its compressed flag; a set flag was not cooked.
    void panicIfCompressedEntry(const SDIFileEntry* pEntry) const;

    static JKRArchive* check_mount_already(s32);
    static JKRArchive* check_mount_already(s32, NebulaArena*);
    static JKRArchive* mount(const char*, EMountMode, NebulaArena*, EMountDirection);
    static JKRArchive* mount(s32, EMountMode, NebulaArena*, EMountDirection);
    bool getDirEntry(SDirEntry*, u32) const;
    void* getIdxResource(u32);
    void* getResource(u16);
    u32 readResource(void*, u32, u16);
    u32 countResource() const;
    u32 getFileAttribute(u32) const;
    bool isSameName(CArcName&, u32, u16) const;
    SDIDirEntry* findResType(u32) const;
    SDIDirEntry* findDirectory(const char*, u32) const;
    SDIFileEntry* findTypeResource(u32, const char*) const;
    SDIFileEntry* findFsResource(const char*, u32) const;
    SDIFileEntry* findIdxResource(u32) const;
    SDIFileEntry* findNameResource(const char*) const;
    SDIFileEntry* findPtrResource(const void*) const;
    SDIFileEntry* findIdResource(u16) const;

    /* 0x806B7148 */ static u32 sCurrentDirID;

    /* 0x38 */ NebulaArena* mHeap;
    /* 0x3C */ u8 mMountMode;
    /* 0x3D */ u8 _3D[3];
    /* 0x40 */ s32 mEntryNum;
    /* 0x44 */ RarcInfoBlock* mInfoBlock;
    /* 0x48 */ SDIDirEntry* mDirs;
    /* 0x4C */ SDIFileEntry* mFiles;
    /* 0x54 */ char* mStringTable;
    /* 0x58 */ u32 _58;
    /* 0x60 */ EMountDirection mMountDir;

    // Each file entry's loaded data, indexed like mFiles.
    mutable void** mFileDataTable = nullptr;

    void*& fileData(const SDIFileEntry* pEntry) const {
        if (mFileDataTable == nullptr) {
            allocFileDataTable();
        }
        return mFileDataTable[pEntry - mFiles];
    }
    void allocFileDataTable() const;
    void freeFileDataTable();
};
