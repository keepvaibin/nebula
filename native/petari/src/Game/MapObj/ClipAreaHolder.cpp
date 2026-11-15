#include "Game/MapObj/ClipAreaHolder.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/ClipArea.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util.hpp"

namespace {
    const s32 sClipAreaMax = 64;
};  // namespace

namespace {
    ClipAreaHolder* getClipAreaHolder() {
        return MR::getSceneObj< ClipAreaHolder >(SceneObj_ClipAreaHolder);
    }
};  // namespace

ClipAreaHolder::ClipAreaHolder(const char* pName) : DeriveActorGroup< ClipArea >(pName, ::sClipAreaMax), mIsActive(true) {
}

void ClipAreaHolder::init(const TableIter& /*rIter*/) {
}

bool ClipAreaHolder::isInArea(const Vec3f& rPos, f32 radius) const {
    if (!mIsActive) {
        return false;
    }

    s32 memberNum = getObjNum();

    for (s32 i = 0; i < memberNum; i++) {
        if (!MR::isDead(getActor(i)) && getMember(i)->isInArea(rPos, radius)) {
            return true;
        }
    }

    return false;
}

ClipAreaCollisionFilter::ClipAreaCollisionFilter(const Vec3f* pClipCenter, f32 clipRadius) : mClipCenter(pClipCenter), mClipRadius(clipRadius) {
}

bool ClipAreaCollisionFilter::isInvalidParts(const CollisionParts* pParts) const {
    if (!MR::isExistClipAreaHolder()) {
        return false;
    }

    if (pParts->mHitSensor->isType(ATYPE_CLIP_FIELD_MAP_PARTS)) {
        if (!MR::isInClipArea(*mClipCenter, mClipRadius)) {
            return true;
        }
    }

    return false;
}

namespace MR {
    void createClipAreaHolder() {
        createSceneObj(SceneObj_ClipAreaHolder);
    }

    bool isExistClipAreaHolder() {
        return isExistSceneObj(SceneObj_ClipAreaHolder);
    }

    void addClipArea(ClipArea* pClipArea) {
        ::getClipAreaHolder()->registerActor(pClipArea);
    }

    bool isInClipArea(const Vec3f& rPos, f32 radius) {
        return ::getClipAreaHolder()->isInArea(rPos, radius);
    }

    bool isActiveClipArea() {
        return ::getClipAreaHolder()->mIsActive;
    }

    void activateClipArea() {
        ::getClipAreaHolder()->mIsActive = true;
    }

    void deactivateClipArea() {
        ::getClipAreaHolder()->mIsActive = false;
    }

    void setBinderExceptSensorType(LiveActor* actor, const Vec3f* pClipCenter, f32 clipRadius) {
        setBinderCollisionPartsFilter(actor, new ClipAreaCollisionFilter(pClipCenter, clipRadius));
    }
};  // namespace MR
