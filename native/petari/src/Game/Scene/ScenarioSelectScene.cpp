#include "Game/Scene/ScenarioSelectScene.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Camera/CameraContext.hpp"
#include "Game/Effect/EffectSystem.hpp"
#include "Game/Effect/EffectSystemUtil.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/CinemaFrame.hpp"
#include "Game/Screen/ScenarioSelectLayout.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/Render/Model.hpp"

namespace NrvScenarioSelectScene {
    NEW_NERVE(ScenarioSelectSceneNrvDeactive, ScenarioSelectScene, Deactive);
    NEW_NERVE(ScenarioSelectSceneNrvInvalidScenarioSelect, ScenarioSelectScene, InvalidScenarioSelect);
    NEW_NERVE(ScenarioSelectSceneNrvWaitStartScenarioSelect, ScenarioSelectScene, WaitStartScenarioSelect);
    NEW_NERVE(ScenarioSelectSceneNrvStartScenarioSelect, ScenarioSelectScene, StartScenarioSelect);
    NEW_NERVE(ScenarioSelectSceneNrvWaitScenarioSelect, ScenarioSelectScene, WaitScenarioSelect);
    NEW_NERVE(ScenarioSelectSceneNrvWaitSceneLoad, ScenarioSelectScene, WaitSceneLoad);
    NEW_NERVE(ScenarioSelectSceneNrvWaitInitializeEnd, ScenarioSelectScene, WaitInitializeEnd);
    NEW_NERVE(ScenarioSelectSceneNrvWaitDisappearLayout, ScenarioSelectScene, WaitDisappearLayout);
    NEW_NERVE(ScenarioSelectSceneNrvWaitSceneLoadIfRequestedReset, ScenarioSelectScene, WaitSceneLoadIfRequestedReset);
    NEW_NERVE(ScenarioSelectSceneNrvWaitSceneLoadIfCanceledSelect, ScenarioSelectScene, WaitSceneLoadIfCanceledSelect);
};  // namespace NrvScenarioSelectScene

namespace {
    ItemBuffer* createDrawBuffer() {
        ItemBuffer* buffer = new ItemBuffer();
        buffer->setNonSort();
        return buffer;
    }
};  // namespace

ScenarioSelectScene::ScenarioSelectScene()
    : Scene("シナリオ選択シーン"), mIsInitialized(), mIsBackground(), mIsCanceled(), mScenarioLayout(), mCinemaFrame(), mIsResetRequested(), mEffectSystem(), mCameraContext() {
}

void ScenarioSelectScene::init() {
    mDrawBufferOpa = ::createDrawBuffer();
    mDrawBufferXlu = ::createDrawBuffer();
    mEffectSystem = new EffectSystem("エフェクトシステム", false);
    mEffectSystem->initWithoutIter();
    mEffectSystem->entry(MR::getParticleResourceHolder(), 0x20);
    mCameraContext = new CameraContext();

    Pos3f view;
    view.setPositionFromLookAt(Vec3f(0.0f, 0.0f, 1000.0f), Vec3f(0.0f, 1.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));
    mCameraContext->setViewMtx(view);

    mScenarioLayout = new ScenarioSelectLayout(mEffectSystem, mCameraContext);
    mScenarioLayout->initWithoutIter();
    mCinemaFrame = new CinemaFrame(false);
    mCinemaFrame->initWithoutIter();
    initNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvDeactive));
    mIsInitialized = true;
}

void ScenarioSelectScene::start() {
    mEffectSystem->startScene();
    mIsBackground = false;
    mIsCanceled = false;
    setNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvInvalidScenarioSelect));
}

void ScenarioSelectScene::startBackground() {
    start();
    mIsBackground = true;
}

void ScenarioSelectScene::update() {
    if (!mIsInitialized) {
        return;
    }

    updateNerve();

    if (isRunning()) {
        mScenarioLayout->movement();
        mCinemaFrame->movement();
        MR::Effect::checkEffectSceneUpdate(mEffectSystem);
    }
}

void ScenarioSelectScene::calcAnim() {
    if (isRunning()) {
        // The scene builds its perspective once, at boot: follow the picture's aspect as it changes.
        mCameraContext->updateProjectionMtx();
        mScenarioLayout->calcAnim();
        mCinemaFrame->calcAnim();
        MR::Effect::movementEffectNormal(mEffectSystem);
        calcViewAndEntry();
    }
}

void ScenarioSelectScene::calcViewAndEntry() {
    if (isRunning()) {
        setupCameraMtx();
        ModelSys::mDrawBuffer[0] = mDrawBufferOpa;
        ModelSys::mDrawBuffer[1] = mDrawBufferXlu;
        mScenarioLayout->calcViewAndEntryStarModel();
    }
}

void ScenarioSelectScene::draw() const {
    if (isRunning()) {
        setupCameraMtx();
        mDrawBufferOpa->draw();
        mDrawBufferOpa->frameInit();
        mDrawBufferXlu->draw();
        mDrawBufferXlu->frameInit();
        MR::Effect::drawEffect3D(mEffectSystem, mCameraContext->getViewMtx());
        mScenarioLayout->draw();
        MR::Effect::drawEffect2D(mEffectSystem);
        mCinemaFrame->draw();
    }
}

bool ScenarioSelectScene::isActive() const {
    return mIsInitialized && !isNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvDeactive));
}

bool ScenarioSelectScene::isExecForeground() const {
    return isActive() && !mIsBackground;
}

bool ScenarioSelectScene::isScenarioSelecting() const {
    return isExecForeground() && !isNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvWaitDisappearLayout));
}

void ScenarioSelectScene::validateScenarioSelect() {
    if (!mIsResetRequested) {
        if (mIsBackground) {
            setNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvWaitSceneLoad));
            return;
        }

        setNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvWaitStartScenarioSelect));
    } else {
        setNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvWaitSceneLoadIfRequestedReset));
    }
}

void ScenarioSelectScene::requestReset(bool waitForInitialize) {
    if (!mIsInitialized) {
        mIsResetRequested = false;
        return;
    }

    mIsResetRequested = true;
    suspend();
    if (isNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvDeactive)) ||
        isNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvWaitDisappearLayout)) ||
        isNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvWaitInitializeEnd))) {
        mIsResetRequested = false;
        setNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvDeactive));
    } else if (isNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvInvalidScenarioSelect))) {
        if (!waitForInitialize) {
            mIsResetRequested = false;
            setNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvDeactive));
        }
    } else {
        setNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvWaitSceneLoadIfRequestedReset));
    }
}

bool ScenarioSelectScene::isResetEnd() const {
    return !mIsResetRequested;
}

void ScenarioSelectScene::setupCameraMtx() const {
    NebulaMtxCopy(mCameraContext->getViewMtx(), ModelSys::mViewMtx);
    Frame::setPerspective(mCameraContext->mProjection);
}

bool ScenarioSelectScene::trySetCurrentScenarioNo() const {
    if (mScenarioLayout->isDecided()) {
        if (!MR::isScenarioDecided()) {
            s32 scenarioNo = mScenarioLayout->getSelectedScenarioNo();
            s32 placedNo = scenarioNo;
            GalaxyStatusAccessor accessor = MR::makeCurrentGalaxyStatusAccessor();
            if (accessor.isHiddenStar(scenarioNo)) {
                placedNo = MR::getPlacedHiddenStarScenarioNo(MR::getCurrentStageName(), scenarioNo);
            }

            MR::setCurrentScenarioNo(placedNo, scenarioNo);
        }

        return true;
    }

    return false;
}

void ScenarioSelectScene::suspend() {
    mIsCanceled = false;

    if (!MR::isDead(mScenarioLayout)) {
        mScenarioLayout->kill();
    }

    if (!MR::isDead(mCinemaFrame)) {
        mCinemaFrame->kill();
    }

    mDrawBufferOpa->frameInit();
    mDrawBufferXlu->frameInit();
    MR::Effect::forceDeleteAllEmitters(mEffectSystem);
}

void ScenarioSelectScene::tryStartScreenToFrame() {
    if (!mIsBackground && mScenarioLayout->isEndAnimStartStep()) {
        mCinemaFrame->tryScreenToFrame();
    }
}

void ScenarioSelectScene::exeDeactive() {
    if (MR::isFirstStep(this)) {
        if (!MR::isDead(mCinemaFrame)) {
            mCinemaFrame->kill();
        }
    }
}

void ScenarioSelectScene::exeInvalidScenarioSelect() {
}

void ScenarioSelectScene::exeStartScenarioSelect() {
    if (MR::isFirstStep(this)) {
        bool isSpecificStage = MR::isStageKoopaVs3() || MR::isEqualStageName("HeavensDoorGalaxy");

        if (isSpecificStage) {
            MR::openSystemWipeFade();
        } else {
            MR::openSystemWipeWhiteFade();
        }

        mCinemaFrame->forceToFrame();
        mScenarioLayout->appear();
    }

    if (mScenarioLayout->isAppearStarEnd()) {
        setNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvWaitScenarioSelect));
    }
}

void ScenarioSelectScene::exeWaitStartScenarioSelect() {
    if (!MR::isSystemWipeActive()) {
        setNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvStartScenarioSelect));
    }
}

void ScenarioSelectScene::exeWaitScenarioSelect() {
    if (MR::isFirstStep(this)) {
        mCinemaFrame->tryFrameToScreen();
        MR::startStarPointerModeScenarioSelectScene(this);
    }

    if (trySetCurrentScenarioNo()) {
        MR::endStarPointerMode(this);
        setNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvWaitSceneLoad));
    } else if (mScenarioLayout->isCanceled()) {
        MR::endStarPointerMode(this);
        setNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvWaitSceneLoadIfCanceledSelect));
    }
}

void ScenarioSelectScene::exeWaitSceneLoad() {
    tryStartScreenToFrame();

    if (MR::tryResumeSceneInitialize()) {
        setNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvWaitInitializeEnd));
    }
}

void ScenarioSelectScene::exeWaitInitializeEnd() {
    tryStartScreenToFrame();

    if (MR::isInitializeStateEnd()) {
        if (MR::isDead(mScenarioLayout)) {
            MR::Effect::forceDeleteAllEmitters(mEffectSystem);
            setNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvDeactive));
        } else if (mScenarioLayout->isReadyToDisappear()) {
            setNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvWaitDisappearLayout));
        }
    }
}

void ScenarioSelectScene::exeWaitDisappearLayout() {
    if (MR::isFirstStep(this)) {
        mCinemaFrame->forceToScreen();
        mScenarioLayout->disappear();
    }

    if (MR::isDead(mScenarioLayout)) {
        MR::Effect::forceDeleteAllEmitters(mEffectSystem);
        setNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvDeactive));
    }
}

void ScenarioSelectScene::exeWaitSceneLoadIfRequestedReset() {
    if (MR::isFirstStep(this)) {
        GameSystemFunction::resetCurrentScenarioNo();
    }

    if (MR::tryResumeSceneInitialize()) {
        mIsResetRequested = false;
        setNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvDeactive));
    }
}

void ScenarioSelectScene::exeWaitSceneLoadIfCanceledSelect() {
    if (MR::isDead(mScenarioLayout)) {
        if (MR::tryResumeSceneInitialize()) {
            GameSystemFunction::resetCurrentScenarioNo();
            GameSequenceFunction::requestCancelScenarioSelect();
            suspend();
            mIsCanceled = true;
            MR::Effect::forceDeleteAllEmitters(mEffectSystem);
            setNerve(GET_NERVE(ScenarioSelectScene, ScenarioSelectSceneNrvDeactive));
        }
    }
}
