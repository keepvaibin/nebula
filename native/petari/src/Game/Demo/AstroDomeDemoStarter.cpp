#include "Game/Demo/AstroDomeDemoStarter.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/SpinDriverPathDrawer.hpp"
#include "Game/MapObj/SpinDriverShootPath.hpp"
#include "Game/Util.hpp"
#include "math_types.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_startJumpOut_f1, "startJumpOut.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movePlayer_f1, "movePlayer.f1", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_movePlayer_f2, "movePlayer.f2", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpinDriverShoot_f1, "exeSpinDriverShoot.f1", 50.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpinDriverStart_setBckRate_1, "exeSpinDriverStart.setBckRate.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSpinDriverShoot_setBckRate_1, "exeSpinDriverShoot.setBckRate.1", 1.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeSpinDriverStart_1, "exeSpinDriverStart.1", 43);

namespace {
    const char* const cJumpOutPartName = "飛び出す";
    NEBULA_TUNABLE_RECORD(Vec, cAppearRotate, {90.0f, -5.0f, 0.0f});
    // const s32 cJumpOutStopStep = _;
    // const s32 cJumpOutStopFrame = _;
    // const f32 cJumpOutStartSpeed = _;
    NEBULA_TUNABLE(f32, cJumpOutRotateSpeed, -18.0f);
    NEBULA_TUNABLE(f32, cJumpOutNearClipDistance, 250.0f);
    NEBULA_TUNABLE(s32, cWhiteOutFrame, 146);
    NEBULA_TUNABLE(s32, cBgmStartFrame, 108);

    f32 countRotations(f32 angle) {
        s32 rotations = angle / TWO_PI;
        return TWO_PI * rotations;
    }
};  // namespace

namespace NrvAstroDomeDemoStarter {
    NEW_NERVE(AstroDomeDemoStarterNrvSpinDriverAppear, AstroDomeDemoStarter, SpinDriverAppear);
    NEW_NERVE(AstroDomeDemoStarterNrvSpinDriverStart, AstroDomeDemoStarter, SpinDriverStart);
    NEW_NERVE(AstroDomeDemoStarterNrvSpinDriverShoot, AstroDomeDemoStarter, SpinDriverShoot);
    NEW_NERVE(AstroDomeDemoStarterNrvJumpOut, AstroDomeDemoStarter, JumpOut);
    NEW_NERVE(AstroDomeDemoStarterNrvWhiteOut, AstroDomeDemoStarter, WhiteOut);
};  // namespace NrvAstroDomeDemoStarter

AstroDomeDemoStarter::AstroDomeDemoStarter(const char* pName) : LiveActor(pName), mShootPath(), mPathDrawer(), mCameraInfo() {
    mBaseMtx.identity();
    mPlayerDemoMtx.identity();
}

void AstroDomeDemoStarter::init(const TableIter& rIter) {
    initModelManagerWithAnm("SuperSpinDriver", nullptr, false);
    MR::hideModel(this);
    MR::connectToSceneNoSilhouettedMapObjStrongLight(this);

    initEffectKeeper(0, nullptr);
    initSound(6, false);

    MR::invalidateClipping(this);

    MR::initActorCamera(this, rIter, &mCameraInfo);
    MR::initAnimCamera(this, mCameraInfo, "AstroDomeDemoCamera", "AstroDomeDemoCamera");

    mShootPath = new SpinDriverShootPath();
    mShootPath->init(rIter);

    mPathDrawer = new SpinDriverPathDrawer(mShootPath);
    mPathDrawer->initWithoutIter();
    mPathDrawer->setColorNormal();

    MR::startBtpAndSetFrameAndStop(this, "SuperSpinDriver", 0.0f);
    MR::startBrk(this, "Yellow");

    initNerve(GET_NERVE(AstroDomeDemoStarter, AstroDomeDemoStarterNrvSpinDriverAppear));

    MR::tryRegisterDemoCast(this, rIter);
    MR::registerDemoActionNerve(this, GET_NERVE(AstroDomeDemoStarter, AstroDomeDemoStarterNrvSpinDriverStart), "スピンドライバ起動");
    MR::registerDemoActionNerve(this, GET_NERVE(AstroDomeDemoStarter, AstroDomeDemoStarterNrvJumpOut), "飛び出す");
    MR::registerDemoActionFunctor(this, MR::Functor(this, &AstroDomeDemoStarter::startJumpOut), nullptr);
    MR::needStageSwitchWriteA(this, rIter);

    makeActorDead();
}

void AstroDomeDemoStarter::startJumpOut() {
    MR::onSwitchA(this);
    mPosition.zero();
    mBaseMtx.identity();

    MR::startAnimCameraTargetSelf(this, mCameraInfo, "AstroDomeDemoCamera", 0, MR::getAnimCameraFrame(this, mCameraInfo, "AstroDomeDemoCamera") / cNum_startJumpOut_f1);
    MR::overlayWithPreviousScreen(2);
}

void AstroDomeDemoStarter::calcAndSetBaseMtx() {
}

void AstroDomeDemoStarter::movePlayer() {
    if (!MR::isDemoPartActive(::cJumpOutPartName)) {
        return;
    }

    f32 demoPartStepRate = MR::calcDemoPartStepRate(::cJumpOutPartName);

    Vec3f direction;

    mShootPath->calcDirection(&direction, demoPartStepRate, cNum_movePlayer_f1);

    f32 speed = ::cJumpOutRotateSpeed;
    f32 spinRate = ::countRotations(speed) + (-MR::pi() / cNum_movePlayer_f2);
    f32 angle = MR::getEaseOutValue(demoPartStepRate, 0.0f, 1.0f, 1.0f) * spinRate;

    Pos3f mtx;
    mtx.identity();
    mtx.setEulerY(angle);

    Vec3f position;

    mShootPath->calcPosition(&position, demoPartStepRate);

    Pos3f playerMtx;
    MR::makeMtxUpFrontPos(&playerMtx, direction, Vec3f(0.0f, -1.0f, 0.0f), position);

    playerMtx.concat(mtx);

    MR::setPlayerBaseMtx(playerMtx);

    if (MR::isJudgedToNearClip(position, ::cJumpOutNearClipDistance)) {
        MR::hidePlayer();
    } else {
        MR::showPlayer();
    }

    mPathDrawer->setCoord(demoPartStepRate);
    MR::startLevelSoundPlayer("SE_PM_LV_SPIN_DRV_FLY");
}

void AstroDomeDemoStarter::exeSpinDriverAppear() {
    if (MR::isFirstStep(this)) {
        MtxPtr playerDemobaseMtx = MR::getPlayerDemoActor()->getBaseMtx();
        mPlayerDemoMtx.set(playerDemobaseMtx);
        Pos3f rotateMtx;
        MR::makeMtxRotate(rotateMtx, Vec3f(::cAppearRotate));
        mPlayerDemoMtx.getTrans(mPosition);
        MR::makeMtxUpFrontPos(&mBaseMtx, MR::getCamYdir(), MR::getCamZdir(), mPosition);
        mBaseMtx.concat(rotateMtx);
        MR::setBaseTRMtx(this, mBaseMtx);
        MR::showModel(this);
        MR::startBck(this, "Appear");
        MR::startBckPlayer("SpinDriverWait", 20);
        MR::startSound(this, "SE_OJ_S_SPIN_DRV_APPEAR");
    }

    MR::startLevelSound(this, "SE_OJ_LV_S_SPIN_DRV_SHINE");
    MR::startLevelSound(this, "SE_OJ_LV_SPIN_DRV_CAPTURE");

    Pos3f slerpMtx;
    MR::blendMtxRotateSlerp(mPlayerDemoMtx, mBaseMtx, MR::calcDemoPartStepRate("スピンドライバ出現"), slerpMtx);
    slerpMtx.setTrans(mPosition);
    MR::setPlayerBaseMtx(slerpMtx);
}

void AstroDomeDemoStarter::exeSpinDriverStart() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Start");
        MR::startBckPlayer("SuperSpinDriverStart");
        MR::setPlayerBaseMtx(mBaseMtx);
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::startCSSound("CS_SPIN_DRIVE_LONG", "SE_SY_CS_S_SPIN_DRV_START", 0);
        MR::startSoundPlayer("SE_PM_SPIN_ATTACK");
    }

    if (MR::isLessStep(this, cStep_exeSpinDriverStart_1)) {
        MR::tryRumblePadMiddle(this, PLAYER_1);
    } else {
        MR::setBckRate(this, cNum_exeSpinDriverStart_setBckRate_1);
        MR::setBckRate(MR::getPlayerDemoActor(), 0.0f);
    }

    MR::setNerveAtStep(this, GET_NERVE(AstroDomeDemoStarter, AstroDomeDemoStarterNrvSpinDriverShoot), 58);
}

void AstroDomeDemoStarter::exeSpinDriverShoot() {
    if (MR::isFirstStep(this)) {
        MR::setBckRate(this, cNum_exeSpinDriverShoot_setBckRate_1);
        MR::startBckPlayer("SpaceFlyStart");
        MR::emitEffect(MR::getPlayerDemoActor(), "SuperSpinDriverFlyGlow");
        MR::shakeCameraNormal();
        MR::tryRumblePadVeryStrong(this, PLAYER_1);

        if (MR::isKoopaFortressAppearInGalaxy()) {
            MR::startSystemSE("SE_SY_ASTRO_DEPERTURE_ME_KP");
        } else {
            MR::startSystemSE("SE_SY_ASTRO_DEPERTURE_ME");
        }

        MR::startSound(this, "SE_OJ_S_SPIN_DRV_JUMP");
        MR::startSoundPlayer("SE_PV_JUMP_JOY");
    }

    Pos3f playerBaseMtx = mBaseMtx;
    Vec3f mult;
    mBaseMtx.mult(Vec3f(0.0f, getNerveStep() * cNum_exeSpinDriverShoot_f1, 0.0f), mult);

    playerBaseMtx.setTrans(mult);
    MR::setPlayerBaseMtx(playerBaseMtx);
}

void AstroDomeDemoStarter::exeJumpOut() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);
        MR::resetPlayerEffect();
        MR::startBckPlayer("SpaceFlyLoop");
        MR::emitEffect(MR::getPlayerDemoActor(), "SuperSpinDriverFlyGlow");
        mPathDrawer->appear();
    }

    movePlayer();

    if (MR::isStep(this, ::cBgmStartFrame)) {
        MR::startStageBGM("BGM_SENARIO_SEL", false);
    }

    if (MR::isStep(this, ::cWhiteOutFrame)) {
        setNerve(GET_NERVE(AstroDomeDemoStarter, AstroDomeDemoStarterNrvWhiteOut));
    }
}

void AstroDomeDemoStarter::exeWhiteOut() {
    if (MR::isFirstStep(this)) {
        MR::startSystemSE("SE_DM_SENARIO_SEL_START");
        MR::submitTrigSE();
        MR::closeSystemWipeWhiteFade();
    }

    movePlayer();
}
