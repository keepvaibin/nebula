#pragma once

#include <nebula_geometry.hpp>

class LiveActor;

// How a ride hands Mario back: MarioActor::endRush reads it.
class RushEndInfo {
public:
    enum Mode : u32 {
        Mode_Wait = 0,      // he stops walking, then is let go
        Mode_Jump = 2,      // a free jump along mVec
        Mode_PowerJump = 3, // a power jump along mVec (weak gravity, launchers, damage, flips)
        Mode_Release = 4,   // let go where he is
    };
    enum Damage : u8 {
        Damage_None = 0,
        Damage_Hit = 1, // knocked along mVec
        Damage_Fire = 2,
        Damage_Freeze = 3,
        Damage_Acid = 4,
        Damage_Electric = 5,
        Damage_Flip = 6, // a large flip along mVec
    };

    RushEndInfo(LiveActor* pActor, Mode mode, const Vec3f& rVec, bool keepAnimation, u32 blendFrames);

    LiveActor* mActor;          // the ride that ends; null: whatever Mario rides
    Mode mMode;
    Vec3f mVec;                 // the jump, or the direction of the hit or flip
    bool mKeepAnimation;        // false: his animation stops
    u32 mBlendFrames;           // 0: no blend
    bool mForced = false;       // the "Force" jumps
    bool mInputOff = false;     // the "InputOff" jumps, damage and flips
    bool mLimitJump = false;    // a power jump with its limit
    bool mRollLanding = false;  // he lands rolling
    Damage mDamage = Damage_None;
};
