#include "Game/Ride/PlantLeaf.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/Model.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sScaleMin = 0.0001f;
    const f32 sDistancePush = 50.0f;
    const f32 sPlayerPushRate = 0.02f;
    const f32 sSpringAccelRate = 0.1f;
    const f32 sSpringFrictionRate = 0.9f;
    const f32 sSpringSpeedMax = 0.5f;
    const f32 sSpringCoordMax = 0.9f;
    const f32 sSpringCoordToStop = 0.01f;
    const f32 sSpringSpeedToStop = 0.001f;
};  // namespace

PlantLeaf::PlantLeaf(f32 leafCoord, const Vec3f& rPosition, const Vec3f& rGrowDirection, f32 leafSize)
    : LiveActor("葉（伸び植物）"), mSpringCoord(), mSpringSpeed(), mLeafCoord(leafCoord), mLeafSize(leafSize), mSide(1.0f, 0.0f, 0.0f),
      mUp(0.0f, 1.0f, 0.0f), mFront(rGrowDirection) {
    lumacore::setClass(this, lumacore::kClassId< PlantLeaf >);
    mPosition.set(rPosition);
    MR::makeAxisFrontUp(&mSide, &mUp, mFront, mUp);
    mBaseMtx.setXYZDir(mSide, mUp, mFront);
    mBaseMtx.scale(mLeafSize * ::sScaleMin);
    mBaseMtx.setTrans(mPosition);
}

PlantLeafDrawInit::PlantLeafDrawInit(const char* pName) : LiveActor(pName), mMaterial(), mShape() {
    lumacore::setClass(this, lumacore::kClassId< PlantLeafDrawInit >);
    initModelManagerWithAnm("PlantLeaf", 0, false);
    ModelData* pModelData = MR::getModelData(this);
    mMaterial = MR::getMaterial(pModelData, 0);
    mShape = mMaterial->getMesh();
}

void PlantLeaf::init(const TableIter&) {
    MR::invalidateClipping(this);
    makeActorAppeared();
}

void PlantLeaf::updateGrowUp(const Vec3f& rStalkPos, const Vec3f& rAxisY, f32 growthPercent, f32 offset) {
    mPosition.set(mFront);
    mPosition *= offset;
    mPosition.add(rStalkPos);

    f32 t = growthPercent * growthPercent;
    mUp.x = rAxisY.x * (1.0f - t);
    mUp.y = rAxisY.y * (1.0f - t) + t;
    mUp.z = rAxisY.z * (1.0f - t);

    MR::makeAxisFrontUp(&mSide, &mUp, mFront, mUp);
    mBaseMtx.setXYZDir(mSide, mUp, mFront);
    mBaseMtx.scale(mLeafSize * growthPercent);
    mBaseMtx.setTrans(mPosition);
}

bool PlantLeaf::updateSpring(const Vec3f& rPos, f32 springPower, f32 growthPercent) {
    if (mPosition.squared(rPos) < ::sDistancePush * ::sDistancePush) {
        if (mSpringCoord == 0.0f && mSpringSpeed == 0.0f) {
            MR::tryRumblePadWeak(this, PLAYER_1);
        }

        mSpringSpeed += springPower * ::sPlayerPushRate;
    }

    return updateSpring(growthPercent);
}

bool PlantLeaf::updateSpring(f32 growthPercent) {
    mSpringSpeed -= mSpringCoord * ::sSpringAccelRate;
    mSpringSpeed = MR::clamp(mSpringSpeed, -::sSpringSpeedMax, ::sSpringSpeedMax);

    mSpringCoord += mSpringSpeed;
    mSpringCoord = MR::clamp(mSpringCoord, -::sSpringCoordMax, ::sSpringCoordMax);

    mSpringSpeed *= ::sSpringFrictionRate;

    const bool stopped = MR::abs(mSpringCoord) < ::sSpringCoordToStop && MR::abs(mSpringSpeed) < ::sSpringSpeedToStop;
    if (stopped) {
        mSpringCoord = 0.0f;
        mSpringSpeed = 0.0f;
        MR::makeAxisFrontUp(&mSide, &mUp, mFront, mUp);
        mBaseMtx.setXYZDir(mSide, mUp, mFront);
    } else {
        // The leaf's front bends up by the spring.
        Vec3f bentFront = mFront;
        Vec3f side;
        Vec3f up;
        bentFront.y += mSpringCoord;
        MR::normalize(&bentFront);
        MR::makeAxisFrontUp(&side, &up, bentFront, mUp);
        mBaseMtx.setXYZDir(side, up, bentFront);
    }

    mBaseMtx.scale(mLeafSize * growthPercent);
    mBaseMtx.setTrans(mPosition);
    return stopped;
}

void PlantLeafDrawInit::init(const TableIter& /*rIter*/) {
    MR::invalidateClipping(this);
    makeActorDead();
}
