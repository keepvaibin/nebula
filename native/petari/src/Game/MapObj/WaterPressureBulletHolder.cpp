#include "Game/MapObj/WaterPressureBulletHolder.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/WaterPressureBullet.hpp"
#include "Game/Util.hpp"
#include "Game/Util/LiveActorUtil.hpp"

WaterPressureBulletHolder::WaterPressureBulletHolder(const char* pName) : NameObj(pName) {
}

WaterPressureBullet* WaterPressureBulletHolder::callEmptyBullet() {
    for (u32 i = 0; i < ARRAY_SIZE(mBullet); i++) {
        if (MR::isDead(mBullet[i])) {
            return mBullet[i];
        }
    }

    return nullptr;
}

WaterPressureBulletHolder::~WaterPressureBulletHolder() {
}

void WaterPressureBulletHolder::init(const JMapInfoIter& rIter) {
    for (u32 i = 0; i < ARRAY_SIZE(mBullet); i++) {
        mBullet[i] = new WaterPressureBullet("\203\105\203\110\201\133\203\136\201\133\203\166\203\214\203\142\203\126\203\203\201\133\202\314\222\145" /* ウォータープレッシャーの弾 */);
        mBullet[i]->initWithoutIter();
    }
}