#pragma once

#include "Game/NameObj/NameObj.hpp"

class HitSensor;

class MarioMessenger : NameObj {
public:
    MarioMessenger(HitSensor* pSender);

    virtual ~MarioMessenger();
    virtual void movement();

    void addRequest(HitSensor* pReceiver, u32 msg);

private:
    HitSensor* mSender;
    HitSensor** mReceiverArray;
    u32* mMsgArray;
    u32 mArraySize;
};
