#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Color.hpp"

class ProjmapEffectMtxSetter;

class AstroDomeSky : public LiveActor {
public:
    AstroDomeSky(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void draw() const;
    virtual void calcAndSetBaseMtx();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    bool tryRotateAppearance();
    void exeHide();
    void exeAppear();
    void exeWait();
    void exeRotateAppear();
    void exeRotateDisappear();
    void exeDisappear();
    void exeReturnDemoWait();
    void exeJumpOutDemo() {};

    f32 mBrkFrame;
    Color8 mColor;
    ProjmapEffectMtxSetter* mProjmap;
};
