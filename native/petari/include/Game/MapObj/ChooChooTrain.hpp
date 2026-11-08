#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Array.hpp"

class ModelObj;

class ChooChooTrain : public LiveActor {
public:
    /// @brief Creates a new `ChooChooTrain`.
    /// @param pName A pointer to the null-terminated name of the object.
    ChooChooTrain(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();

    MR::Vector< MR::AssignableArray< ModelObj* > > mModelArray;
    Vec3f mClipCenter;
    f32 mRailSpeed;
    s32 mWhistleTimer;
};
