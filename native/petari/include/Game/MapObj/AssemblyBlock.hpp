#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class PartsModel;

class AssemblyBlock : public LiveActor {
public:
    /// @param A pointer to the null-terminated name of the object.
    AssemblyBlock(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void calcAndSetBaseMtx() override;

    void exeWait();
    void exeAssemble();
    void exeAssembleWait();
    void exeReturn();
    void exeTimer();
    bool tryStartAssemble();
    bool tryStartReturn();

    Pos3f mCurrentMtx;
    Pos3f mAssembledMtx;
    Pos3f mReturnMtx;
    s32 mObjArg7;
    f32 mPlayerSearchDistance;
    Vec3f mClipCenter;
    f32 mFloatRotateSpeed;
    s32 mTimer;
    PartsModel* mBloomModel;
    bool mIsTimerType;
};
