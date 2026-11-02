#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LotusLeaf : public LiveActor {
public:
    /// @brief Creates a new `LotusLeaf`.
    /// @param pName A pointer to the null-terminated name of the object.
    LotusLeaf(const char* pName);

    virtual void init(const JMapInfoIter&);

    void convergeToInitPos();

    void exeWait();
    void exeWaitPlayerOn();
    void exeShake();

    TVec3f mInitPos;
    f32 mShakeSpeed;
    f32 mShakePeriod;
};
