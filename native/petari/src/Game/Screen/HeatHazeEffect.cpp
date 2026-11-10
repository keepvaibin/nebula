#include "Game/Screen/HeatHazeEffect.hpp"
#include "Game/AreaObj/HazeCube.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f2, "control.f2", 1000.0f);

HeatHazeEffect::HeatHazeEffect(const char* pName) : LiveActor(pName), mHazeDistance(1500.0f) {
}

void HeatHazeEffect::appear() {
    LiveActor::appear();
    MR::startBtk(this, "ShimmerBoard");
}

void HeatHazeEffect::control() {
    const Pos3f& rCameraMtx = MR::getCameraInvViewMtx();
    Vec3f direction;
    rCameraMtx.getZDir(direction);
    direction.negate();
    direction.mult(mHazeDistance);
    rCameraMtx.getTrans(mPosition);
    mPosition.add(direction);
    rCameraMtx.getEuler(mRotation);
    mRotation.mult(cNum_control_f1 / PI);
    mScale.setAll< f32 >(mHazeDistance / cNum_control_f2);
}

HeatHazeDirector::HeatHazeDirector(const char* pName) : NameObj(pName) {
}

void HeatHazeEffect::init(const TableIter& /*rIter*/) {
    initModelManagerWithAnm("ShimmerBoard", nullptr, false);
    MR::connectToSceneIndirectMapObj(this);
    MR::invalidateClipping(this);
    makeActorDead();
}

void HeatHazeDirector::init(const TableIter& /*rIter*/) {
    mEffect = new HeatHazeEffect("陽炎");
    mEffect->initWithoutIter();

    MR::connectToSceneMapObjMovement(this);
}

void HeatHazeDirector::movement() {
    HazeCube* pHazeCube = static_cast< HazeCube* >(MR::getAreaObj("HazeCube", *MR::getPlayerPos()));

    if (pHazeCube != nullptr) {
        if (MR::isDead(mEffect)) {
            mEffect->mHazeDistance = pHazeCube->mDistance;
            mEffect->appear();
        }
    } else if (!MR::isDead(mEffect)) {
        mEffect->kill();
    }
}
