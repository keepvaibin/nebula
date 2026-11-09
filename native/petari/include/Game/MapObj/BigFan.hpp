#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ModelObj;

class BigFan : public LiveActor {
public:
    BigFan(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;

    void initWindModel();
    void calcWindInfo(Vec3f*, const Vec3f&);
    void start();

    void exeStop();
    void exeStart();
    void exeWait();

    bool isStartOrWait();

    ModelObj* mWindModel;
    Vec3f mClippingCenter;
    f32 mWindLength;
    f32 mWindSpeed;
    bool mIsTeresaGalaxy;
};
