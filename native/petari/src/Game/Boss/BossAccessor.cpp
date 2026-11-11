#include "Game/Boss/BossAccessor.hpp"
#include "Game/Boss/Koopa.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    BossAccessor* getBossAccessor() {
        return MR::getSceneObj< BossAccessor >(SceneObj_BossAccessor);
    }
};  // namespace

BossAccessor::BossAccessor() : NameObj("ボスへのアクセス"), mBoss() {
    lumacore::setClass(this, lumacore::kClassId< BossAccessor >);
}

namespace BossAccess {
    void setBossAccessorKoopa(Koopa* pKoopa) {
        MR::createSceneObj(SceneObj_BossAccessor);
        ::getBossAccessor()->setBoss(pKoopa);
    }

    Koopa* getBossAccessorKoopa() {
        if (MR::isExistSceneObj(SceneObj_BossAccessor)) {
            return static_cast< Koopa* >(::getBossAccessor()->getBoss());
        }

        return nullptr;
    }
};  // namespace BossAccess
