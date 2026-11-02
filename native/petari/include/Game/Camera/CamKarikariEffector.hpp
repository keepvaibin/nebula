#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class CameraMan;

class CamKarikariEffector {
public:
    CamKarikariEffector();

    void update(CameraMan*);

    /* 0x00 */ s32 mCounter;
};
