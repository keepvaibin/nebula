#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util/ActorSensorUtil.hpp"

void MarioActor::bodyClap() {
    for (u32 i = 0; i < mClapCoinCount; i++) {
        Vec3f stack_14(mClapCoins[i]->mPosition);
        Vec3f stack_8(stack_14 - mCenterPos);
        if (stack_8.length() < 100.0f) {
            mClapCoins[i]->receiveMessage(ACTMES_ITEM_GET, getSensor("body"));
        }
    }
}
