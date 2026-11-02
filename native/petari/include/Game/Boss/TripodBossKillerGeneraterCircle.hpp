#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class TripodBossKillerGenerater;

class TripodBossKillerGeneraterCircle : public NameObj {
public:
    /// @brief Creates a new `TripodBossKillerGeneraterCircle`.
    /// @param pName A pointer to the null-terminated name of the object.
    TripodBossKillerGeneraterCircle(const char* pName);

    virtual ~TripodBossKillerGeneraterCircle();
    virtual void init(const JMapInfoIter&);

    void placementGenerater();

    TripodBossKillerGenerater* mGenerater;
    TVec3f mPosition;
    TVec3f mRotation;
    const f32* mAngleTable;
    s32 mAngleNum;
    f32 _30;
    f32 _34;
    f32 _38;
    f32 _3C;
    u32 _40;
};
