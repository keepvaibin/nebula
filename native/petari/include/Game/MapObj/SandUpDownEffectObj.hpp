#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class TriangleFilterFunc;

class SandUpDownEffectObj : public LiveActor {
public:
    SandUpDownEffectObj(const char* pName);

    virtual void init(const TableIter& rIter) override;

    void startMoveUp();
    void startMoveDown();
    bool checkSandHit();
    bool isUfoTop() const;

    void exeWait();
    void exeFallingRock();
    void exeFallingRockAndSmoke();
    void exeBurySmokePrepare();
    void exeBurySmoke();
    void exeBuryEnd();

private:
    f32 mLineOffset;
    f32 mLineLength;
    Vec3f mClippingCenter;
    Pos3f mEffectMtx;
    TriangleFilterFunc* mTriFilter;
};
