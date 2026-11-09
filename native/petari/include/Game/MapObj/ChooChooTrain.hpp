#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Array.hpp"

class ModelObj;

class ChooChooTrain : public LiveActor {
public:
    ChooChooTrain(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void control() override;

    MR::Vector< MR::AssignableArray< ModelObj* > > mModelArray;
    Vec3f mClipCenter;
    f32 mRailSpeed;
    s32 mWhistleTimer;
};
