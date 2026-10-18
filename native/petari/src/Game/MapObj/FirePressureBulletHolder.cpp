#include "Game/MapObj/FirePressureBulletHolder.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/FirePressureBullet.hpp"
#include "Game/Util.hpp"
#include "Game/Util/LiveActorUtil.hpp"

FirePressureBulletHolder::FirePressureBulletHolder(const char* pName) : NameObj(pName) {
}

FirePressureBullet* FirePressureBulletHolder::callEmptyBullet() {
    for (u32 i = 0; i < ARRAY_SIZE(mBullet); i++) {
        if (MR::isDead(mBullet[i])) {
            return mBullet[i];
        }
    }

    return nullptr;
}

FirePressureBulletHolder::~FirePressureBulletHolder() {
}

void FirePressureBulletHolder::init(const JMapInfoIter& rIter) {
    for (u32 i = 0; i < ARRAY_SIZE(mBullet); i++) {
        mBullet[i] = new FirePressureBullet("\203\164\203\100\203\103\203\101\203\166\203\214\203\142\203\126\203\203\201\133\202\314\222\145" /* ファイアプレッシャーの弾 */);
        mBullet[i]->initWithoutIter();
    }
}
