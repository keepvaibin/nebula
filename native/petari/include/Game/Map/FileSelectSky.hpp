#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ProjmapEffectMtxSetter;

class FileSelectSky : public LiveActor {
public:
    FileSelectSky(const char* pName);

    virtual ~FileSelectSky() override;

    virtual void init(const TableIter& rIter) override;
    virtual void calcAnim() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void exeWait();

private:
    f32 mAngleX;
    f32 mAngleY;
    Pos3f mBaseMtx;
    ProjmapEffectMtxSetter* mProjmapEffectMtxSetter;
};
