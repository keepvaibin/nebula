#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ProjmapEffectMtxSetter;

class FileSelectSky : public LiveActor {
public:
    /// @brief Creates a new `FileSelectSky`.
    /// @param pName A pointer to the null-terminated name of the object.
    FileSelectSky(const char* pName);

    /// @brief Destroys the `FileSelectSky`.
    virtual ~FileSelectSky();

    virtual void init(const JMapInfoIter& rIter);
    virtual void calcAnim();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeWait();

private:
    f32 mAngleX;
    f32 mAngleY;
    TPos3f mBaseMtx;
    ProjmapEffectMtxSetter* mProjmapEffectMtxSetter;
};
