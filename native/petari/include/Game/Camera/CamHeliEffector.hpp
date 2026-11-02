#pragma once


class CameraMan;

class CamHeliEffector {
public:
    CamHeliEffector();

    void update(CameraMan*);

    /* 0x0 */ s32 _0;
    /* 0x4 */ f32 _4;
};
