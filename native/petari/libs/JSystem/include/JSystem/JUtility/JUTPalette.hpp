#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>
#include <revolution/os.h>

enum JUTTransparency { UNK0, UNK1 };

struct ResTLUT {
    /* 0x0 */ u8 mFormat;
    /* 0x1 */ u8 mTransparency;
    /* 0x2 */ u16 mLutNum;
};

class JUTPalette {
public:
    JUTPalette(_GpuTlut p1, _GpuTlutFmt p2, JUTTransparency p3, u16 p4, void* p5) {
        this->storeTLUT(p1, p2, p3, p4, p5);
    }

    JUTPalette(GpuTlut tlutNo, ResTLUT* p_tlutRes) {
        storeTLUT(tlutNo, p_tlutRes);
    }

    void storeTLUT(GpuTlut, ResTLUT*);
    void storeTLUT(GpuTlut, GpuTlutFmt, JUTTransparency, u16, void*);

    /* 0x00 */ GpuTlutObj mObj;
    /* 0x0C */ u8 mName;
    /* 0x0D */ u8 mFormat;
    /* 0x10 */ ResTLUT* mColorTable;
    /* 0x14 */ u16 mLutNum;
    /* 0x16 */ u8 mTransparency;
};
