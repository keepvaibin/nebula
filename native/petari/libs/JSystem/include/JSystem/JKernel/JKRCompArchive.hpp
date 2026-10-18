#pragma once

#include "JSystem/JKernel/JKRArchive.hpp"

class JKRAramBlock;
class JKRDvdFile;

class JKRCompArchive : public JKRArchive {
public:
    JKRCompArchive(s32, EMountDirection);
    virtual ~JKRCompArchive(void);

    virtual void removeResourceAll();
    virtual bool removeResource(void*);
    virtual u32 getExpandedResSize(const void*) const;
    virtual void* fetchResource(SDIFileEntry*, u32*);
    virtual void* fetchResource(void*, u32, SDIFileEntry*, u32*);

    bool open(s32);

#ifdef TARGET_PC
    // The address of the archive's file data in main RAM.
    /* 0x64 */ uintptr_t field_0x64;
#else
    /* 0x64 */ int field_0x64;
#endif
    /* 0x68 */ JKRAramBlock* mAramPart;
    /* 0x6C */ int field_0x6c;
    /* 0x70 */ JKRDvdFile* mDvdFile;
    /* 0x74 */ u32 mSizeOfMemPart;
    /* 0x78 */ u32 mSizeOfAramPart;
    /* 0x7C */ int field_0x7c;
};
