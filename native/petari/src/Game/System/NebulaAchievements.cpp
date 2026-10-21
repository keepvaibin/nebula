#include "Game/System/NebulaAchievements.hpp"

#ifdef TARGET_PC
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/GalaxyInfoTable.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/JMapInfo.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <revolution/os.h>

namespace {
    const s32 cShowFrames = 180;
    const s32 cQueueMax = 8;

    wchar_t sQueue[cQueueMax][64];
    s32 sQueued = 0;
    s32 sShowFrames = 0;

    // Asks the host to unlock; a new one waits for the banner.
    void tryUnlock(const char* pId) {
        if (sQueued >= cQueueMax) {
            return;
        }
        u16 name[48];
        if (__NebulaAchievement(pId, name, 48) == 0) {
            return;
        }
        wchar_t* pLine = sQueue[sQueued++];
        const wchar_t* pPrefix = L"Achievement unlocked: ";
        s32 length = 0;
        for (const wchar_t* p = pPrefix; *p != 0 && length < 63; p++) {
            pLine[length++] = *p;
        }
        for (s32 i = 0; i < 48 && name[i] != 0 && length < 63; i++) {
            pLine[length++] = static_cast< wchar_t >(name[i]);
        }
        pLine[length] = 0;
        OSReport("achievement: %s\n", pId);
    }

    // Every galaxy of the map (the game's galaxy table, as GalaxyMap lists them), its comet, purple coin and hidden
    // stars.
    // Each "every" goal counts what it checked: a goal with nothing to check (no file loaded) is not met.
    void checkGalaxies(bool* pComets, bool* pPurpleCoins, bool* pHidden) {
        s32 comets = 0, purpleCoins = 0, hidden = 0;
        JMapInfo galaxies;
        galaxies.attach(&cGalaxyInfoTable);
        for (s32 i = 0; i < galaxies.getNumEntries(); i++) {
            const char* pPane = nullptr;
            const char* pName = nullptr;
            if (!galaxies.getValue(i, "MapPaneName", &pPane) || !galaxies.getValue(i, "name", &pName) || pName == nullptr ||
                MR::isEqualString(pPane, "") || MR::isEqualString(pPane, "dummy")) {
                continue;
            }
            const GalaxyStatusAccessor galaxy = MR::makeGalaxyStatusAccessor(pName);
            const s32 comet = galaxy.getNormalCometScenarioNo();
            if (comet > 0) {
                comets++;
                *pComets = *pComets && galaxy.hasPowerStar(comet);
            }
            const s32 purple = galaxy.getCoin100CometScenarioNo();
            if (purple > 0) {
                purpleCoins++;
                *pPurpleCoins = *pPurpleCoins && galaxy.hasPowerStar(purple);
            }
            for (s32 scenario = 1; scenario <= galaxy.getScenarioNum(); scenario++) {
                if (galaxy.isHiddenStar(scenario)) {
                    hidden++;
                    *pHidden = *pHidden && galaxy.hasPowerStar(scenario);
                }
            }
        }
        *pComets = *pComets && comets > 0;
        *pPurpleCoins = *pPurpleCoins && purpleCoins > 0;
        *pHidden = *pHidden && hidden > 0;
    }
};  // namespace

namespace NebulaAchievements {
    void evaluate() {
        // A file in play: the title's stage has none, and a new file meets nothing.
        const s32 stars = MR::getPowerStarNum();
        if (stars <= 0) {
            return;
        }
        if (GameDataFunction::isOnGameEventFlag("ViewNormalEnding")) {
            tryUnlock("ending");
        }
        if (stars >= 120) {
            tryUnlock(GameDataFunction::isDataMario() ? "stars120" : "luigi120");
        }
        if (GameDataFunction::hasPowerStar("PeachCastleFinalGalaxy", 1)) {
            tryUnlock("grandfinale");
        }
        bool comets = true, purpleCoins = true, hidden = true;
        checkGalaxies(&comets, &purpleCoins, &hidden);
        if (comets) {
            tryUnlock("comets");
        }
        if (purpleCoins) {
            tryUnlock("purplecoins");
        }
        if (hidden) {
            tryUnlock("hidden");
        }
        // The Hungry Lumas that make a galaxy: the alms with a real target (the others are capped at 32767).
        bool fed = true;
        for (s32 i = 0; i < 16; i++) {
            const s32 target = GameDataFunction::getStarPieceNumMaxGivingToTicoSeed(i);
            if (target < 32767 && GameDataFunction::getStarPieceNumGivingToTicoSeed(i) < target) {
                fed = false;
            }
        }
        if (fed) {
            tryUnlock("lumas");
        }
        if (GameDataFunction::getPictureBookChapterAlreadyRead() >= 9) {
            tryUnlock("storybook");
        }
        if (GameDataFunction::getStockedStarPieceNum() >= 9999) {
            tryUnlock("bank");
        }
        if (GameDataFunction::getPlayerLeft() >= 99) {
            tryUnlock("lives");
        }
    }

    void update() {
        if (sShowFrames > 0) {
            if (--sShowFrames == 0) {
                MR::disappearInformationMessage();
            }
            return;
        }
        if (sQueued == 0 || MR::isDemoActive() || !MR::isDeadInformationMessage()) {
            return;
        }
        MR::appearInformationMessage(sQueue[0], false);
        sShowFrames = cShowFrames;
        for (s32 i = 1; i < sQueued; i++) {
            for (s32 c = 0; c < 64; c++) {
                sQueue[i - 1][c] = sQueue[i][c];
            }
        }
        sQueued--;
    }
};  // namespace NebulaAchievements
#endif
