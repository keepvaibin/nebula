#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Array.hpp"

class ModelObj;

class ChooChooTrain : public LiveActor {
public:
    /// @brief Creates a new `ChooChooTrain`.
    /// @param pName A pointer to the null-terminated name of the object.
    ChooChooTrain(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();

    MR::Vector< MR::AssignableArray< ModelObj* > > mModelArray;
    TVec3f _98;
    f32 mRailSpeed;
    s32 mWhistleTimer;
};
