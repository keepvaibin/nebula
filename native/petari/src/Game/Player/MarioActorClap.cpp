#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_bodyClap_f1, "bodyClap.f1", 100.0f);

void MarioActor::bodyClap() {
    for (u32 i = 0; i < mClapCoinCount; i++) {
        Vec3f coinPos(mClapCoins[i]->mPosition);
        Vec3f toCoin(coinPos - mCenterPos);
        if (toCoin.length() < cNum_bodyClap_f1) {
            mClapCoins[i]->receiveMessage(ACTMES_ITEM_GET, getSensor("body"));
        }
    }
}
