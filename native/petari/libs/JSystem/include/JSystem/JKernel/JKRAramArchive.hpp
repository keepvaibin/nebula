#pragma once

#include "JSystem/JKernel/JKRArchive.hpp"

class JKRAramBlock;
class JKRFile;

class JKRAramArchive : public JKRArchive {
public:
    JKRAramArchive(s32, EMountDirection);
    virtual ~JKRAramArchive();

    virtual u32 getExpandedResSize(const void*) const;
    virtual void* fetchResource(SDIFileEntry*, u32*);
    virtual void* fetchResource(void*, u32, SDIFileEntry*, u32*);

    bool open(s32);
    static u32 fetchResource_subroutine(u32, u32, unsigned char*, u32, int);
    static u32 fetchResource_subroutine(u32, u32, JKRHeap*, int, unsigned char**);

    JKRAramBlock* mBlock;
    JKRFile* mDvdFile;
};

inline int JKRConvertAttrToCompressionType(int arg) {
    if ((arg & 0x4) == 0) {
        return 0;
    }

    return ((arg & 0x80) != 0) + 1;
}
