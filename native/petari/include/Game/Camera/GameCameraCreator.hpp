#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class CameraParamChunkHolder;

class GameCameraCreator {
public:
    GameCameraCreator(CameraParamChunkHolder*);

    void initCameraCodeCollection(const char*, s32);
    void registerCameraCode(u32);
    void termCameraCodeCollection();
    void scanStartPos();
    void scanArea();

    CameraParamChunkHolder* mChunkHolder;
    const char* mName;
    s32 _8;
    u8 mCodes[0x100];
};
