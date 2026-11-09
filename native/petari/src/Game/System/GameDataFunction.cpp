#include "Game/System/NebulaHost.hpp"
#include "Game/System/NebulaCalendar.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/GameDataGalaxyStorage.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/System/GameDataTemporaryInGalaxy.hpp"
#include "Game/System/GameEventFlagTable.hpp"
#include "Game/System/GameSequenceDirector.hpp"
#include "Game/System/GameSequenceProgress.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/SaveDataHandleSequence.hpp"
#include "Game/System/ScenarioDataParser.hpp"
#include "Game/System/UserFile.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StringUtil.hpp"

namespace {
    SaveDataHandleSequence* getSaveDataHandleSequence() {
        return SingletonHolder< GameSystem >::get()->mSequenceDirector->mSaveDataHandleSequence;
    }

    GameDataTemporaryInGalaxy* getGameDataTemporaryInGalaxy() {
        return SingletonHolder< GameSystem >::get()->mSequenceDirector->mGameDataTemporaryInGalaxy;
    }

    GameSequenceProgress* getGameSequenceProgress() {
        return SingletonHolder< GameSystem >::get()->mSequenceDirector->mGameSequenceProgress;
    }
};  // namespace

namespace GameDataFunction {
    bool isDataMario() {
        return getCurrentGameDataHolder()->isDataMario();
    }

    bool hasPowerStar(const char* pGalaxyName, s32 scenarioNo) {
        return getCurrentGameDataHolder()->hasPowerStar(pGalaxyName, scenarioNo);
    }

    bool hasGrandStar(int id) {
        return getCurrentGameDataHolder()->hasGrandStar(id);
    }

    void setGameFlagPowerStarSuccess(const char* pGalaxyName, s32 scenarioNo, bool isOwned) {
        getCurrentGameDataHolder()->setPowerStar(pGalaxyName, scenarioNo, isOwned);
        if (isOwned) {
            NebulaMods::fire(NEBULA_MOD_EVENT_STAR_GET, scenarioNo, nullptr, pGalaxyName);
        }
    }

    GameDataSomeScenarioAccessor makeGalaxyScenarioAccessor(const char* pGalaxyName, s32 scenarioNo) {
        return getCurrentGameDataHolder()->makeGalaxyScenarioAccessor(pGalaxyName, scenarioNo);
    }

    bool isOnGalaxyScenarioFlagAlreadyVisited(const char* pGalaxyName, s32 scenarioNo) {
        return getCurrentGameDataHolder()->isOnGalaxyScenarioFlagAlreadyVisited(pGalaxyName, scenarioNo);
    }

    void onGalaxyScenarioFlagAlreadyVisited(const char* pGalaxyName, s32 scenarioNo) {
        getCurrentGameDataHolder()->onGalaxyScenarioFlagAlreadyVisited(pGalaxyName, scenarioNo);
    }

    bool isAppearGalaxy(const char* pGalaxyName) {
        return getCurrentGameDataHolder()->isAppearGalaxy(pGalaxyName);
    }

    s32 calcCurrentPowerStarNum() {
        return getCurrentGameDataHolder()->calcCurrentPowerStarNum();
    }

    s32 getPowerStarNumOwned(const char* pGalaxyName) {
        return getCurrentGameDataHolder()->getPowerStarNumOwned(pGalaxyName);
    }

    const char* findStarGalaxyName(const char* pGalaxyName) {
        const ScenarioData* pData = ScenarioDataFunction::getScenarioDataParser()->getScenarioData(pGalaxyName);
        if (pData == nullptr || getCurrentGameDataHolder()->mAllGalaxyStorage->findIndex(pData->mGalaxyName) < 0) {
            return nullptr;
        }
        return pData->mGalaxyName;
    }

    s32 getPowerStarNumMax() {
        s32 powerStarNumMax = 0;

        for (ScenarioDataIter iter = MR::makeBeginScenarioDataIter(); !iter.isEnd(); iter.goNext()) {
            powerStarNumMax += iter.makeAccessor().getPowerStarNum();
        }

        return powerStarNumMax;
    }

    s32 getPictureBookChapterCanRead() {
        return getCurrentGameDataHolder()->getPictureBookChapterCanRead();
    }

    u32 getPictureBookChapterAlreadyRead() {
        return getCurrentGameDataHolder()->getPictureBookChapterAlreadyRead();
    }

    void setPictureBookChapterAlreadyRead(int chapterAlreadyRead) {
        getCurrentGameDataHolder()->setPictureBookChapterAlreadyRead(chapterAlreadyRead);
    }

    void onGameEventFlag(const char* pName) {
        getCurrentGameDataHolder()->tryOnGameEventFlag(pName);
    }

    bool canOnGameEventFlag(const char* pName) {
        return getCurrentGameDataHolder()->canOnGameEventFlag(pName);
    }

    bool canOnAndIsOffGameEventFlag(const char* pName) {
        return getCurrentGameDataHolder()->canOnGameEventFlag(pName) && !getCurrentGameDataHolder()->isOnGameEventFlag(pName);
    }

    bool isOnGameEventFlag(const char* pName) {
        return getCurrentGameDataHolder()->isOnGameEventFlag(pName);
    }

    u32 getGameEventValue(const char* pName) {
        return getCurrentGameDataHolder()->getGameEventValue(pName);
    }

    void setGameEventValue(const char* pName, u16 value) {
        getCurrentGameDataHolder()->setGameEventValue(pName, value);
    }

    bool isOnGameEventValueForBit(const char* pName, int index) {
        return getCurrentGameDataHolder()->isOnGameEventValueForBit(pName, index);
    }

    void setGameEventValueForBit(const char* pName, int index, bool isOn) {
        getCurrentGameDataHolder()->setGameEventValueForBit(pName, index, isOn);
    }

    s32 setupAlreadyDoneFlag(const char* pName, const TableIter& rIter, u32* pIsDone) {
        return ::getGameDataTemporaryInGalaxy()->setupAlreadyDoneFlag(pName, rIter, pIsDone);
    }

    void updateAlreadyDoneFlag(int index, u32 isDone) {
        ::getGameDataTemporaryInGalaxy()->updateAlreadyDoneFlag(index, isDone);
    }

    void restoreGalaxyCometStatus(int cometId, u16* pCometStatus, u16* pPastSecond) {
        if (cometId > 6) {
            *pCometStatus = 0;
            *pPastSecond = 0;
            return;
        }
        // The save keeps no seconds of a comet's state, so the past seconds start at 0 (they were left unset).
        *pPastSecond = 0;
        *pCometStatus = getCurrentGameDataHolder()->getGameEventValue(MR::FormattedName("Comet%1dStatus", cometId).c_str());
    }

    void setRaceBestTime(const char* pRaceName, u32 bestTime) {
        getCurrentGameDataHolder()->setRaceBestTime(pRaceName, bestTime);
    }

    s32 getRaceBestTime(const char* pRaceName) {
        return getCurrentGameDataHolder()->getRaceBestTime(pRaceName);
    }

    void addMissPoint(int num) {
        getCurrentGameDataHolder()->addMissPoint(num);
    }

    void resetMissPoint() {
        getCurrentGameDataHolder()->resetMissPoint();
    }

    bool isPointCollectForLetter() {
        return getCurrentGameDataHolder()->isPointCollectForLetter();
    }

    void incPlayerMissNum() {
        getCurrentGameDataHolder()->incPlayerMissNum();
    }

    s32 registerStorageSpinDriverPathDrawRange(const char* pGalaxyName, int scenarioNo, int zoneId, int spinDriverId, f32* pDrawRange) {
        return getCurrentGameDataHolder()->setupSpinDriverPathStorage(pGalaxyName, scenarioNo, zoneId, spinDriverId, pDrawRange);
    }

    void updateStorageSpinDriverPathDrawRange(const char* pGalaxyName, int scenarioNo, int spinDriverIndex, f32 drawRange) {
        getCurrentGameDataHolder()->updateSpinDriverPathStorage(pGalaxyName, scenarioNo, spinDriverIndex, drawRange);
    }

    u32 getStarPieceNumGivingToTicoSeed(int index) {
        return getCurrentGameDataHolder()->getStarPieceNumGivingToTicoSeed(index);
    }

    u32 getStarPieceNumMaxGivingToTicoSeed(int index) {
        return getCurrentGameDataHolder()->getStarPieceNumMaxGivingToTicoSeed(index);
    }

    void addStarPieceGivingToTicoSeed(int index, int num) {
        getCurrentGameDataHolder()->addStarPieceGivingToTicoSeed(index, num);
    }

    bool isPassedStoryEvent(const char* pStoryEventName) {
        return getCurrentGameDataHolder()->isPassedStoryEvent(pStoryEventName);
    }

    void followStoryEventByName(const char* pStoryEventName) {
        if (getCurrentGameDataHolder()->isPassedStoryEvent(pStoryEventName)) {
            return;
        }

        getCurrentGameDataHolder()->followStoryEventByName(pStoryEventName);
    }

    s32 getPlayerLeft() {
        return getCurrentGameDataHolder()->getPlayerLeft();
    }

    void addPlayerLeft(int num) {
        getCurrentGameDataHolder()->addPlayerLeft(num);
    }

    bool isPlayerLeftSupply() {
        return getCurrentGameDataHolder()->isPlayerLeftSupply();
    }

    void offPlayerLeftSupply() {
        getCurrentGameDataHolder()->offPlayerLeftSupply();
    }

    s32 getStockedStarPieceNum() {
        return getCurrentGameDataHolder()->getStockedStarPieceNum();
    }

    s32 getStarPieceNum() {
        return ::getGameDataTemporaryInGalaxy()->getStarPieceNum();
    }

    void addStarPiece(int num) {
        ::getGameDataTemporaryInGalaxy()->addStarPiece(num);
    }

    void setLast1upStarPieceNum(int last1upStarPieceNum) {
        ::getGameDataTemporaryInGalaxy()->setLast1upStarPieceNum(last1upStarPieceNum);
    }

    s32 getLast1upStarPieceNum() {
        return ::getGameDataTemporaryInGalaxy()->mLast1upStarPieceNum;
    }

    void resetAllGameData() {
        getCurrentGameDataHolder()->resetAllData();
    }

    bool isOnJustGameEventFlag(const char* pName) {
        return getCurrentGameDataHolder()->isOnGameEventFlag(pName) && !getSceneStartGameDataHolder()->isOnGameEventFlag(pName);
    }

    bool canOnJustGameEventFlag(const char* pName) {
        return getCurrentGameDataHolder()->canOnGameEventFlag(pName) && !getSceneStartGameDataHolder()->canOnGameEventFlag(pName);
    }

    bool isOnJustAppearGalaxy(const char* pGalaxyName) {
        return getCurrentGameDataHolder()->isAppearGalaxy(pGalaxyName) && !getSceneStartGameDataHolder()->isAppearGalaxy(pGalaxyName);
    }

    bool isEqualJustPowerStarNum(int powerStarNum) {
        return getCurrentGameDataHolder()->calcCurrentPowerStarNum() == powerStarNum &&
               getSceneStartGameDataHolder()->calcCurrentPowerStarNum() != powerStarNum;
    }

    GameDataHolder* getCurrentGameDataHolder() {
        return ::getSaveDataHandleSequence()->getCurrentUserFile()->mGameDataHolder;
    }

    GameDataHolder* getSceneStartGameDataHolder() {
        return ::getSaveDataHandleSequence()->getBackupUserFile()->mGameDataHolder;
    }

    s32 calcTicoGalaxyNum(const GameDataHolder* pGameDataHolder) {
        s32 ticoGalaxyNum = 0;
        s32 exclamationGalaxyNum = GameEventFlagTable::calcExclamationGalaxyNum();

        for (s32 i = 0; i < exclamationGalaxyNum; i++) {
            const char* pExclamationGalaxyName = GameEventFlagTable::getExclamationGalaxyNameFromIndex(i);

            if (pExclamationGalaxyName != nullptr && pGameDataHolder->isAppearGalaxy(pExclamationGalaxyName)) {
                ticoGalaxyNum++;
            }
        }

        return ticoGalaxyNum;
    }

    s32 calcGrandStarNum(const GameDataHolder* pGameDataHolder) {
        s32 grandStarNum = 0;

        for (int i = 1; i <= 7; i++) {
            if (pGameDataHolder->hasGrandStar(i)) {
                grandStarNum++;
            }
        }

        return grandStarNum;
    }

    void resetGameDataGoToGalaxyFirst() {
        ::getGameDataTemporaryInGalaxy()->resetPlayerRestartIdInfo();
        ::getGameDataTemporaryInGalaxy()->mIsRetry = false;
        ::getGameDataTemporaryInGalaxy()->clearAlreadyDoneFlag();
        ::getGameDataTemporaryInGalaxy()->resetStageResultStarPieceParam();
        ::getGameDataTemporaryInGalaxy()->resetStageResultCoinParam();
    }

    void resetGameDataGoToGalaxyRetry() {
        ::getGameDataTemporaryInGalaxy()->mIsRetry = true;
    }

    void onCompleteEndingCurrentPlayer() {
        ::getSaveDataHandleSequence()->getCurrentUserFile()->onCompleteEndingCurrentPlayer();
    }

    bool isOnCompleteEndingMarioAndLuigi() {
        return ::getSaveDataHandleSequence()->getCurrentUserFile()->isOnCompleteEndingMarioAndLuigi();
    }

    bool isLuigiLeftSupply() {
        return ::getGameSequenceProgress()->isLuigiLeftSupply();
    }

    void offLuigiLeftSupply() {
        ::getGameSequenceProgress()->offLuigiLeftSupply();
    }

    bool isOnGameEventFlagNormalComet(const char* pGalaxyName) {
        MR::FormattedName eventFlagName;

        eventFlagName.format("NormalComet%s", pGalaxyName);

        return getCurrentGameDataHolder()->isOnGameEventFlag(eventFlagName.c_str());
    }

    bool isOnGameEventFlagCoin100Comet(const char* pGalaxyName) {
        MR::FormattedName eventFlagName;

        eventFlagName.format("Coin100Comet%s", pGalaxyName);

        return getCurrentGameDataHolder()->isOnGameEventFlag(eventFlagName.c_str());
    }

    void updateGalaxyCometStatus(int cometId, u16 cometStatus, u16 /*pastSecond*/) {
        // The save keeps the game's six domes' comets: a mod's further ones start over each play.
        if (cometId > 6) {
            return;
        }
        getCurrentGameDataHolder()->setGameEventValue(MR::FormattedName("Comet%1dStatus", cometId).c_str(), cometStatus);
    }

    void addStockedStarPiece(int num) {
        getCurrentGameDataHolder()->addStockedStarPiece(num);

        if (getCurrentGameDataHolder()->getStockedStarPieceNum() != NebulaHost::setting(NEBULA_PATCH_MAX_STAR_BITS, 9999)) {
            return;
        }

        const char* pEventFlagName = "StarPieceCounterStop";

        if (getCurrentGameDataHolder()->isOnGameEventFlag(pEventFlagName)) {
            return;
        }

        getCurrentGameDataHolder()->tryOnGameEventFlag(pEventFlagName);
    }

    bool isPowerStarLeftInCometOnly() {
        const char* pGalaxyName;

        for (ScenarioDataIter iter = MR::makeBeginScenarioDataIter(); !iter.isEnd(); iter.goNext()) {
            GalaxyStatusAccessor accessor = iter.makeAccessor();

            if (accessor.getPowerStarNum() == 0) {
                continue;
            }

            for (int i = 1; i <= accessor.getPowerStarNum(); i++) {
                if (accessor.isCometStar(i)) {
                    continue;
                }

                pGalaxyName = accessor.getName();

                if (getCurrentGameDataHolder()->hasPowerStar(pGalaxyName, i)) {
                    continue;
                }

                if (GameEventFlagTable::isPowerStarType(accessor.getName(), i, "SpecialStarFinalChallenge")) {
                    continue;
                }

                return false;
            }
        }

        return true;
    }

    s32 calcGreenStarNum(const GameDataHolder* pGameDataHolder) {
        s32 greenStarNum = 0;
        for (int i = 1; i <= 3; i++) {
            if (pGameDataHolder->isOnGameEventFlag(MR::FormattedName("SpecialStarGreen%1d", i).c_str())) {
                greenStarNum++;
            }
        }

        return greenStarNum;
    }
};  // namespace GameDataFunction
