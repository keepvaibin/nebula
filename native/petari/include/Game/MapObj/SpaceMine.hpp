#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class SpaceMine : public MapObjActor {
public:
    SpaceMine(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void initAfterPlacement();
    virtual void kill();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    bool isCalcShadowAlways() const;
    void exeWait();
    void exeAppear();

    inline bool isShadowValid(s32 val) const {
        return val > -1 && val < 2;
    }

    f32 mRotateSpeed;
    Vec3f mClippingRange;
    s32 mShadowType;
    u8 mIsPlayerNear;
};
