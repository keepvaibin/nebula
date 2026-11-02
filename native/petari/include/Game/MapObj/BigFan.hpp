#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ModelObj;

class BigFan : public LiveActor {
public:
    BigFan(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void control();

    void initWindModel();
    void calcWindInfo(TVec3f*, const TVec3f&);
    void start();

    void exeStop();
    void exeStart();
    void exeWait();

    bool isStartOrWait();

    ModelObj* mWindModel;
    TVec3f mClippingCenter;
    f32 mWindLength;
    f32 mWindSpeed;
    bool mIsTeresaGalaxy;
};
