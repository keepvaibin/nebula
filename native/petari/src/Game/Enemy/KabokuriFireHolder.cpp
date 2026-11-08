#include "Game/Enemy/KabokuriFireHolder.hpp"
#include "Game/Enemy/KabokuriFire.hpp"
#include "Game/Scene/SceneObjHolder.hpp"

namespace {
    const s32 sMaxKabokuriFire = 16;
};  // namespace

// "Kabokuri flame holder"
KabokuriFireHolder::KabokuriFireHolder() : DeriveActorGroup< KabokuriFire >("カボクリ炎管理", ::sMaxKabokuriFire) {
    for (s32 i = 0; i < ::sMaxKabokuriFire; i++) {
        KabokuriFire* const pFire = new KabokuriFire("カボクリ炎");  // "Kabokuri flame"
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

bool KabokuriFireHolderFunc::generateFire(const Vec3f& rPosition, const Vec3f& rGravity, bool setGravity) {
    KabokuriFire* const pFire = getDeadMember();

    if (pFire == nullptr) {
        return false;
    }

    if (setGravity) {
        pFire->mGravity.set(rGravity);
    }

    pFire->start(rPosition, setGravity);

    return true;
}
