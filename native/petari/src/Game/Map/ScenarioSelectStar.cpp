#include "Game/Map/ScenarioSelectStar.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/PowerStar.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_setup_f1, "setup.f1", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePos_f1, "updatePos.f1", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePos_f2, "updatePos.f2", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePointing_f1, "exePointing.f1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePointing_f2, "exePointing.f2", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeNotSelected_f1, "exeNotSelected.f1", 150.0f);

namespace {
    static const s32 cAppearWaitFrame = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(s32, cAppearDelayFrame, 15);
    NEBULA_TUNABLE(s32, cHideDelayFrame, 10);
    NEBULA_TUNABLE(f32, cNotPointingRotateSpeedZ, 1.5f);
    NEBULA_TUNABLE(f32, cPointingRotateSpeedZ, 3.0f);
    NEBULA_TUNABLE(f32, cPointingScale, 1.5f);
    NEBULA_TUNABLE(f32, cNotPointingScale, 0.9f);
    static const s32 cMoveInterval = NebulaFrameClock::stepsOfSeconds(3);
    NEBULA_TUNABLE(f32, cMoveDistance, 15.0f);
    NEBULA_TUNABLE(s32, cAppearFrame, 60);
    NEBULA_TUNABLE(s32, cStartPointingFrame, 20);
    NEBULA_TUNABLE(s32, cEndPointingFrame, 10);
    NEBULA_TUNABLE(s32, cSelectedWaitFrame, 40);
    NEBULA_TUNABLE(s32, cSelectedMoveFrame, 60);
    NEBULA_TUNABLE(f32, cSelectedRotateSpeedZ, 20.0f);
    NEBULA_TUNABLE(f32, cSelectedRotateMoveRate, 6.0f);
    NEBULA_TUNABLE(s32, cSelectedRotateAccelFrame, 180);
};  // namespace

namespace NrvScenarioSelectStar {
    NEW_NERVE(ScenarioSelectStarNrvAppear, ScenarioSelectStar, Appear);
    NEW_NERVE(ScenarioSelectStarNrvNotPointing, ScenarioSelectStar, NotPointing);
    NEW_NERVE(ScenarioSelectStarNrvPointing, ScenarioSelectStar, Pointing);
    NEW_NERVE(ScenarioSelectStarNrvEndPointing, ScenarioSelectStar, EndPointing);
    NEW_NERVE(ScenarioSelectStarNrvSelected, ScenarioSelectStar, Selected);
    NEW_NERVE(ScenarioSelectStarNrvSelectedMove, ScenarioSelectStar, SelectedMove);
    NEW_NERVE(ScenarioSelectStarNrvNotSelected, ScenarioSelectStar, NotSelected);
};  // namespace NrvScenarioSelectStar

ScenarioSelectStar::ScenarioSelectStar(EffectSystem* pSystem)
    : MultiSceneActor("シナリオ選択のスター", "PowerStar", false), mStarCollectedStatus(0), mScenarioNo(1), mStarId(0), mIsPointing(false),
      mRotateSpeed(0.0f), mBasePos(gZeroVec), mTranslationOnSelect(gZeroVec), mAppearFrame(::cAppearFrame), mScaleOnSelect(1.0f) {
    lumacore::setClass(this, lumacore::kClassId< ScenarioSelectStar >);
    initEffect(pSystem, 0, "ScenarioStar");
}

void ScenarioSelectStar::init(const TableIter& /*rIter*/) {
    initNerve(GET_NERVE(ScenarioSelectStar, ScenarioSelectStarNrvNotPointing));
}

void ScenarioSelectStar::calcViewAndEntry() {
    if (!isNerve(GET_NERVE(ScenarioSelectStar, ScenarioSelectStarNrvAppear)) || !MultiScene::isLessEqualStep(this, mAppearFrame + 1)) {
        MultiSceneActor::calcViewAndEntry();
    }
}

void ScenarioSelectStar::appear() {
    mScale.set(::cNotPointingScale, ::cNotPointingScale, ::cNotPointingScale);
    mIsPointing = false;
    mRotateSpeed = 0.0f;
    updatePos();
    MultiScene::startBtp(this, "PowerStar");
    MultiScene::startBva(this, "PowerStar");
    MultiScene::setBtpFrameAndStop(this, PowerStar::getBtpFrameCurrentStage(mScenarioNo));
    MultiScene::setBvaFrameAndStop(this, mStarCollectedStatus == 0 ? 1.0f : 0.0f);
    MultiSceneActor::appear();
    setNerve(GET_NERVE(ScenarioSelectStar, ScenarioSelectStarNrvAppear));
}

void ScenarioSelectStar::kill() {
    MultiScene::forceDeleteEffectAll(this);
    MultiSceneActor::kill();
}

void ScenarioSelectStar::tryPointing() {
    mIsPointing = true;

    if (isNerve(GET_NERVE(ScenarioSelectStar, ScenarioSelectStarNrvNotPointing)) ||
        isNerve(GET_NERVE(ScenarioSelectStar, ScenarioSelectStarNrvEndPointing))) {
        setNerve(GET_NERVE(ScenarioSelectStar, ScenarioSelectStarNrvPointing));
    }
}

void ScenarioSelectStar::select() {
    setNerve(GET_NERVE(ScenarioSelectStar, ScenarioSelectStarNrvSelected));
}

void ScenarioSelectStar::notSelect() {
    setNerve(GET_NERVE(ScenarioSelectStar, ScenarioSelectStarNrvNotSelected));
}

bool ScenarioSelectStar::isAppearEnd() const {
    return !mIsDead && !isNerve(GET_NERVE(ScenarioSelectStar, ScenarioSelectStarNrvAppear));
}

void ScenarioSelectStar::setup(s32 scenarioNo, int starCollectedStatus, const Vec3f& rPos, s32 starId) {
    mStarCollectedStatus = starCollectedStatus;
    mScenarioNo = scenarioNo;
    mBasePos.set(rPos);
    mStarId = starId;

    mRotation.set< f32 >(0.0f, 0.0f, cNum_setup_f1 * mStarId);
    mAppearFrame = mStarId * ::cAppearDelayFrame + ::cAppearWaitFrame;
    mRotateTime = mStarId * 15 + (mStarId % 2 ? 0 : 90);
}

void ScenarioSelectStar::control() {
    mIsPointing = false;
    mRotation.z = MR::repeatDegree(mRotation.z + mRotateSpeed);

    if (isNerve(GET_NERVE(ScenarioSelectStar, ScenarioSelectStarNrvNotPointing)) ||
        isNerve(GET_NERVE(ScenarioSelectStar, ScenarioSelectStarNrvPointing)) ||
        isNerve(GET_NERVE(ScenarioSelectStar, ScenarioSelectStarNrvEndPointing))) {
        mRotateTime++;
        updatePos();
    }
}

bool ScenarioSelectStar::tryEndPointing() {
    if (!mIsPointing) {
        setNerve(GET_NERVE(ScenarioSelectStar, ScenarioSelectStarNrvEndPointing));
        return true;
    }

    return false;
}

void ScenarioSelectStar::updatePos() {
    Vec3f offset(0.0f, MR::sinDegree(MR::repeat((mRotateTime * cNum_updatePos_f1) / ::cMoveInterval, 0.0f, cNum_updatePos_f2)) * ::cMoveDistance, 0.0f);
    mTranslation.add(mBasePos, offset);
}

void ScenarioSelectStar::exeAppear() {
    if (MultiScene::isStep(this, mAppearFrame)) {
        MultiScene::startBck(this, "ScenarioAppear");
        MR::startSystemSE("SE_OJ_SCEN_SEL_STAR_APPEAR");
    }

    if (MultiScene::isStep(this, mAppearFrame + 1)) {
        if (mStarCollectedStatus == 1) {
            MultiScene::emitEffect(this, "ClearStarLight");
        } else {
            MultiScene::emitEffect(this, "Light");
        }
    }

    if (MultiScene::isGreaterStep(this, mAppearFrame)) {
        MultiScene::setNerveAtBckStopped(this, GET_NERVE(ScenarioSelectStar, ScenarioSelectStarNrvNotPointing));
    }
}

void ScenarioSelectStar::exeNotPointing() {
    if (MultiScene::isFirstStep(this)) {
        mScale.x = ::cNotPointingScale;
        mScale.y = ::cNotPointingScale;
        mScale.z = ::cNotPointingScale;
        mRotateSpeed = ::cNotPointingRotateSpeedZ;
    }
}

void ScenarioSelectStar::exePointing() {
    if (MultiScene::isFirstStep(this)) {
        MR::startSystemSE("SE_SY_CURSOR_1");
        MR::tryRumblePadWeak(this, PLAYER_1);
        mRotateSpeed = ::cPointingRotateSpeedZ;
    }

    if (MultiScene::isLessStep(this, ::cStartPointingFrame)) {
        f32 t = MultiScene::calcNerveRate(this, ::cStartPointingFrame);
        f32 reaction = MR::getScaleWithReactionValueZeroToOne(t, cNum_exePointing_f1, -cNum_exePointing_f2);
        f32 linerVal = MR::getLinerValue(reaction, ::cNotPointingScale, ::cPointingScale, 1.0f);
        mScale.setAll< f32 >(linerVal);
    } else {
        mScale.x = ::cPointingScale;
        mScale.y = ::cPointingScale;
        mScale.z = ::cPointingScale;
    }

    if (tryEndPointing()) {
        return;
    }
}

void ScenarioSelectStar::exeEndPointing() {
    if (MultiScene::isFirstStep(this)) {
        mRotateSpeed = ::cNotPointingRotateSpeedZ;
    }

    mScale.setAll< f32 >(MR::getEaseOutValue(MultiScene::calcNerveRate(this, ::cEndPointingFrame), ::cPointingScale, ::cNotPointingScale, 1.0f));
    MultiScene::setNerveAtStep(this, GET_NERVE(ScenarioSelectStar, ScenarioSelectStarNrvNotPointing), ::cEndPointingFrame);
}

void ScenarioSelectStar::exeSelected() {
    if (MultiScene::isFirstStep(this)) {
        MultiScene::emitEffect(this, "Select");
        MR::tryRumblePadMiddle(this, PLAYER_1);
        mTranslationOnSelect.set(mTranslation);
        mRotateSpeed = ::cPointingRotateSpeedZ;
        mScaleOnSelect = mScale.x;
    }

    mScale.setAll< f32 >(MultiScene::calcNerveValue(this, ::cSelectedWaitFrame, mScaleOnSelect, ::cPointingScale));
    MultiScene::setNerveAtStep(this, GET_NERVE(ScenarioSelectStar, ScenarioSelectStarNrvSelectedMove), ::cSelectedWaitFrame);
}

void ScenarioSelectStar::exeSelectedMove() {
    if (MultiScene::isFirstStep(this)) {
        mScale.x = ::cPointingScale;
        mScale.y = ::cPointingScale;
        mScale.z = ::cPointingScale;
    }

    if (MultiScene::isStep(this, ::cSelectedMoveFrame)) {
        MR::startSystemSE("SE_DM_SENARIO_SEL_ACCEL");
    }

    if (MultiScene::isLessStep(this, ::cSelectedMoveFrame)) {
        f32 t = MultiScene::calcNerveEaseOutRate(this, ::cSelectedMoveFrame);
        mTranslation.scale(1.0f - t, mTranslationOnSelect);
        mRotateSpeed = MR::getLinerValue(t, ::cPointingRotateSpeedZ, ::cSelectedRotateMoveRate, 1.0f);
    } else {
        mTranslation.zero();
        mRotateSpeed = MR::getLinerValue(MultiScene::calcNerveRate(this, ::cSelectedMoveFrame, ::cSelectedRotateAccelFrame),
                                         ::cSelectedRotateMoveRate, ::cSelectedRotateSpeedZ, 1.0f);
    }
}

void ScenarioSelectStar::exeNotSelected() {
    s32 hideDelay = ::cHideDelayFrame * mStarId;

    if (MultiScene::isFirstStep(this)) {
        mRotateSpeed = ::cNotPointingRotateSpeedZ;
    }

    if (MultiScene::isStep(this, hideDelay)) {
        mRotateSpeed = 0.0f;
        mRotation.z = cNum_exeNotSelected_f1 * mStarId;
        MultiScene::startBck(this, "ScenarioHide");
        MultiScene::deleteEffectAll(this);
    }

    if (MultiScene::isGreaterStep(this, hideDelay)) {
        if (MultiScene::isBckStopped(this)) {
            kill();
        }
    }
}
