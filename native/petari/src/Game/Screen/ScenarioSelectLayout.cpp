#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Screen/ScenarioSelectLayout.hpp"
#include "Game/System/NebulaRegion.hpp"
#include "Game/Camera/CameraContext.hpp"
#include "Game/Effect/MultiEmitter.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/RaceManager.hpp"
#include "Game/Map/ScenarioSelectStar.hpp"
#include "Game/Screen/BackButton.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/GameDataConst.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_appear_f1, "appear.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWorldPositionFromScreenPos_f1, "calcWorldPositionFromScreenPos.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWorldPositionFromScreenPos_f2, "calcWorldPositionFromScreenPos.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWorldPositionFromScreenPos_f3, "calcWorldPositionFromScreenPos.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWorldPositionFromScreenPos_f4, "calcWorldPositionFromScreenPos.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWorldPositionFromScreenPos_f5, "calcWorldPositionFromScreenPos.f5", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryChangePage_f1, "tryChangePage.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryChangePage_f2, "tryChangePage.f2", 0.15f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryChangePage_f3, "tryChangePage.f3", 0.85f);
NEBULA_TUNABLE_NAMED(f32, cNum_appearStar_f1, "appearStar.f1", 1000.0f);

namespace {
    const char* const cStarPaneName[] = {"Star1", "Star2", "Star3", "Star4", "Star5", "Star6", "Star7"};
    const char* const cQuestionPaneName[] = {
        "NewStar1",
        "NewStar2",
    };
    NEBULA_TUNABLE_RECORD(Vec, cSkyTrans, {0.0f, 0.0f, -10000.0f});
    NEBULA_TUNABLE(f32, cPointingTargetRadius, 60.0f);
    NEBULA_TUNABLE(s32, cDecideFrame, 40);
    const s32 cSelectedAccelStartStep = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(s32, cSelectedAccelFrame, 150);
    NEBULA_TUNABLE(f32, cSelectedEffectRateMin, 2.0f);
    NEBULA_TUNABLE(f32, cSelectedEffectRateMax, 15.0f);
    NEBULA_TUNABLE(f32, cSelectedEffectSpeedMin, 9.0f);
    NEBULA_TUNABLE(f32, cSelectedEffectSpeedMax, 15.0f);
    const s32 cSelectedEndAnimStartStep = NebulaFrameClock::stepsOfSeconds(1);
    const s32 cSelectedFadeOutStartStep = NebulaFrameClock::stepsOfSeconds(3);
    NEBULA_TUNABLE(s32, cSelectedFadeOutFrame, 30);
    NEBULA_TUNABLE(s32, cSelectedFadeInFrame, 180);
    NEBULA_TUNABLE(s32, cCancelFadeOutFrame, 90);
    NEBULA_TUNABLE(f32, cSkyScale, 10.0f);
    NEBULA_TUNABLE(s32, cCometWarningWaitFrame, 15);
    NEBULA_TUNABLE(s32, cCometWarningFrame, 180);
};  // namespace

namespace {
    inline s32 getHiddenScenarioNum() {
        s32 normalNum = MR::getCurrentStageNormalScenarioNum();
        return MR::getCurrentStagePowerStarNum() - normalNum;
    }

    bool isCurrentStageRaceComplete(s32 scenarioNo) {
        const char* stageName = MR::getCurrentStageName();

        return MR::isOnGameEventFlagPowerStarSuccess(stageName, scenarioNo) && RaceManagerFunction::getRaceId(stageName, scenarioNo) >= 0;
    }
};  // namespace

namespace NrvScenarioSelectLayout {
    NEW_NERVE(ScenarioSelectLayoutNrvAppearStar, ScenarioSelectLayout, AppearStar);
    NEW_NERVE(ScenarioSelectLayoutNrvAppear, ScenarioSelectLayout, Appear);
    NEW_NERVE(ScenarioSelectLayoutNrvWaitScenarioSelect, ScenarioSelectLayout, WaitScenarioSelect);
    NEW_NERVE(ScenarioSelectLayoutNrvDecide, ScenarioSelectLayout, Decide);
    NEW_NERVE(ScenarioSelectLayoutNrvAfterScenarioSelected, ScenarioSelectLayout, AfterScenarioSelected);
    NEW_NERVE(ScenarioSelectLayoutNrvDisappear, ScenarioSelectLayout, Disappear);
    NEW_NERVE(ScenarioSelectLayoutNrvCancel, ScenarioSelectLayout, Cancel);
    NEW_NERVE(ScenarioSelectLayoutNrvCancelFadeOut, ScenarioSelectLayout, CancelFadeOut);
    NEW_NERVE(ScenarioSelectLayoutNrvAppearCometWarning, ScenarioSelectLayout, AppearCometWarning);
    NEW_NERVE(ScenarioSelectLayoutNrvWaitCometWarning, ScenarioSelectLayout, WaitCometWarning);
    NEW_NERVE(ScenarioSelectLayoutNrvDisappearCometWarning, ScenarioSelectLayout, DisappearCometWarning);
};  // namespace NrvScenarioSelectLayout

ScenarioSelectLayout::ScenarioSelectLayout(EffectSystem* pEffectSystem, const CameraContext* pCameraContext)
    : LayoutActor("シナリオ選択レイアウト", true), mSelectedScenarioNo(-1), mIsDecided(), mStar(), mScenarioSky(), mEffectSystem(pEffectSystem),
      mCameraContext(pCameraContext), mNewTextFollowPos(0.0f, 0.0f), mNewGreenTextFollowPos(0.0f, 0.0f), mStarTopFollowPos(0.0f, 0.0f), mBackButton(),
      mMarioPaneFollowPos(0.0f, 0.0f), mMarioPaneName(), mTextFadeState(), mTextScenarioNo(-1) {
    lumacore::setClass(this, lumacore::kClassId< ScenarioSelectLayout >);
    mEffectHostMtx.identity();

    for (u32 i = 0; i < ARRAY_SIZE(mQuestionPane); i++) {
        mQuestionPane[i].zero();
    }
}

void ScenarioSelectLayout::init(const TableIter& /*rIter*/) {
    initLayoutManager("ScenarioSelect", 1);
    MR::createAndAddPaneCtrl(this, "ScenarioSelect", 1);
    MR::createAndAddPaneCtrl(this, "ScenarioFrame", 1);
    MR::createAndAddPaneCtrl(this, "Scenario", 1);
    MR::createAndAddPaneCtrl(this, "StarTop", 1);
    MR::createAndAddPaneCtrl(this, "StarDown", 1);
    MR::createAndAddPaneCtrl(this, "BestScore", 1);
    MR::createAndAddPaneCtrl(this, "BestTime", 1);
    MR::createAndAddPaneCtrl(this, "New", 1);
    MR::createAndAddPaneCtrl(this, "NewStarGreen", 1);
    MR::createAndAddPaneCtrl(this, "Mario", 1);
    MR::createAndAddPaneCtrl(this, "CometAppear", 2);
    MR::setFollowPos(&mStarTopFollowPos, this, "StarTop");
    MR::setFollowTypeAdd(this, "StarTop");
    MR::setFollowPos(&mNewTextFollowPos, this, "New");
    MR::setFollowPos(&mNewGreenTextFollowPos, this, "NewStarGreen");
    MR::setFollowPos(&mMarioPaneFollowPos, this, "Mario");

    for (u32 i = 0; i < ARRAY_SIZE(mQuestionPane); i++) {
        MR::createAndAddPaneCtrl(this, ::cQuestionPaneName[i], 1);
        MR::setFollowPos(&mQuestionPane[i], this, ::cQuestionPaneName[i]);
    }

    initPointingTarget(7);
    initEffectKeeper(0, "ScenarioSelect", mEffectSystem);
    MR::setEffectHostMtx(this, "ScenarioSelectEffect", mEffectHostMtx);

    mStar = new ScenarioSelectStar*[7];

    for (s32 i = 0; i < 7; i++) {
        mStar[i] = new ScenarioSelectStar(mEffectSystem);
        mStar[i]->initWithoutIter();

        MR::addStarPointerTargetCircle(this, ::cStarPaneName[i], ::cPointingTargetRadius, Vec2f(0.0f, 0.0f), nullptr);
    }

    mScenarioSky = new MultiSceneActor("シナリオ選択の空", "ScenarioSelectSky", false);
    mScenarioSky->initWithoutIter();

    mBackButton = new BackButton("戻るボタン", false);
    mBackButton->initWithoutIter();

    if (!NebulaRegion::isEurope()) {
        MR::hidePaneRecursive(this, "CometAppear");
    }

    initNerve(GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvAppearStar));
}

void ScenarioSelectLayout::appear() {
    MR::showLayout(this);

    s32 normalScenarioCompleteNum;
    s32 extraScenarioCompleteNum;
    bool isNewScenario = calcDisplayScenarioNum(&normalScenarioCompleteNum, &extraScenarioCompleteNum);

    MR::startPaneAnim(this, "ScenarioSelect", "Appear", 0);
    MR::setPaneAnimFrameAndStop(this, "ScenarioSelect", 0.0f, 0);
    startAnimAllNewPane("Appear");
    setAnimRateAllNewPane(0.0f);
    MR::startPaneAnim(this, "ScenarioFrame", "Wait", 0);
    MR::startPaneAnim(this, "StarTop", "StarPositionTop", 0);
    MR::setPaneAnimFrameAndStop(this, "StarTop", normalScenarioCompleteNum - 1, 0);

    if (extraScenarioCompleteNum > 0) {
        MR::startPaneAnim(this, "StarDown", "StarPositionDown", 0);
        MR::setPaneAnimFrameAndStop(this, "StarDown", extraScenarioCompleteNum - 1, 0);
        mStarTopFollowPos.zero();
    } else {
        mStarTopFollowPos.set(0.0f, cNum_appear_f1);
    }

    MR::setTextBoxMessageRecursive(this, "Galaxy", MR::getCurrentGalaxyNameOnCurrentLanguage());
    MR::hidePaneRecursive(this, "Scenario");
    setPlayerLeft();
    LayoutActor::appear();
    appearAllStar(normalScenarioCompleteNum, extraScenarioCompleteNum, isNewScenario);
    MR::hidePaneRecursive(this, "New");
    MR::hidePaneRecursive(this, "NewStarGreen");

    for (u32 i = 0; i < ARRAY_SIZE(::cQuestionPaneName); i++) {
        MR::hidePaneRecursive(this, ::cQuestionPaneName[i]);
    }

    if (isNewScenario) {
        MR::showPaneRecursive(this, "New");
        MR::copyPaneTrans(&mNewTextFollowPos, this, ::cStarPaneName[(normalScenarioCompleteNum - 1) % 7]);
    } else if (!MR::isScenarioDecided()) {
        const char* stageName = MR::getCurrentStageName();

        s32 hiddenIndex = 0;

        for (s32 scenarioNo = MR::getCurrentStageNormalScenarioNum() + 1; scenarioNo <= MR::getCurrentStagePowerStarNum(); scenarioNo++) {
            if (!MR::makeCurrentGalaxyStatusAccessor().isHiddenStar(scenarioNo)) {
                continue;
            }

            if (MR::isOnGameEventFlagPowerStarSuccess(stageName, scenarioNo)) {
                continue;
            }

            if (GameDataConst::isPowerStarLuigiHas(stageName, scenarioNo) && !MR::isLuigiHidingGalaxyAndScenario(stageName, scenarioNo)) {
                continue;
            }

            if (hiddenIndex >= static_cast< s32 >(ARRAY_SIZE(::cQuestionPaneName))) {
                break;
            }
            const char* starPaneName = ::cStarPaneName[(MR::getPlacedHiddenStarScenarioNo(stageName, scenarioNo) - 1) % 7];

            if (MR::isPowerStarGreenInCurrentStage(scenarioNo)) {
                MR::showPaneRecursive(this, "NewStarGreen");
                MR::copyPaneTrans(&mNewGreenTextFollowPos, this, starPaneName);
            } else {
                MR::showPaneRecursive(this, ::cQuestionPaneName[hiddenIndex]);
                MR::copyPaneTrans(&mQuestionPane[hiddenIndex], this, starPaneName);
                hiddenIndex++;
            }
        }
    }

    MR::startPaneAnim(this, "BestScore", "SelectIn", 0);
    MR::startPaneAnim(this, "BestTime", "SelectIn", 0);
    MR::setPaneAnimFrameAndStop(this, "BestScore", 0.0f, 0);
    MR::setPaneAnimFrameAndStop(this, "BestTime", 0.0f, 0);

    mScenarioSky->mTranslation.set(::cSkyTrans);
    mScenarioSky->mScale.set(::cSkyScale);
    mScenarioSky->appear();
    MultiScene::startBtk(mScenarioSky, "ScenarioSelectSky");

    if (NebulaRegion::isEurope()) {
        MR::hidePaneRecursive(this, "CometAppear");
    }

    mSelectedScenarioNo = -1;
    mIsDecided = false;

    setNerve(GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvAppearStar));
}

void ScenarioSelectLayout::kill() {
    for (s32 i = 0; i < 7; i++) {
        ScenarioSelectStar* pStar = mStar[i];

        if (pStar->mIsDead) {
            continue;
        }

        pStar->kill();
    }

    mScenarioSky->kill();
    mBackButton->kill();
    LayoutActor::kill();
}

void ScenarioSelectLayout::movement() {
    LayoutActor::movement();

    for (s32 i = 0; i < 7; i++) {
        mStar[i]->movement();
    }

    mScenarioSky->movement();
    mBackButton->movement();
}

void ScenarioSelectLayout::calcAnim() {
    LayoutActor::calcAnim();

    for (s32 i = 0; i < 7; i++) {
        mStar[i]->calcAnim();
    }

    mScenarioSky->calcAnim();
    mBackButton->calcAnim();
}

void ScenarioSelectLayout::draw() const {
    LayoutActor::draw();
    mBackButton->draw();
}

void ScenarioSelectLayout::calcViewAndEntryStarModel() {
    for (s32 i = 0; i < 7; i++) {
        mStar[i]->calcViewAndEntry();
    }

    mScenarioSky->calcViewAndEntry();
}

s32 ScenarioSelectLayout::getSelectedScenarioNo() const {
    return mSelectedScenarioNo;
}

bool ScenarioSelectLayout::isReadyToDisappear() const {
    if (isNerve(GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvAfterScenarioSelected))) {
        return MR::isGreaterStep(this, NebulaFrameClock::stepsOfMilliseconds(3500));
    }

    return false;
}

void ScenarioSelectLayout::disappear() {
    setNerve(GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvDisappear));
}

bool ScenarioSelectLayout::isAppearStarEnd() const {
    return !MR::isDead(this) && !isNerve(GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvAppearStar)) &&
           !isNerve(GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvAppearCometWarning)) &&
           !isNerve(GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvWaitCometWarning)) &&
           !isNerve(GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvDisappearCometWarning));
}

bool ScenarioSelectLayout::isEndAnimStartStep() const {
    return isNerve(GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvAfterScenarioSelected)) && MR::isStep(this, ::cSelectedEndAnimStartStep);
}

bool ScenarioSelectLayout::isCanceled() const {
    return isNerve(GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvCancel)) ||
           isNerve(GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvCancelFadeOut));
}

void ScenarioSelectLayout::control() {
    if (!mIsDecided && mSelectedScenarioNo > 0) {
        // No star shows the selected number (null) after a page change or a kill.
        ScenarioSelectStar* pSelected = getSelectedStar();
        if (pSelected != nullptr) {
            pSelected->tryPointing();
        }
    }

    MR::copyPaneTrans(&mMarioPaneFollowPos, this, mMarioPaneName);
}

void ScenarioSelectLayout::updateSelectedScenario() {
    ScenarioSelectStar* star;

    if (isNerve(GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvWaitScenarioSelect))) {
        for (s32 i = 0; i < 7; i++) {
            star = mStar[i];

            if (!star->mIsDead && MR::isStarPointerPointingTarget(this, ::cStarPaneName[i], 0, true, "弱")) {
                mSelectedScenarioNo = star->mScenarioNo;
                return;
            }
        }
    }

    mSelectedScenarioNo = -1;
}

void ScenarioSelectLayout::updateScenarioText() {
    bool isEndAnimStep = isNerve(GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvAfterScenarioSelected)) &&
                         MR::isGreaterEqualStep(this, ::cSelectedEndAnimStartStep);

    if (MR::isHiddenPane(this, "Scenario") || (mSelectedScenarioNo > 0 && getSelectedScenarioNo() != mTextScenarioNo)) {
        if (!isEndAnimStep && mSelectedScenarioNo > 0) {
            fadeInText();
        }
    } else if (mTextFadeState == 0) {
        if (MR::isPaneAnimStopped(this, "Scenario", 0) && mSelectedScenarioNo <= 0) {
            fadeOutText();
        }
    } else if (MR::isPaneAnimStopped(this, "Scenario", 0)) {
        MR::hidePaneRecursive(this, "Scenario");
    }
}

void ScenarioSelectLayout::fadeInText() {
    const char* stageName = MR::getCurrentStageName();

    mTextFadeState = 0;
    mTextScenarioNo = mSelectedScenarioNo;

    MR::setTextBoxMessageRecursive(this, "Scenario", MR::getScenarioNameOnCurrentLanguage(stageName, mSelectedScenarioNo));
    MR::showPaneRecursive(this, "Scenario");
    MR::startPaneAnim(this, "Scenario", "SelectIn", 0);

    bool isPlacedCoin = MR::isPlacedCoin(MR::getCurrentStageName(), mSelectedScenarioNo);

    MR::startPaneAnim(this, "BestScore", "SelectIn", 0);

    if (isPlacedCoin) {
        const s32 bestScore = MR::getCoinBestScore(stageName, mSelectedScenarioNo);
        MR::setTextBoxFormatRecursive(this, "ShaCoinNum", u"%03d", bestScore);
    } else {
        MR::setPaneAnimFrameAndStop(this, "BestScore", 0.0f, 0);
    }

    MR::startPaneAnim(this, "BestTime", "SelectIn", 0);

    if (::isCurrentStageRaceComplete(mSelectedScenarioNo)) {
        MR::Text16 bestTime;
        MR::setTextBoxMessageRecursive(this, "ShaTime",
                                       MR::makeRaceBestTimeString(bestTime, RaceManagerFunction::getRaceId(stageName, mSelectedScenarioNo)));
    } else {
        MR::setPaneAnimFrameAndStop(this, "BestTime", 0.0f, 0);
        MR::clearTextBoxMessageRecursive(this, "ShaTime");
    }
}

void ScenarioSelectLayout::fadeOutText() {

    mTextFadeState = 1;

    MR::startPaneAnim(this, "Scenario", "SelectOut", 0);

    if (MR::isPlacedCoin(MR::getCurrentStageName(), mTextScenarioNo)) {
        MR::startPaneAnim(this, "BestScore", "SelectOut", 0);
    }

    if (::isCurrentStageRaceComplete(mTextScenarioNo)) {
        MR::startPaneAnim(this, "BestTime", "SelectOut", 0);
    }
}

bool ScenarioSelectLayout::trySelect() {
    if (MR::testPointerMenuPadDecideTrigger()) {
        if (mSelectedScenarioNo > 0) {
            return true;
        }

        if (!MR::isStarPointerInScreen(PLAYER_1)) {
            for (s32 i = 0; i < 7; i++) {
                if (mStar[i]->mIsDead) {
                    continue;
                }

                if (mStar[i]->mStarCollectedStatus) {
                    continue;
                }

                mSelectedScenarioNo = mStar[i]->mScenarioNo;

                return true;
            }
        }
    }

    return false;
}

ScenarioSelectStar* ScenarioSelectLayout::getSelectedStar() const {
    for (s32 i = 0; i < 7; i++) {
        if (mStar[i]->mIsDead) {
            continue;
        }

        if (mSelectedScenarioNo != mStar[i]->mScenarioNo) {
            continue;
        }

        return mStar[i];
    }

    return nullptr;
}

void ScenarioSelectLayout::calcWorldPositionFromScreenPos(Vec3f* pWorldPos, const Vec2f& rScreenPos, f32 depth) const {
    f32 fovy = mCameraContext->getFovy() * (PI / cNum_calcWorldPositionFromScreenPos_f1);
    f32 height = static_cast< s32 >(NebulaFrameClock::cScreenLines);
    f32 focalLength = (height * cNum_calcWorldPositionFromScreenPos_f2) / MR::tan(fovy * cNum_calcWorldPositionFromScreenPos_f3);
    f32 distance;

    if (depth >= 0.0f) {
        distance = depth;
    } else {
        f32 defaultDistance = focalLength;
        distance = defaultDistance;
    }

    f32 scale = distance / focalLength;
    Vec3f pos(scale * (rScreenPos.x - MR::getScreenWidth() * cNum_calcWorldPositionFromScreenPos_f4), scale * -(rScreenPos.y - height * cNum_calcWorldPositionFromScreenPos_f5), -distance);
    Pos3f invView(mCameraContext->getInvViewMtx());
    invView.mult(pos, *pWorldPos);
}

bool ScenarioSelectLayout::calcDisplayScenarioNum(s32* pNormalScenarioCompleteNum, s32* pExtraScenarioCompleteNum) const {
    if (MR::isScenarioDecided()) {
        *pNormalScenarioCompleteNum = 1;
        *pExtraScenarioCompleteNum = 0;

        return !MR::hasPowerStarInCurrentStage(MR::getCurrentScenarioNo());
    }

    bool isNewScenario = false;
    s32 index;

    for (*pNormalScenarioCompleteNum = 0; (index = *pNormalScenarioCompleteNum) < MR::getCurrentStageNormalScenarioNum();
         *pNormalScenarioCompleteNum += 1) {
        if (MR::hasPowerStarInCurrentStage(index + 1)) {
            continue;
        }

        *pNormalScenarioCompleteNum += 1;
        isNewScenario = true;
        break;
    }

    *pExtraScenarioCompleteNum = 0;

    for (s32 i = 0; i < getHiddenScenarioNum(); i++) {
        if (MR::hasPowerStarInCurrentStage(MR::getCurrentStageNormalScenarioNum() + i + 1)) {
            *pExtraScenarioCompleteNum += 1;
        }
    }

    return isNewScenario;
}

void ScenarioSelectLayout::appearAllStar(s32 normalScenarioCompleteNum, s32 /*extraScenarioCompleteNum*/, bool isNewScenario) {
    if (MR::isScenarioDecided()) {
        s32 starState = 2;

        if (isNewScenario) {
            starState = 0;
        }

        appearStar(0, MR::getCurrentScenarioNo(), starState);
    } else {
        const s32 room = normalScenarioCompleteNum + getHiddenScenarioNum();
        delete[] mListScenario;
        delete[] mListState;
        mListScenario = new s32[room > 0 ? room : 1];
        mListState = new s32[room > 0 ? room : 1];
        mListNum = 0;
        mListPage = 0;

        for (s32 i = 0; i < normalScenarioCompleteNum; i++) {
            s32 starState;

            if (!isNewScenario) {
                starState = 2;
            } else if (i < normalScenarioCompleteNum - 1) {
                starState = 1;
            } else {
                starState = 0;
            }

            mListScenario[mListNum] = i + 1;
            mListState[mListNum++] = starState;
        }

        for (s32 j = 0; j < getHiddenScenarioNum(); j++) {
            s32 scenarioNo = MR::getCurrentStageNormalScenarioNum() + j + 1;

            if (MR::hasPowerStarInCurrentStage(scenarioNo)) {
                mListScenario[mListNum] = scenarioNo;
                mListState[mListNum++] = isNewScenario ? 1 : 2;
            }
        }

        // The game's own places: its normal stars first, the rest from the fourth. With more than the screen holds, pages.
        if (mListNum <= 7) {
            s32 starIndex = 0;
            for (s32 i = 0; i < mListNum; i++) {
                // The jump to the fourth place only when the rest still fits the seven (a mod's galaxy with many extras).
                if (i == normalScenarioCompleteNum && starIndex < 3 && 3 + (mListNum - i) <= 7) {
                    starIndex = 3;
                }
                appearStar(starIndex++, mListScenario[i], mListState[i]);
            }
        } else {
            appearPage(0);
        }
    }
}

void ScenarioSelectLayout::appearPage(s32 page) {
    for (s32 i = 0; i < 7; i++) {
        if (!mStar[i]->mIsDead) {
            mStar[i]->kill();
        }
    }
    mListPage = page;
    mSelectedScenarioNo = -1;
    // Six on a page: the screen's seventh place lies over the second.
    for (s32 i = 0; i < 6 && page * 6 + i < mListNum; i++) {
        appearStar(i, mListScenario[page * 6 + i], mListState[page * 6 + i]);
    }

    // The page, after the galaxy's name.
    const char16_t* pName = MR::getGalaxyNameOnCurrentLanguage(MR::getCurrentStageName());
    s32 nameLength = 0;
    while (pName != nullptr && pName[nameLength] != 0) {
        nameLength++;
    }
    // The name, then " < " and up to two digits, "/", up to two digits, " >" and the terminator.
    MR::Text16 textBuffer;
    char16_t* const text = textBuffer.room(static_cast< u32 >(nameLength) + 16);
    s32 at = 0;
    while (at < nameLength) {
        text[at] = pName[at];
        at++;
    }
    const s32 pages = (mListNum + 5) / 6;
    const s32 number = page + 1;
    text[at++] = u' ';
    text[at++] = u' ';
    text[at++] = u'<';
    text[at++] = u' ';
    if (number >= 10) {
        text[at++] = static_cast< char16_t >(u'0' + number / 10 % 10);
    }
    text[at++] = static_cast< char16_t >(u'0' + number % 10);
    text[at++] = u'/';
    if (pages >= 10) {
        text[at++] = static_cast< char16_t >(u'0' + pages / 10 % 10);
    }
    text[at++] = static_cast< char16_t >(u'0' + pages % 10);
    text[at++] = u' ';
    text[at++] = u'>';
    text[at] = 0;
    MR::setTextBoxMessageRecursive(this, "Galaxy", text);
}

void ScenarioSelectLayout::tryChangePage() {
    if (mListNum <= 7) {
        return;
    }
    const s32 pages = (mListNum + 5) / 6;
    // A pointer: pressing with it at either side of the screen, on no star.
    const f32 pointerX = MR::isStarPointerInScreen(0) && MR::testPointerMenuPadDecideTrigger() && mSelectedScenarioNo <= 0 ? MR::getStarPointerScreenPosition(0)->x : MR::getScreenWidth() * cNum_tryChangePage_f1;
    const bool left = MR::isHeldLeft(0) || MR::isMoveStickPressedLeft(0) || pointerX < MR::getScreenWidth() * cNum_tryChangePage_f2;
    const bool right = MR::isHeldRight(0) || MR::isMoveStickPressedRight(0) || pointerX > MR::getScreenWidth() * cNum_tryChangePage_f3;
    if (!left && !right) {
        mPageKeyHeld = false;
        return;
    }
    if (mPageKeyHeld) {
        return;
    }
    mPageKeyHeld = true;
    MR::startSystemSE("SE_SY_GALAMAP_CURSOR_ON");
    appearPage((mListPage + (right ? 1 : pages - 1)) % pages);
}

void ScenarioSelectLayout::appearStar(s32 starId, s32 scenarioNo, int a3) {
    ScenarioSelectStar* pStar = mStar[starId];

    Vec2f starPaneTrans;
    MR::copyPaneTrans(&starPaneTrans, this, ::cStarPaneName[starId]);

    Vec3f worldPos;
    calcWorldPositionFromScreenPos(&worldPos, starPaneTrans, cNum_appearStar_f1);

    pStar->setup(scenarioNo, a3, worldPos, starId);
    pStar->appear();
}

bool ScenarioSelectLayout::isAppearStarEndAll() const {
    for (s32 i = 0; i < 7; i++) {
        if (mStar[i]->mIsDead) {
            continue;
        }

        if (mStar[i]->isAppearEnd()) {
            continue;
        }

        return false;
    }

    return true;
}

bool ScenarioSelectLayout::tryCancel() {
    if (mBackButton->isSelected()) {
        setNerve(GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvCancel));
        return true;
    }

    if (MR::isPressedFire(PLAYER_1)) {
        MR::startSystemSE("SE_SY_GALAXY_DECIDE_CANCEL");
        mBackButton->disappear();
        setNerve(GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvCancel));
        return true;
    }

    return false;
}

void ScenarioSelectLayout::setPlayerLeft() {
    MR::setTextBoxGameMessageRecursive(this, "ShaMario", "PlayerLeft");
    MR::setTextBoxNumberRecursive(this, "ShaMarioNum", MR::getPlayerLeft());

    if (MR::getPlayerLeft() < 10) {
        mMarioPaneName = "MarioPosition01";
    } else {
        mMarioPaneName = "MarioPosition10";
    }
}

void ScenarioSelectLayout::startAnimAllNewPane(const char* pAnimName) {
    MR::startPaneAnim(this, "New", pAnimName, 0);
    MR::startPaneAnim(this, "NewStarGreen", pAnimName, 0);

    for (u32 i = 0; i < ARRAY_SIZE(::cQuestionPaneName); i++) {
        MR::startPaneAnim(this, ::cQuestionPaneName[i], pAnimName, 0);
    }
}

void ScenarioSelectLayout::setAnimRateAllNewPane(f32 rate) {
    MR::setPaneAnimRate(this, "New", rate, 0);
    MR::setPaneAnimRate(this, "NewStarGreen", rate, 0);

    for (u32 i = 0; i < ARRAY_SIZE(::cQuestionPaneName); i++) {
        MR::setPaneAnimRate(this, ::cQuestionPaneName[i], rate, 0);
    }
}

void ScenarioSelectLayout::exeCancelFadeOut() {
    kill();
}

void ScenarioSelectLayout::exeAppearStar() {
    if (MR::isFirstStep(this)) {
        MR::permitTrigSE();
        MR::getEffect(this, "ScenarioSelectEffect")->create(mEffectSystem);
        MR::setEffectRate(this, "ScenarioSelectEffect", ::cSelectedEffectRateMin);
        MR::setEffectDirectionalSpeed(this, "ScenarioSelectEffect", ::cSelectedEffectSpeedMin);

        if (!MR::isPlayingStageBgmName("BGM_SENARIO_SEL")) {
            MR::startStageBGM("BGM_SENARIO_SEL_2", false);
        }
    }

    MR::startSystemLevelSE("SE_DM_LV_SENARIO_SEL_FLY");

    if (!isAppearStarEndAll()) {
        return;
    }

    s32 scenarioNo;
    bool needsAppearComet;

    if (!MR::isScenarioDecided()) {
        needsAppearComet = false;
    } else {
        scenarioNo = MR::getCurrentScenarioNo();
        needsAppearComet = false;
        GalaxyStatusAccessor galaxyAccessor = MR::makeCurrentGalaxyStatusAccessor();

        if (galaxyAccessor.isCometStar(scenarioNo)) {
            if (scenarioNo == MR::getEncounterGalaxyCometPowerStarId(MR::getCurrentStageName())) {
                needsAppearComet = true;
            }
        }
    }

    if (needsAppearComet) {
        setNerve(GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvAppearCometWarning));
    } else {
        setNerve(GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvAppear));
    }
}

void ScenarioSelectLayout::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::setPaneAnimRate(this, "ScenarioSelect", 1.0f, 0);
        setAnimRateAllNewPane(1.0f);
    }

    MR::startSystemLevelSE("SE_DM_LV_SENARIO_SEL_FLY");
    MR::setNerveAtPaneAnimStopped(this, "ScenarioSelect", GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvWaitScenarioSelect), 0);
}

void ScenarioSelectLayout::exeWaitScenarioSelect() {
    if (MR::isFirstStep(this)) {
        startAnimAllNewPane("NewWait");
        mBackButton->appear();
        if (mListNum > 7) {
            appearPage(mListPage);
        }
    }

    tryChangePage();
    updateSelectedScenario();
    updateScenarioText();
    MR::startSystemLevelSE("SE_DM_LV_SENARIO_SEL_FLY");

    if (tryCancel()) {
        return;
    }

    if (trySelect()) {
        mIsDecided = true;
        MR::startSystemSE("SE_SY_DECIDE_1");
        MR::startCSSound("CS_CLICK_CLOSE", nullptr, 0);
        mBackButton->disappear();
        setNerve(GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvDecide));
    }
}

void ScenarioSelectLayout::exeDecide() {
    if (MR::isFirstStep(this)) {
        for (s32 i = 0; i < 7; i++) {
            if (mStar[i]->mIsDead) {
                continue;
            }

            if (mStar[i] == getSelectedStar()) {
                mStar[i]->select();
            } else {
                mStar[i]->notSelect();
            }
        }

        startAnimAllNewPane("End");
    }

    updateScenarioText();
    MR::startSystemLevelSE("SE_DM_LV_SENARIO_SEL_FLY");
    MR::setNerveAtStep(this, GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvAfterScenarioSelected), ::cDecideFrame);
}

void ScenarioSelectLayout::exeAfterScenarioSelected() {
    MR::startPaneAnimAtStep(this, "ScenarioFrame", "End", ::cSelectedEndAnimStartStep, 0);
    updateScenarioText();
    MR::startSystemLevelSE("SE_DM_LV_SENARIO_SEL_FLY");

    f32 rate = MR::calcNerveEaseInValue(this, ::cSelectedAccelStartStep, ::cSelectedAccelFrame, ::cSelectedEffectRateMin, ::cSelectedEffectRateMax);
    f32 directionalSpeed =
        MR::calcNerveEaseInValue(this, ::cSelectedAccelStartStep, ::cSelectedAccelFrame, ::cSelectedEffectSpeedMin, ::cSelectedEffectSpeedMax);

    MR::setEffectRate(this, "ScenarioSelectEffect", rate);
    MR::setEffectDirectionalSpeed(this, "ScenarioSelectEffect", directionalSpeed);

    if (MR::isStep(this, ::cSelectedFadeOutStartStep)) {
        MR::stopStageBGM(::cSelectedFadeOutFrame);
        MR::closeSystemWipeWhiteFade(::cSelectedFadeOutFrame);
    }
}

void ScenarioSelectLayout::exeDisappear() {
    if (MR::isFirstStep(this)) {
        MR::hideLayout(this);
        MR::getEffect(this, "ScenarioSelectEffect")->forceDelete(mEffectSystem);

        for (s32 i = 0; i < 7; i++) {
            ScenarioSelectStar* star = mStar[i];

            if (!star->mIsDead) {
                star->kill();
            }
        }

        mScenarioSky->kill();
        MR::openSystemWipeWhiteFade(::cSelectedFadeInFrame);
    }

    if (MR::isSystemWipeActive()) {
        return;
    }

    kill();
}

void ScenarioSelectLayout::exeCancel() {
    if (MR::isFirstStep(this)) {
        mSelectedScenarioNo = -1;
    }

    updateScenarioText();

    if (mBackButton->isHidden()) {
        MR::closeSystemWipeFadeWithCaptureScreen(::cCancelFadeOutFrame);
        MR::stopStageBGM(1);
        setNerve(GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvCancelFadeOut));
    }
}

void ScenarioSelectLayout::exeAppearCometWarning() {
    if (MR::isStep(this, ::cCometWarningWaitFrame)) {
        int id = MR::getEncounterGalaxyCometNameId(MR::getCurrentStageName());
        const char* cometName = MR::getCometNameFromId(id);
        MR::FormattedName cometMsg;
        MR::makeCometMessageID(cometMsg, cometName);
        MR::setTextBoxGameMessageRecursive(this, "TextComet", "GalaxyInfo_CometAppearR");
        MR::setTextBoxArgGameMessageRecursive(this, "TextComet", cometMsg.c_str(), 0);
        MR::showPaneRecursive(this, "CometAppear");
        MR::startPaneAnim(this, "CometAppear", "CometAppear", 0);
        MR::setCometPaneAnimFromId(this, "CometAppear", id, 1);
        MR::startSystemSE("SE_SY_COMET_WARNING_DISP");
    }

    if (MR::isGreaterStep(this, ::cCometWarningWaitFrame)) {
        MR::setNerveAtPaneAnimStopped(this, "CometAppear", GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvWaitCometWarning), 0);
    }
}

void ScenarioSelectLayout::exeWaitCometWarning() {
    if (getNerveStep() % 80 == 25) {
        MR::startSystemSE("SE_SY_COMET_WARNING");
    }

    MR::startPaneAnimAtFirstStep(this, "CometAppear", "CometWait", 0);
    MR::setNerveAtStep(this, GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvDisappearCometWarning), ::cCometWarningFrame);
}

void ScenarioSelectLayout::exeDisappearCometWarning() {
    MR::startPaneAnimAtFirstStep(this, "CometAppear", "CometEnd", 0);
    MR::setNerveAtPaneAnimStopped(this, "CometAppear", GET_NERVE(ScenarioSelectLayout, ScenarioSelectLayoutNrvAppear), 0);
}
