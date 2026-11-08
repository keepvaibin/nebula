#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioDamage : public MarioState {
public:
    MarioDamage(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();
    virtual bool notice();

    void setVec(const Vec3f&);
    void setVecSize(f32, f32);

    void stopHead(const Vec3f&);

    inline void setStrings(const char* pChar1, const char* pChar2) {
        mAirAnimation = pChar1;
        mLandAnimation = pChar2;
    }

    u8 mFlipLarge;  // a large flip (flipLarge): no damage effect, not damaging
    bool mDamaging;  // isDamaging
    u16 mTimer;  // frames since start
    u16 mAfterTimer;  // frames after the damage (getDamageAfterTimer): no new damage
    u16 mStep;  // update's step (0 in the air)
    Vec3f mVelocity;  // the knockback
    const char* mAirAnimation;  // the air animation
    const char* mLandAnimation;  // the landing animation
};
