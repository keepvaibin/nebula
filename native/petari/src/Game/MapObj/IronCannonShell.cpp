#include "Game/MapObj/IronCannonShell.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 cShellBaseScale = 1.3f;
    const s32 cShellNum = 3;
    const s32 cDefaultShotInterval = NebulaFrameClock::stepsOfSeconds(5);
    const f32 cDefaultBulletSpeed = 30.0f;
    // Shells start this far along the front; the launcher model's muzzle is cGunPointModelOffset further.
    const f32 cGunPointOffset = 75.0f;
    const f32 cGunPointModelOffset = 100.0f;
    const s32 cGroupClippingNum = 32;
}  // namespace

namespace NrvIronCannonLauncherPoint {
    NEW_NERVE(IronCannonLauncherPointNrvWait, IronCannonLauncherPoint, Wait);
    NEW_NERVE(IronCannonLauncherPointNrvShot, IronCannonLauncherPoint, Shot);
}  // namespace NrvIronCannonLauncherPoint

namespace NrvIronCannonLauncher {
    NEW_NERVE(IronCannonLauncherNrvRelax, IronCannonLauncher, Relax);
    NEW_NERVE(IronCannonLauncherNrvWait, IronCannonLauncher, Wait);
    NEW_NERVE(IronCannonLauncherNrvShot, IronCannonLauncher, Shot);
}  // namespace NrvIronCannonLauncher

IronCannonShell::IronCannonShell(const char* pName) : KoopaJrShipCannonShell(pName) {
    lumacore::setClass(this, lumacore::kClassId< IronCannonShell >);
}

f32 IronCannonShell::getBaseScale() const {
    return ::cShellBaseScale;
}

IronCannonLauncherPoint::IronCannonLauncherPoint(const char* pName)
    : LiveActor(pName), mShellHolder(), mShotInterval(::cDefaultShotInterval), mBulletSpeed(::cDefaultBulletSpeed) {
    lumacore::setClass(this, lumacore::kClassId< IronCannonLauncherPoint >);
}

void IronCannonLauncherPoint::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelAndConnectToScene();
    initBullet();
    MR::getPlacementArg0NoInit(rIter, &mShotInterval);
    MR::getPlacementArg1NoInit(rIter, &mBulletSpeed);
    initEffectKeeper(0, "IronCannonLauncherPoint");

    const bool isWaitAppearSwitch = MR::useStageSwitchReadAppear(this, rIter);
    if (isWaitAppearSwitch) {
        MR::syncStageSwitchAppear(this);
    }

    MR::useStageSwitchReadA(this, rIter);
    MR::setGroupClipping(this, rIter, ::cGroupClippingNum);
    initNerve(GET_NERVE(IronCannonLauncherPoint, IronCannonLauncherPointNrvWait));

    if (isWaitAppearSwitch) {
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

// With switch A in use and off (outside the opening camera), the interval is held at its start.
void IronCannonLauncherPoint::exeWait() {
    if (!MR::isStageStateScenarioOpeningCamera() && MR::isValidSwitchA(this) && !MR::isOnSwitchA(this)) {
        setNerve(GET_NERVE(IronCannonLauncherPoint, IronCannonLauncherPointNrvWait));
        return;
    }

    if (MR::isGreaterStep(this, mShotInterval)) {
        setNerve(GET_NERVE(IronCannonLauncherPoint, IronCannonLauncherPointNrvShot));
    }
}

void IronCannonLauncherPoint::exeShot() {
    if (MR::isFirstStep(this)) {
        tryShotBullet(0.0f);
        setNerve(GET_NERVE(IronCannonLauncherPoint, IronCannonLauncherPointNrvWait));
    }
}

void IronCannonLauncherPoint::initModelAndConnectToScene() {
    MR::connectToSceneMapObjMovement(this);
}

void IronCannonLauncherPoint::initBullet() {
    mShellHolder = new CannonShellHolder(::cShellNum);

    for (s32 i = 0; i < ::cShellNum; i++) {
        IronCannonShell* pShell = new IronCannonShell("キャノン弾砲台の弾");
        pShell->initWithoutIter();
        pShell->makeActorDead();
        mShellHolder->registerCannonShell(pShell);
    }
}

// Fires along the launcher's front from cGunPointOffset + muzzleOffset ahead; the shadow drops along its down axis.
bool IronCannonLauncherPoint::tryShotBullet(f32 muzzleOffset) {
    IronCannonShell* pShell = static_cast< IronCannonShell* >(mShellHolder->getValidShell());

    if (pShell == nullptr) {
        return false;
    }

    Pos3f mtx;
    MR::makeMtxTRS(mtx, mPosition, mRotation, mScale);
    Vec3f front;
    mtx.getZDir(front);

    Vec3f startPos;
    startPos.scaleAdd(::cGunPointOffset + muzzleOffset, front, mPosition);
    pShell->launch(startPos, front * mBulletSpeed);

    Vec3f shadowDir;
    mtx.getYDir(shadowDir);
    shadowDir.negate();
    MR::setShadowDropDirection(pShell, nullptr, shadowDir);

    MR::emitEffect(this, "Shoot");
    return true;
}

IronCannonLauncher::IronCannonLauncher(const char* pName) : IronCannonLauncherPoint(pName), mMuzzlePos(0.0f, 0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< IronCannonLauncher >);
}

void IronCannonLauncher::init(const TableIter& rIter) {
    IronCannonLauncherPoint::init(rIter);

    MR::useStageSwitchReadA(this, rIter);
    MR::setClippingTypeSphereContainsModelBoundingBox(this, ::cGunPointModelOffset);
    Vec3f front;
    MR::calcFrontVec(&front, this);
    mMuzzlePos.scaleAdd(::cGunPointModelOffset, front, mPosition);
    MR::setEffectHostSRT(this, "Shoot", &mMuzzlePos, nullptr, nullptr);
    setNerve(GET_NERVE(IronCannonLauncher, IronCannonLauncherNrvWait));
}

void IronCannonLauncher::exeRelax() {
    if (MR::isOnSwitchA(this)) {
        setNerve(GET_NERVE(IronCannonLauncher, IronCannonLauncherNrvWait));
    }
}

void IronCannonLauncher::exeWait() {
    if (MR::isStep(this, mShotInterval)) {
        setNerve(GET_NERVE(IronCannonLauncher, IronCannonLauncherNrvShot));
    }
}

void IronCannonLauncher::exeShot() {
    if (MR::isFirstStep(this)) {
        MR::startAllAnim(this, "Shot");
        tryShotBullet(::cGunPointModelOffset);
        setNerve(GET_NERVE(IronCannonLauncher, IronCannonLauncherNrvWait));
    }
}

void IronCannonLauncher::initModelAndConnectToScene() {
    initModelManagerWithAnm("IronCannonLauncher", nullptr, false);
    MR::connectToSceneMapObj(this);
}
