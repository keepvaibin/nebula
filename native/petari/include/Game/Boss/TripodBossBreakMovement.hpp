#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class SpringValue;

class TripodBossBreakMovement : public LiveActor {
public:
    /// @brief Creates a new `TripodBossBreakMovement`.
    /// @param pName A pointer to the null-terminated name of the object.
    TripodBossBreakMovement(const char* pName);

    virtual void movement();

    void start(const Pos3f&, s32);
    void setBreakDownLevel(s32);

    void exeWait();
    void exeStartBreak();
    void exeBreak();

    Pos3f _8C;
    SpringValue* mSpring;
    Vec3f _C0;
    Vec3f _CC;
    Vec3f _D8;
    Vec3f _E4;
    f32 _F0;
    f32 _F4;
    f32 _F8;
    u32 _FC;
    s32 _100;
    u32 _104;
};
