#pragma once

#include <revolution/gx.h>
#include <revolution/mtx.h>
#include <revolution/os.h>

class CameraMan;

class CamKarikariEffector {
public:
    CamKarikariEffector();

    void update(CameraMan*);

    /* 0x00 */ s32 mCounter;
};
