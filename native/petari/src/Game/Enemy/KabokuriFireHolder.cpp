#include "Game/Enemy/KabokuriFireHolder.hpp"
#include "Game/Enemy/KabokuriFire.hpp"
#include "Game/Scene/SceneObjHolder.hpp"

namespace {
    static const s32 sMaxKabokuriFire = 16;
};  // namespace

// "Kabokuri flame holder"
KabokuriFireHolder::KabokuriFireHolder() : DeriveActorGroup< KabokuriFire >("\203\112\203\173\203\116\203\212\211\212\212\307\227\235" /* カボクリ炎管理 */, ::sMaxKabokuriFire) {
    KabokuriFire* pFire;

    for (int i = 0; i < ::sMaxKabokuriFire; i++) {
        pFire = new KabokuriFire("\203\112\203\173\203\116\203\212\211\212" /* カボクリ炎 */);  // "Kabokuri flame"
        pFire->initWithoutIter();
        registerActor(pFire);
    }
}

void KabokuriFireHolderFunc::createHolder() {
    MR::createSceneObj(SceneObj_KabokuriFireHolder);
}

KabokuriFire* KabokuriFireHolderFunc::getDeadMember() {
    return MR::getSceneObj< KabokuriFireHolder >(SceneObj_KabokuriFireHolder)->getDeadMember();
}

bool KabokuriFireHolderFunc::generateFire(const TVec3f& rPosition, const TVec3f& rGravity, bool setGravity) {
    KabokuriFire* deadMember = getDeadMember();

    if (deadMember == nullptr) {
        return false;
    }

    if (setGravity) {
        deadMember->mGravity.set(rGravity);
    }

    deadMember->start(rPosition, setGravity);

    return true;
}
