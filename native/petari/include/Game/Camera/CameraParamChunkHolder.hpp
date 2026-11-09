#pragma once

#include "Game/NameObj/NameObj.hpp"

class CameraHolder;
class CameraParamChunk;
class CameraParamChunkID;
class NebulaArena;

class CameraParamChunkHolder : public NameObj {
public:
    CameraParamChunkHolder(CameraHolder* pCameraHolder, const char* pName);

    CameraParamChunk* createChunk(const CameraParamChunkID&, NebulaArena*);
    CameraParamChunk* getChunk(const CameraParamChunkID&);
    void sort();
    void loadCameraParameters();
    bool isNewAttribute(const CameraParamChunkID&);
    CameraParamChunk* findChunk(const CameraParamChunkID&);
    CameraParamChunk* findChunk(s32, const char*);
    void loadFile(s32);
    void arrangeChunk(CameraParamChunk*);

    static const u32 cInitialCapacity = 0x400;

    CameraHolder* mCameraHolder;
    s32 mChunkCapacity;
    s32 mNrChunks;
    CameraParamChunk** mChunks;
    u32 mCameraVersion;
    bool mIsSorted;
};
