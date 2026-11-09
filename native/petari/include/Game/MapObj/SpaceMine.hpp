#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class SpaceMine : public MapObjActor {
public:
    SpaceMine(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void kill() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

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
