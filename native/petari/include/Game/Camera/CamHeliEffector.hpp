#pragma once


class CameraMan;

class CamHeliEffector {
public:
    CamHeliEffector();

    void update(CameraMan*);

    s32 mStep;
    f32 mRate;
};
