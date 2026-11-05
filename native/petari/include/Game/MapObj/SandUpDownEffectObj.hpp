#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class TriangleFilterFunc;

class SandUpDownEffectObj : public LiveActor {
public:
    /// @brief Creates a new `SandUpDownEffectObj`.
    /// @param pName A pointer to the null-terminated name of the object.
    SandUpDownEffectObj(const char* pName);

    virtual void init(const JMapInfoIter& rIter);

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
    f32 _8C;
    f32 _90;
    Vec3f _94;
    Pos3f _A0;
    TriangleFilterFunc* _D0;
};
