#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ProjmapEffectMtxSetter;

class FileSelectSky : public LiveActor {
public:
    FileSelectSky(const char* pName);

    virtual ~FileSelectSky();

    virtual void init(const TableIter& rIter);
    virtual void calcAnim();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeWait();

private:
    f32 mAngleX;
    f32 mAngleY;
    Pos3f mBaseMtx;
    ProjmapEffectMtxSetter* mProjmapEffectMtxSetter;
};
