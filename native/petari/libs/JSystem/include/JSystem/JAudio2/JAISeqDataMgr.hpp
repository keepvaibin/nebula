#pragma once

#include "JSystem/JAudio2/JAISound.hpp"
#include <stdint.h>

struct JAISeqData {
    JAISeqData(const void* _data, u32 _offset) {
        data = (void*)_data;
        offset = _offset;
    }

    void set(const void* _data, u32 _offset) {
        data = (void*)_data;
        offset = _offset;
    }

    void* data;
    u32 offset;
};

struct JAISeqDataRegion {
    bool intersects(const JAISeqData& seqData) const {
        if ((uintptr_t)addr + size < (uintptr_t)seqData.data) {
            return false;
        }
        if ((uintptr_t)seqData.data + seqData.offset < (uintptr_t)addr) {
            return false;
        }
        return true;
    }

    u8* addr;
    u32 size;
};

struct JAISeqDataUser {
    virtual ~JAISeqDataUser();
    virtual bool isUsingSeqData(JAISeqDataRegion const&) = 0;
    virtual int releaseSeqData(JAISeqDataRegion const&) = 0;
};

struct JAISeqDataMgr {
    enum SeqDataReturnValue { SeqDataReturnValue_0 = 0, SeqDataReturnValue_1 = 1, SeqDataReturnValue_2 = 2 };
    virtual ~JAISeqDataMgr();
    virtual SeqDataReturnValue getSeqData(JAISoundID, JAISeqData*) = 0;
    virtual int releaseSeqData() = 0;
    virtual bool setSeqDataUser(JAISeqDataUser*) = 0;
};
