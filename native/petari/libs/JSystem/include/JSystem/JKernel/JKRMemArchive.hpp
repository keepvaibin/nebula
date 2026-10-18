#pragma once

#include "JSystem/JKernel/JKRArchive.hpp"

enum JKRMemBreakFlag { JKR_MEM_BREAK_FLAG_0 = 0, JKR_MEM_BREAK_FLAG_1 = 1 };

class JKRMemArchive : public JKRArchive {
public:
    JKRMemArchive();
    JKRMemArchive(s32, EMountDirection);
    virtual ~JKRMemArchive();

    virtual void removeResourceAll();
    virtual bool removeResource(void*);
    virtual u32 getExpandedResSize(const void*) const;
    virtual void* fetchResource(SDIFileEntry*, u32*);
    virtual void* fetchResource(void*, u32, SDIFileEntry*, u32*);

    void fixedInit(s32);
    bool mountFixed(void*, JKRMemBreakFlag);
    bool open(s32, EMountDirection);
    bool open(void*, u32, JKRMemBreakFlag);
    static s32 fetchResource_subroutine(unsigned char*, u32, unsigned char*, u32, int);

    /* 0x64 */ RarcHeader* mHeader;
    /* 0x68 */ u8* mFileDataStart;
    /* 0x6C */ bool _6C;
    /* 0x6D */ u8 _6D[3];
};
