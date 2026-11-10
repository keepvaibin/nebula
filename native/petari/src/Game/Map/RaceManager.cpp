#include "Game/Util/SortUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Map/RaceManager.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/RaceRail.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <algorithm>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_playGo_f1, "playGo.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isPlayCountAnim_f1, "isPlayCountAnim.f1", 180.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeIntro_1, "exeIntro.1", 110);


namespace {
    NEBULA_TUNABLE(s32, sBgmStartStep, 166);
};  // namespace

namespace {
    struct RaceStructData {
        const char* mName;
        s32 mEventBgmId;
        s32 mMaxTime;  // seconds; 0 is no limit
        const char* mMessageId;
        const char* mGalaxyName;
        s32 mScenarioNo;
        bool mIsDemoWithStarPointer;
    };

    static const RaceStructData sRaceStruct[] = {
        {"ペンギンレース[オーシャンリング]", MR::EventBgmID_PenguinRace, 120, "RaceName_Penguin", "OceanRingGalaxy", 2, false},
        {"テレサレース[ファントム]", MR::EventBgmID_TeresaRace, 120, "RaceName_TeresaPhantom", "PhantomGalaxy", 2, true},
        {"テレサレース[デスプロムナード]", MR::EventBgmID_TeresaRace, 120, "RaceName_TeresaDeathPromenade", "TeresaMario2DGalaxy", 1, false},
        {"サーフィン[トライアル]", MR::EventBgmID_Surfing, 0, "RaceName_SurfingTrial", "SurfingLv1Galaxy", 1, false},
        {"サーフィン[チャレンジ]", MR::EventBgmID_Surfing, 180, "RaceName_SurfingChallenge", "SurfingLv2Galaxy", 1, false}};

    const RaceStructData& getRaceStructData(s32 id) {
        // A race number from a rail's Arg0 outside the table takes the first race.
        if (id < 0 || id >= static_cast< s32 >(ARRAY_SIZE(::sRaceStruct))) {
            return ::sRaceStruct[0];
        }

        return ::sRaceStruct[id];
    }

    RaceManager* getRaceManager() {
        return MR::getSceneObj< RaceManager >(SceneObj_RaceManager);
    }
};  // namespace

RaceManagerLayout::RaceManagerLayout(const char* pName) : LayoutActor(pName, true) {
}

void RaceManagerLayout::init(const TableIter& /*rIter*/) {
    MR::connectToSceneLayout(this);
    initLayoutManager("Race", 1);
    MR::createAndAddPaneCtrl(this, "Go", 1);
    MR::createAndAddPaneCtrl(this, "Goal", 1);
    MR::createAndAddPaneCtrl(this, "Lose", 1);
    MR::createAndAddPaneCtrl(this, "TimeUp", 1);
    MR::createAndAddPaneCtrl(this, "Rank", 1);
    MR::createAndAddPaneCtrl(this, "ScoreTime", 1);
    initEffectKeeper(2, "Race", nullptr);
    hideAllPane();
}

void RaceManagerLayout::playCountAndGo() {
    MR::showPane(this, "Go");
    MR::showPane(this, "BestRecord");
    MR::startPaneAnim(this, "Go", "Count", 0);
}

void RaceManagerLayout::playGo() {
    MR::showPane(this, "Go");
    MR::startPaneAnim(this, "Go", "Count", 0);
    MR::setPaneAnimFrame(this, "Go", cNum_playGo_f1, 0);
    MR::emitEffect(this, "RaceStart");
}

void RaceManagerLayout::playGoal() {
    MR::showPane(this, "Goal");
    MR::startPaneAnim(this, "Goal", "Goal", 0);
    MR::startPaneAnim(this, "ScoreTime", "Goal", 0);
    MR::emitEffect(this, "RaceGoal");
}

void RaceManagerLayout::playLose() {
    MR::showPane(this, "Lose");
    MR::startPaneAnim(this, "Lose", "Lose", 0);
    MR::startPaneAnim(this, "ScoreTime", "Lose", 0);
}

void RaceManagerLayout::playTimeUp() {
    MR::showPane(this, "TimeUp");
    MR::startPaneAnim(this, "TimeUp", "TimeUp", 0);
    MR::startPaneAnim(this, "ScoreTime", "TimeUp", 0);
}

void RaceManagerLayout::playRank(u32 rank) {
    MR::showPane(this, "Rank");
    MR::startPaneAnim(this, "Rank", "Rank", 0);
    MR::setPaneAnimFrameAndStop(this, "Rank", rank, 0);
}

bool RaceManagerLayout::isAllAnimStopped() {
    return MR::isPaneAnimStopped(this, "Go", 0) && MR::isPaneAnimStopped(this, "Goal", 0) && MR::isPaneAnimStopped(this, "Lose", 0) &&
           MR::isPaneAnimStopped(this, "TimeUp", 0) && MR::isPaneAnimStopped(this, "ScoreTime", 0) && MR::isPaneAnimStopped(this, "Rank", 0);
}

void RaceManagerLayout::hideRecordPane() {
    MR::hidePane(this, "Record");
}

void RaceManagerLayout::hideBestRecordPane() {
    MR::hidePane(this, "BestRecord");
}

void RaceManagerLayout::hideAllPane() {
    MR::hidePane(this, "Go");
    MR::hidePane(this, "Goal");
    MR::hidePane(this, "Lose");
    MR::hidePane(this, "TimeUp");
    MR::hidePane(this, "Record");
    MR::hidePane(this, "BestRecord");
    MR::hidePane(this, "Rank");
}

bool RaceManagerLayout::isPlayCountAnim() const {
    return MR::getPaneAnimFrame(this, "Go", 0) < cNum_isPlayCountAnim_f1;
}

bool RaceManagerLayout::isPlayCountAndGoAnim() const {
    return !MR::isPaneAnimStopped(this, "Go", 0);
}

void RaceManagerLayout::setTime(u32 time) {
    MR::updateClearTimeTextBox(this, "TimeCounter", time);
}

void RaceManagerLayout::setBestTime(u32 bestTime) {
    MR::updateClearTimeTextBox(this, "BestTimeCounter", bestTime);
}

namespace NrvRaceManager {
    NEW_NERVE(RaceManagerNrvWait, RaceManager, Wait);
    NEW_NERVE(RaceManagerNrvPrepWipe, RaceManager, Prep);
    NEW_NERVE(RaceManagerNrvPrepImme, RaceManager, Prep);
    NEW_NERVE(RaceManagerNrvPreWipeOut, RaceManager, WipeOut);
    NEW_NERVE(RaceManagerNrvPreWipeWait, RaceManager, WipeWait);
    NEW_NERVE(RaceManagerNrvPreWipeIn, RaceManager, WipeIn);
    NEW_NERVE(RaceManagerNrvIntro, RaceManager, Intro);
    NEW_NERVE(RaceManagerNrvCount, RaceManager, Count);
    NEW_NERVE(RaceManagerNrvRace, RaceManager, Race);
    NEW_NERVE(RaceManagerNrvDemo, RaceManager, Demo);
    NEW_NERVE(RaceManagerNrvGoal, RaceManager, Goal);
    NEW_NERVE(RaceManagerNrvRank, RaceManager, Rank);
    NEW_NERVE(RaceManagerNrvPstWipeOut, RaceManager, WipeOut);
    NEW_NERVE(RaceManagerNrvPstWipeWait, RaceManager, WipeWait);
    NEW_NERVE(RaceManagerNrvPstWipeIn, RaceManager, WipeIn);
};  // namespace NrvRaceManager

RaceManager::RaceManager()
    : LiveActor("レース管理"), mLayout(nullptr), mRacerNum(0), mAudienceNum(0), mRank(0), mBestTime(0), mTime(0), mPlayerRacer(nullptr) {
    mGoalInvMtx.identity();
}

void RaceManager::init(const TableIter& rIter) {
    MR::connectToSceneMapObjMovement(this);

    mLayout = new RaceManagerLayout("レース管理用レイアウト");
    mLayout->init(rIter);

    MR::invalidateClipping(this);
    initNerve(GET_NERVE(RaceManager, RaceManagerNrvWait));
    makeActorAppeared();
}

void RaceManager::movement() {
    LiveActor::movement();
}

void RaceManager::exeWait() {
}

void RaceManager::exePrep() {
    for (auto it = &mRacer[0], end = &mRacer[mRacerNum]; it != end; ++it) {
        (*it)->initRacer();
    }

    if (isNerve(GET_NERVE(RaceManager, RaceManagerNrvPrepWipe))) {
        setNerve(GET_NERVE(RaceManager, RaceManagerNrvPreWipeOut));
    } else {
        prepBind();
        setNerve(GET_NERVE(RaceManager, RaceManagerNrvIntro));
    }
}

void RaceManager::exeWipeOut() {
    if (MR::isFirstStep(this)) {
        if (isNerve(GET_NERVE(RaceManager, RaceManagerNrvPreWipeOut))) {
            MR::stopStageBGM(90);
        }

        MR::closeWipeFade();
    }

    if (MR::isWipeActive()) {
        return;
    }

    if (isNerve(GET_NERVE(RaceManager, RaceManagerNrvPreWipeOut))) {
        prepRace();
        setNerve(GET_NERVE(RaceManager, RaceManagerNrvPreWipeWait));
    } else {
        resetRace();
        setNerve(GET_NERVE(RaceManager, RaceManagerNrvPstWipeWait));
    }
}

void RaceManager::exeWipeWait() {
    if (!MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
        return;
    }

    if (isNerve(GET_NERVE(RaceManager, RaceManagerNrvPreWipeWait))) {
        setNerve(GET_NERVE(RaceManager, RaceManagerNrvPreWipeIn));
    } else {
        setNerve(GET_NERVE(RaceManager, RaceManagerNrvPstWipeIn));
    }
}

void RaceManager::exeWipeIn() {
    if (MR::isFirstStep(this)) {
        if (isNerve(GET_NERVE(RaceManager, RaceManagerNrvPreWipeIn))) {
            MR::startSubBGM("BGM_MINIGAME_START", false);
        } else if (!MR::isPlayerDead()) {
            MR::startCurrentStageBGM();
        }

        MR::openWipeFade();
    }

    if (MR::isWipeActive()) {
        return;
    }

    if (isNerve(GET_NERVE(RaceManager, RaceManagerNrvPreWipeIn))) {
        setNerve(GET_NERVE(RaceManager, RaceManagerNrvIntro));
    } else {
        MR::onPlayerControl(true);
        MR::endDemo(this, "レース");
        for (auto it = &mRacer[0], end = &mRacer[mRacerNum]; it != end; ++it) {
            (*it)->exitRacer();
        }
        setNerve(GET_NERVE(RaceManager, RaceManagerNrvWait));
    }
}

void RaceManager::exeIntro() {
    if (MR::isFirstStep(this)) {
        if (::getRaceStructData(mCurrentRace).mIsDemoWithStarPointer) {
            MR::startStarPointerModeDemoWithStarPointer(this);
        } else {
            MR::startStarPointerModeDemo(this);
        }
    }

    if (MR::isStep(this, cStep_exeIntro_1)) {
        setNerve(GET_NERVE(RaceManager, RaceManagerNrvCount));
    }
}

void RaceManager::exeCount() {
    if (MR::isFirstStep(this)) {
        mLayout->setTime(0);
        mLayout->setBestTime(mBestTime);
        mLayout->appear();
        mLayout->playCountAndGo();
        MR::showPane(mLayout, "Record");
    }

    mLayout->movement();

    if (getNerveStep() % NebulaFrameClock::stepsOfSeconds(1) == 0) {
        MR::startSystemSE("SE_SY_RACE_COUNT_DOWN");
    }

    if (MR::isStep(this, ::sBgmStartStep)) {
        MR::startEventBGM(::getRaceStructData(mCurrentRace).mEventBgmId);
    }

    if (mLayout->isPlayCountAnim()) {
        return;
    }

    mLayout->playGo();
    startRace();
    setNerve(GET_NERVE(RaceManager, RaceManagerNrvRace));
}

void RaceManager::exeRace() {
    if (MR::isFirstStep(this)) {
        MR::startSystemSE("SE_SY_RACE_START");
    }

    s32 maxTime = ::getRaceStructData(mCurrentRace).mMaxTime;
    MR::updateClearTimeTextBox(mLayout, "TimeCounter", getNerveStep());
    if (maxTime > 0 && getNerveStep() >= NebulaFrameClock::stepsOfSeconds(maxTime)) {
        mRank = 0;
        renewTime();
        setNerve(GET_NERVE(RaceManager, RaceManagerNrvDemo));
        return;
    }

    if (mRacerNum > 1) {
        mLayout->playRank(getRank(mPlayerRacer));
    }

    for (u32 idx = mRank; idx < mRacerNum; idx++) {
        AbstractRacer* racer = mRacer[idx];
        if (racer->updateRacer(this)) {
            std::swap(mRacer[idx], mRacer[mRank++]);
            if (racer->goalRacer()) {
                renewTime();
                setNerve(GET_NERVE(RaceManager, RaceManagerNrvDemo));
                break;
            }
        }
    }

    MR::sort(&mRacer[mRank], &mRacer[mRacerNum], AbstractRacer::compRacer);
}

void RaceManager::exeDemo() {
    if (!MR::canStartDemo()) {
        return;
    }

    goalRace();
    MR::offPlayerControl();
    setNerve(GET_NERVE(RaceManager, RaceManagerNrvGoal));
}

void RaceManager::exeGoal() {
    if (MR::isFirstStep(this)) {
        MR::stopStageBGM(30);

        if (mRank == 0) {
            MR::startSubBGM("BGM_RACE_LOSE", false);
            mLayout->playTimeUp();
        } else if (mRank == 1) {
            MR::startSubBGM("BGM_RACE_WIN", false);
            mLayout->playGoal();
        } else {
            MR::startSubBGM("BGM_RACE_LOSE", false);
            mLayout->playLose();
        }
    }

    if (mLayout->isAllAnimStopped()) {
        setNerve(GET_NERVE(RaceManager, RaceManagerNrvRank));
    }
}

void RaceManager::exeRank() {
    if (MR::isLessStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        return;
    }

    if (!MR::canStartDemo()) {
        return;
    }

    MR::tryStartDemoWithoutCinemaFrame(this, "レース");
    setNerve(GET_NERVE(RaceManager, RaceManagerNrvPstWipeOut));
}

bool RaceManager::startWithWipe() {
    mRank = 0;

    mLayout->hideAllPane();
    MR::requestStartDemoMarioPuppetableWithoutCinemaFrame(this, "レース", GET_NERVE(RaceManager, RaceManagerNrvPrepWipe), nullptr);

    return true;
}

bool RaceManager::startImmediately() {
    mRank = 0;

    mLayout->hideAllPane();
    MR::requestStartDemoWithoutCinemaFrame(this, "レース", GET_NERVE(RaceManager, RaceManagerNrvPrepImme), nullptr);
    MR::requestMovementOnPlayer();
    MR::stopStageBGM(90);
    MR::startSubBGM("BGM_MINIGAME_START", false);

    return true;
}

void RaceManager::prepRace() {
    mPlayerRacer->prepRacer(this);

    for (s32 i = 0; i < mRacerNum; i++) {
        AbstractRacer* pRacer = mRacer[i];

        if (pRacer != mPlayerRacer) {
            pRacer->prepRacer(this);
        }
    }

    for (auto it = &mAudience[0], end = &mAudience[mAudienceNum]; it != end; ++it) {
        (*it)->prepAudience();
    }
}

void RaceManager::startRace() {
    if (MR::isPlayerInBind()) {
        MR::sendSimpleMsgToActor(ACTMES_RACE_START, MR::getCurrentRushActor());
    }

    for (auto it = &mRacer[0], end = &mRacer[mRacerNum]; it != end; ++it) {
        (*it)->startRacer();
    }
    MR::endStarPointerMode(this);
    MR::endDemo(this, "レース");
    MR::onPlayerControl(true);
}

void RaceManager::goalRace() {
    if (MR::isPlayerInBind()) {
        MR::sendSimpleMsgToActor(ACTMES_RACE_GOAL, MR::getCurrentRushActor());
    }

    for (auto it = &mRacer[mRank], end = &mRacer[mRacerNum]; it != end; ++it) {
        (*it)->loseRacer();
    }
}

void RaceManager::resetRace() {
    if (MR::isPlayerInBind()) {
        MR::sendSimpleMsgToActor(ACTMES_RACE_RESET, MR::getCurrentRushActor());
    }

    mPlayerRacer->resetRacer(this);

    for (s32 i = 0; i < mRacerNum; i++) {
        AbstractRacer* pRacer = mRacer[i];

        if (pRacer != mPlayerRacer) {
            pRacer->resetRacer(this);
        }
    }

    for (auto it = &mAudience[0], end = &mAudience[mAudienceNum]; it != end; ++it) {
        (*it)->resetAudience();
    }
    mLayout->kill();
}

void RaceManager::prepBind() {
    if (!MR::isPlayerInBind()) {
        return;
    }

    MR::requestMovementOn(MR::getCurrentRushActor());
    MR::sendSimpleMsgToActor(ACTMES_RACE_READY, MR::getCurrentRushActor());
}

void RaceManager::entry(AbstractRacer* pRacer) {
    if (mRacerNum == static_cast< u32 >(mRacer.size())) {
        mRacer.grow();
    }

    mRacer[mRacerNum++] = pRacer;
}

void RaceManager::entry(PlayerRacer* pRacer) {
    MR::getRailArg0WithInit(pRacer, &mCurrentRace);
    mBestTime = MR::getRaceBestTime(mCurrentRace);
    mPlayerRacer = pRacer;
    Vec3f endDir, endPos;
    MR::calcRailEndPointPosAndDirection(&endPos, &endDir, pRacer);
    MR::makeMtxFrontUp(&mGoalInvMtx, endDir, Vec3f(0.0f, 1.0f, 0.0f));
    MR::setMtxTrans(mGoalInvMtx, endPos.x, endPos.y, endPos.z);
    mGoalInvMtx.invert(mGoalInvMtx);
    entry(static_cast< AbstractRacer* >(pRacer));
}

void RaceManager::entry(AbstractAudience* pAudience) {
    if (mAudienceNum == static_cast< u32 >(mAudience.size())) {
        mAudience.grow();
    }

    mAudience[mAudienceNum++] = pAudience;
}

bool RaceManager::isGoal(const AbstractRacer* pRacer) const {
    Vec3f currInnProd, prevInnProd;
    mGoalInvMtx.mult(pRacer->mCurrPosition, currInnProd);
    mGoalInvMtx.mult(pRacer->mPrevPosition, prevInnProd);
    return prevInnProd.z < 0.0f && currInnProd.z > 0.0f;
}

u32 RaceManager::getRank(const AbstractRacer* pRacer) const {
    u32 rank = mRacerNum;

    for (int i = 0; i < mRacerNum; i++) {
        if (mRacer[i] == pRacer) {
            rank = i;
        }
    }

    return rank + 1;
}

void RaceManager::renewTime() {
    mTime = getNerveStep();

    if (mTime < mBestTime) {
        MR::setRaceBestTime(mCurrentRace, mTime);
    }
}

namespace RaceManagerFunction {
    void entryRacerOthers(AbstractRacer* pRacer) {
        MR::createSceneObj(SceneObj_RaceManager);
        ::getRaceManager()->entry(pRacer);
    }

    void entryAudience(AbstractAudience* pAudience) {
        MR::createSceneObj(SceneObj_RaceManager);
        ::getRaceManager()->entry(pAudience);
    }

    void entryRacerPlayer(PlayerRacer* pRacer) {
        MR::createSceneObj(SceneObj_RaceManager);
        ::getRaceManager()->entry(pRacer);
    }

    void startRaceWithWipe() {
        ::getRaceManager()->startWithWipe();
    }

    void startRaceImmediately() {
        ::getRaceManager()->startImmediately();
    }

    u32 getRaceRank() {
        return ::getRaceManager()->mRank;
    }

    u32 getRaceTime() {
        return ::getRaceManager()->mTime;
    }

    const char* getRaceName(int index) {
        return ::getRaceStructData(index).mName;
    }

    const char* getRaceMessageId(int index) {
        return ::getRaceStructData(index).mMessageId;
    }

    s32 getRaceId(const char* pGalaxyName, s32 scenarioNo) {
        for (u32 i = 0; i < ARRAY_SIZE(::sRaceStruct); i++) {
            const RaceStructData& rRaceStructData = ::getRaceStructData(i);

            if (MR::isEqualString(rRaceStructData.mGalaxyName, pGalaxyName) && rRaceStructData.mScenarioNo == scenarioNo) {
                return i;
            }
        }

        return -1;
    }

    bool hasPowerStarRaceScenario(int index) {
        return MR::isOnGameEventFlagPowerStarSuccess(::getRaceStructData(index).mGalaxyName, ::getRaceStructData(index).mScenarioNo);
    }
};  // namespace RaceManagerFunction

