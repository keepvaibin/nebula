#pragma once


class JKRSolidHeap;

class JAU_JASInitializer {
public:
    JAU_JASInitializer();
    void initJASystem(JKRSolidHeap* pHeap) NO_INLINE;

    /* 0x00 */ u32 audioMemory_;
    /* 0x04 */ u32 audioMemSize_;
    /* 0x08 */ int heapSize_;
    /* 0x1C */ int field_0x1c;
    /* 0x20 */ int field_0x20;
    /* 0x2C */ f32 outputLevel_;
    /* 0x30 */ char* waveArcDir_;
};

class JAU_JAIInitializer {
public:
    JAU_JAIInitializer();
    void initJAInterface();

    int mMaxNumSe;
    int mMaxNumSeq;
    int mMaxNumStream;
    int mMaxNumChildren;
};
