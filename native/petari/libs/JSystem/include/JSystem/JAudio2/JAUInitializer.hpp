#pragma once


class NebulaArena;

class JAU_JASInitializer {
public:
    JAU_JASInitializer();
    void initJASystem(NebulaArena* pHeap);

    int heapSize_;
    int field_0x1c;
    int field_0x20;
    f32 outputLevel_;
    char* waveArcDir_;
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
