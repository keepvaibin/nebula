#include "Game/Ride/PlantLeaf.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/Model.hpp"
#include <nebula_geometry.hpp>

namespace {
    static const f32 sScaleMin = 0.0001f;
    static const f32 sDistancePush = 50.0f;
    static const f32 sPlayerPushRate = 0.02f;
    static const f32 sSpringAccelRate = 0.1f;
    static const f32 sSpringFrictionRate = 0.9f;
    static const f32 sSpringSpeedMax = 0.5f;
    static const f32 sSpringCoordMax = 0.9f;
    static const f32 sSpringCoordToStop = 0.01f;
    static const f32 sSpringSpeedToStop = 0.001f;
};  // namespace

PlantLeaf::PlantLeaf(f32 leafCoord, const Vec3f& rPosition, const Vec3f& rGrowDirection, f32 leafSize)
    : LiveActor("\227\164\201\151\220\114\202\321\220\101\225\250\201\152" /* 葉（伸び植物） */), mSpringCoord(), mSpringSpeed(), mLeafCoord(leafCoord), mLeafSize(leafSize), mSide(1.0f, 0.0f, 0.0f),
      mUp(0.0f, 1.0f, 0.0f), mFront(rGrowDirection) {
    mPosition.set(rPosition);
    MR::makeAxisFrontUp(&mSide, &mUp, mFront, mUp);
    mBaseMtx.setXYZDir(mSide, mUp, mFront);
    mBaseMtx.scale(mLeafSize * ::sScaleMin);
    mBaseMtx.setTrans(mPosition);
}

PlantLeafDrawInit::PlantLeafDrawInit(const char* pName) : LiveActor(pName), mMaterial(), mShape() {
    initModelManagerWithAnm("PlantLeaf", 0, false);
    ModelData* pModelData = MR::getModelData(this);
    mMaterial = MR::getMaterial(pModelData, 0);
    mShape = mMaterial->getMesh();
}

void PlantLeaf::init(const JMapInfoIter&) {
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

    if (MR::abs(mSpringCoord) < ::sSpringCoordToStop && MR::abs(mSpringSpeed) < ::sSpringSpeedToStop) {
        mSpringCoord = 0.0f;
        mSpringSpeed = 0.0f;
        MR::makeAxisFrontUp(&mSide, &mUp, mFront, mUp);
        mBaseMtx.setXYZDir(mSide, mUp, mFront);
        mBaseMtx.scale(mLeafSize * growthPercent);
        mBaseMtx.setTrans(mPosition);
        return true;
    } else {
        Vec3f side = mFront;
        Vec3f front;
        Vec3f up;
        side.y += mSpringCoord;
        MR::normalize(&side);
        MR::makeAxisFrontUp(&front, &up, side, mUp);
        mBaseMtx.setXYZDir(front, up, side);
        mBaseMtx.scale(mLeafSize * growthPercent);
        mBaseMtx.setTrans(mPosition);
        return false;
    }
}

void PlantLeafDrawInit::init(const JMapInfoIter& rIter) {
    MR::invalidateClipping(this);
    makeActorDead();
}
