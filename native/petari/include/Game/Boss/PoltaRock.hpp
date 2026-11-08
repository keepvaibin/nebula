#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class Polta;

class PoltaRock : public LiveActor {
public:
    // Also the frame of the "Color" bva.
    enum Color {
        Color_White,
        Color_Black,
        Color_Yellow,
    };

    PoltaRock(const char*);

    virtual void init(const TableIter& rIter);
    virtual void kill();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void start(Polta* pOwner, const Vec3f& rPosition, const Vec3f& rOwnerLocalPos);
    void setColor(Color color);
    void exeFloat();
    void exeSign();
    void exeMove();
    void followOwner();
    void generateKill();
    bool isEnableAttack() const;
    bool isEnableExplosion() const;

    inline Polta* getOwner() {
        return mOwner;
    }

    Polta* mOwner;
    Quat4f mRollQuat;
    Vec3f mChaseDir;
    // Where the rock floats before it flies, in the owner's base frame.
    Vec3f mOwnerLocalPos;
    Color mColor;
};
