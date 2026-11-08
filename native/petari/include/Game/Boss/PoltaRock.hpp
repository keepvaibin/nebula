#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class Polta;
class WalkerStateBindStarPointer;

class PoltaRock : public LiveActor {
public:
    PoltaRock(const char*);

    virtual ~PoltaRock();
    virtual void init(const TableIter& rIter);
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void start(Polta*, const Vec3f&, const Vec3f&);
    void setColorWhite();
    void setColorBlack();
    void setColorYellow();
    void exeFloat();
    void exeSign();
    void exeMove();
    void generateKill();
    bool isEnableAttack() const;
    bool isEnableExplosion() const;

    inline Polta* getOwner() {
        return mOwner;
    }

    Polta* mOwner;
    Quat4f mRollQuat;
    Vec3f mChaseDir;
    Vec3f mLocalPos;
    s32 mType;
};
