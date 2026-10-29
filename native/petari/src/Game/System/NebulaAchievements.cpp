#include "Game/System/NebulaAchievements.hpp"
#include <JSystem/JUtility/JUTNativeLog.hpp>

#ifdef TARGET_PC
#include "Game/System/GalaxyInfoTable.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/Util/JMapInfo.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstdio>
#include <revolution/os.h>

namespace {
    // Glyphs of the picture font (MR::addPictureFontTag's codes).
    const NebulaAchievementIcon cIconPowerStar = {'7', nullptr, false, nullptr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    const NebulaAchievementIcon cIconStarBit = {';', nullptr, false, nullptr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    const NebulaAchievementIcon cIconSilverStar = {'J', nullptr, false, nullptr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    const NebulaAchievementIcon cIconPurpleCoin = {'N', nullptr, false, nullptr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    const NebulaAchievementIcon cIconGreenStar = {'O', nullptr, false, nullptr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    const NebulaAchievementIcon cIconCrown = {'P', nullptr, false, nullptr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    const NebulaAchievementIcon cIconRedStar = {'S', nullptr, false, nullptr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    const NebulaAchievementIcon cIconRainbowStar = {'W', nullptr, false, nullptr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    const NebulaAchievementIcon cIconQuestion = {'Z', nullptr, false, nullptr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    const NebulaAchievementIcon cIconOneUp = {'a', nullptr, false, nullptr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    const NebulaAchievementIcon cIconHungryLuma = {'c', nullptr, false, nullptr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    const NebulaAchievementIcon cIconComet = {'e', nullptr, false, nullptr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    const NebulaAchievementIcon cIconGrandStar = {'K', nullptr, false, nullptr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    const NebulaAchievementIcon cIconLifeMushroom = {'b', nullptr, false, nullptr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    const NebulaAchievementIcon cIconMarioHead = {'B', nullptr, false, nullptr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    const NebulaAchievementIcon cIconCoin = {'A', nullptr, false, nullptr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f};

    // Textures: the domes the Grand Stars open, as the Observatory's map shows them (the Terrace, Fountain, Kitchen,
    // Bedroom, Engine Room and Garden), the faces of the file select's Mii choices, and the storybook's front cover
    // (the right half of its cover texture).
    const NebulaAchievementIcon cTextureIcons[] = {
        {0, "MapDomeIcon.arc", true, "mymapdome1.tpl", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {0, "MapDomeIcon.arc", true, "mymapdome2.tpl", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {0, "MapDomeIcon.arc", true, "mymapdome3.tpl", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {0, "MapDomeIcon.arc", true, "mymapdome4.tpl", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {0, "MapDomeIcon.arc", true, "mymapdome5.tpl", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {0, "MapDomeIcon.arc", true, "mymapdome6.tpl", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {0, "MiiIcon.arc", true, "mymiimario.tpl", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.2f},
        {0, "MiiIcon.arc", true, "mymiiluigi.tpl", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.2f},
        {0, "MiiIcon.arc", true, "mymiipeach.tpl", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.2f},
        {0, "PictureBookTexture.arc", false, "PictureBookCoverFront.bti", 0.5f, 0.0f, 1.0f, 1.0f, 208.0f, 240.0f},
    };
    const NebulaAchievementIcon* const cIconDome = &cTextureIcons[0];
    const NebulaAchievementIcon& cIconMario = cTextureIcons[6];
    const NebulaAchievementIcon& cIconLuigi = cTextureIcons[7];
    const NebulaAchievementIcon& cIconPeach = cTextureIcons[8];
    const NebulaAchievementIcon& cIconStorybook = cTextureIcons[9];

    // Every achievement in the catalogue's order (galaxy::achievements), with its group (0 story, 1 challenge,
    // 2 extra) and its icon.
    // Shown on the board: always; never until unlocked (a secret); or once another is unlocked.
    enum { Shown, Secret, AfterHalfway };

    struct Entry {
        const char* mId;
        s32 mGroup;
        const NebulaAchievementIcon* mIcon;
        s32 mShown;
    };

    const Entry cEntries[] = {
        {"first_star", 0, &cIconPowerStar},
        {"gateway", 0, &cIconDome[0]},
        {"robot_reactor", 0, &cIconDome[1]},
        {"star_reactor", 0, &cIconDome[2]},
        {"airship_armada", 0, &cIconDome[3]},
        {"dark_matter", 0, &cIconDome[4]},
        {"lava_reactor", 0, &cIconDome[5]},
        {"luigi_found", 0, &cIconLuigi},
        {"halfway", 0, &cIconPowerStar},
        {"ending", 0, &cIconPeach},
        {"storybook", 0, &cIconStorybook},
        {"stars120", 1, &cIconCrown, AfterHalfway},
        {"luigi120", 1, &cIconLuigi, AfterHalfway},
        {"grandfinale", 1, &cIconRainbowStar},
        {"comets", 1, &cIconComet},
        {"daredevil", 1, &cIconComet},
        {"cosmic", 1, &cIconMario},
        {"speedy", 1, &cIconComet},
        {"purplecoins", 1, &cIconPurpleCoin},
        {"hidden", 1, &cIconQuestion},
        {"greenstars", 1, &cIconGreenStar},
        {"trials", 1, &cIconSilverStar},
        {"lumas", 1, &cIconHungryLuma},
        {"bank", 1, &cIconStarBit},
        {"lives", 1, &cIconOneUp},
        {"no_miss_ending", 1, &cIconGrandStar, Secret},
        {"no_miss_120", 1, &cIconLifeMushroom, Secret},
        {"first_comet", 2, &cIconComet},
        {"red_star", 2, &cIconRedStar},
        {"first_luma", 2, &cIconHungryLuma},
        {"luigi_play", 2, &cIconLuigi},
        {"bits1000", 2, &cIconStarBit},
        {"lives50", 2, &cIconMarioHead},
        {"coins999", 2, &cIconCoin},
    };
    const s32 cEntryNum = sizeof(cEntries) / sizeof(cEntries[0]);

    const s32 cQueueMax = 40;
    const s32 cBatchMin = 3;  // this many unlocked together show as one pop-up
    const s32 cNameLength = 64;

    struct Waiting {
        wchar_t mName[cNameLength];
        const NebulaAchievementIcon* mIcon;
    };

    Waiting sQueue[cQueueMax];
    s32 sQueued = 0;
    s32 sUpdateFrames = 0;

    // galaxy::stats' names, in NebulaStat's order.
    const char* const cStatNames[NebulaStat_Num] = {
        "jumps", "spins", "coins", "star_bits", "lives_lost", "game_overs", "one_ups", "play_seconds",
    };
    u32 sStatWaiting[NebulaStat_Num];
    bool sFileInPlay = false;  // the stage is not the title and file select
    s32 sPlayFrames = 0;
    const s32 cStatFlushFrames = 60;
    s32 sStatFlushFrames = 0;

    // The star collected in this galaxy (sent with its get sequence) while the save does not hold it yet: the hub's
    // result sequence records it (GameSequenceFunction::updateGameDataAndSequenceAfterStageResultSequence), after the
    // scene that follows it has started. Unlocks count it as owned from its get sequence on.
    const char* sPendingGalaxy = nullptr;
    s32 sPendingStar = 0;
    s32 sEvaluatedStars = -1;  // the star total evaluate() last saw (pending included)

    void readPendingStar() {
        sPendingGalaxy = nullptr;
        sPendingStar = 0;
        if (!GameSequenceFunction::hasStageResultSequence()) {
            return;
        }
        const char* pGalaxy = GameSequenceFunction::getClearedStageName();
        const s32 star = GameSequenceFunction::getClearedPowerStarId();
        if (pGalaxy != nullptr && pGalaxy[0] != 0 && star > 0 && !GameDataFunction::hasPowerStar(pGalaxy, star)) {
            sPendingGalaxy = pGalaxy;
            sPendingStar = star;
        }
    }

    bool hasStar(const char* pGalaxy, s32 star) {
        return GameDataFunction::hasPowerStar(pGalaxy, star) ||
               (sPendingGalaxy != nullptr && sPendingStar == star && MR::isEqualString(sPendingGalaxy, pGalaxy));
    }

    // Grand Star n: its event flag (SpecialStarGrand1-7), which the save derives from its galaxy's first star.
    bool hasGrandStar(s32 n) {
        static const char* const cGrandStarGalaxies[] = {"HeavensDoorGalaxy",    "TriLegLv1Galaxy",      "KoopaBattleVs1Galaxy",
                                                         "KoopaJrShipLv1Galaxy", "KoopaBattleVs2Galaxy", "FloaterOtaKingGalaxy",
                                                         "KoopaBattleVs3Galaxy"};
        char flag[24];
        snprintf(flag, sizeof(flag), "SpecialStarGrand%d", n);
        return GameDataFunction::isOnGameEventFlag(flag) || hasStar(cGrandStarGalaxies[n - 1], 1);
    }

    s32 countStars() {
        return MR::getPowerStarNum() + (sPendingGalaxy != nullptr ? 1 : 0);
    }

    const Entry* findEntry(const char* pId) {
        for (s32 i = 0; i < cEntryNum; i++) {
            if (MR::isEqualString(cEntries[i].mId, pId)) {
                return &cEntries[i];
            }
        }
        return nullptr;
    }

    // Its name in the played language: NebulaText's Achievement.<id>, or Achievement.<id>.Luigi on a Luigi file.
    s32 getEntryName(wchar_t* pDst, s32 capacity, const char* pId) {
        char textId[64];
        snprintf(textId, sizeof(textId), "Achievement.%s.Luigi", pId);
        if (GameDataFunction::isDataMario() || !NebulaText::has(textId)) {
            snprintf(textId, sizeof(textId), "Achievement.%s", pId);
        }
        return NebulaText::get(pDst, capacity, textId);
    }

    // Waits for its pop-up, with its name in the played language.
    void enqueue(const Entry& rEntry) {
        if (sQueued >= cQueueMax) {
            return;
        }
        Waiting& rWaiting = sQueue[sQueued++];
        getEntryName(rWaiting.mName, cNameLength, rEntry.mId);
        rWaiting.mIcon = rEntry.mIcon;
    }

    // Asks the host to unlock what is met; a new one waits for its pop-up.
    void tryUnlock(bool met, const char* pId) {
        const Entry* pEntry = met ? findEntry(pId) : nullptr;
        if (pEntry == nullptr || sQueued >= cQueueMax || __NebulaAchievement(pId, nullptr, 0) == 0) {
            return;
        }
        enqueue(*pEntry);
        JUTNativeLog("achievement: %s\n", pId);
    }

    void tryUnlock(const char* pId) {
        tryUnlock(true, pId);
    }

    struct Galaxies {
        bool mAnyComet;
        bool mComets;
        bool mDaredevil;
        bool mCosmic;
        bool mSpeedy;
        bool mPurpleCoins;
        bool mHidden;
        s32 mCometsOwned, mCometsTotal;
        s32 mDaredevilOwned, mDaredevilTotal;
        s32 mCosmicOwned, mCosmicTotal;
        s32 mSpeedyOwned, mSpeedyTotal;
        s32 mPurpleCoinsOwned, mPurpleCoinsTotal;
        s32 mHiddenOwned, mHiddenTotal;
    };

    // Every galaxy of the map (the game's galaxy table, as GalaxyMap lists them): its Prankster Comet by kind, its
    // purple coin star and its hidden stars. An "every" goal is met only when it found something to check.
    Galaxies checkGalaxies() {
        Galaxies result = {false, true, true, true, true, true, true, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        s32 comets = 0, daredevil = 0, cosmic = 0, speedy = 0, purpleCoins = 0, hidden = 0;
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
                const bool owned = galaxy.hasPowerStar(comet);
                const char* pKind = galaxy.getCometName(comet);
                comets++;
                result.mCometsOwned += owned ? 1 : 0;
                result.mComets = result.mComets && owned;
                result.mAnyComet = result.mAnyComet || owned;
                if (pKind != nullptr && MR::isEqualString(pKind, "Dark")) {
                    daredevil++;
                    result.mDaredevilOwned += owned ? 1 : 0;
                    result.mDaredevil = result.mDaredevil && owned;
                } else if (pKind != nullptr && MR::isEqualString(pKind, "Ghost")) {
                    cosmic++;
                    result.mCosmicOwned += owned ? 1 : 0;
                    result.mCosmic = result.mCosmic && owned;
                } else if (pKind != nullptr && MR::isEqualString(pKind, "Red")) {
                    speedy++;
                    result.mSpeedyOwned += owned ? 1 : 0;
                    result.mSpeedy = result.mSpeedy && owned;
                }
            }
            const s32 purple = galaxy.getCoin100CometScenarioNo();
            if (purple > 0) {
                const bool owned = galaxy.hasPowerStar(purple);
                purpleCoins++;
                result.mPurpleCoinsOwned += owned ? 1 : 0;
                result.mPurpleCoins = result.mPurpleCoins && owned;
                result.mAnyComet = result.mAnyComet || owned;
            }
            // Every scenario with a star: getScenarioNum leaves out the hidden ones (they come last).
            for (s32 scenario = 1; scenario <= galaxy.getPowerStarNum(); scenario++) {
                if (galaxy.isHiddenStar(scenario)) {
                    hidden++;
                    const bool owned = galaxy.hasPowerStar(scenario);
                    result.mHiddenOwned += owned ? 1 : 0;
                    result.mHidden = result.mHidden && owned;
                }
            }
        }
        result.mComets = result.mComets && comets > 0;
        result.mDaredevil = result.mDaredevil && daredevil > 0;
        result.mCosmic = result.mCosmic && cosmic > 0;
        result.mSpeedy = result.mSpeedy && speedy > 0;
        result.mPurpleCoins = result.mPurpleCoins && purpleCoins > 0;
        result.mHidden = result.mHidden && hidden > 0;
        result.mCometsTotal = comets;
        result.mDaredevilTotal = daredevil;
        result.mCosmicTotal = cosmic;
        result.mSpeedyTotal = speedy;
        result.mPurpleCoinsTotal = purpleCoins;
        result.mHiddenTotal = hidden;
        return result;
    }

    // The Hungry Lumas that make a galaxy: the alms with a real target (the others are capped at 32767).
    void countLumasFed(s32* pFed, s32* pFeedable) {
        *pFed = 0;
        *pFeedable = 0;
        for (s32 i = 0; i < 16; i++) {
            const s32 target = GameDataFunction::getStarPieceNumMaxGivingToTicoSeed(i);
            if (target >= 32767) {
                continue;
            }
            (*pFeedable)++;
            if (GameDataFunction::getStarPieceNumGivingToTicoSeed(i) >= target) {
                (*pFed)++;
            }
        }
    }

    s32 countTrials() {
        return (hasStar("TamakoroExLv2Galaxy", 1) ? 1 : 0) + (hasStar("SurfingLv2Galaxy", 1) ? 1 : 0) + (hasStar("CubeBubbleExLv2Galaxy", 1) ? 1 : 0);
    }

    s32 countGreenStars() {
        s32 count = 0;
        for (s32 i = 1; i <= 3; i++) {
            char flag[32];
            snprintf(flag, sizeof(flag), "SpecialStarGreen%d", i);
            count += GameDataFunction::isOnGameEventFlag(flag) ? 1 : 0;
        }
        return count;
    }

    s32 atMost(s32 value, s32 most) {
        return value < most ? value : most;
    }

    // Lives and Star Bits change mid-stage; checked at each stage start and once a second.
    void checkCounters() {
        const s32 starBits = GameDataFunction::getStockedStarPieceNum();
        tryUnlock(starBits >= 1000, "bits1000");
        tryUnlock(starBits >= 9999, "bank");
        tryUnlock(GameDataFunction::getPlayerLeft() >= 50, "lives50");
        tryUnlock(GameDataFunction::getPlayerLeft() >= 99, "lives");  // the most the game keeps
        // The coins collected over every play (galaxy::stats): a galaxy's own counter starts again at each visit.
        tryUnlock(NebulaAchievements::getStat(NebulaStat_Coins) >= 999, "coins999");
    }
};  // namespace

namespace NebulaAchievements {
    void evaluate() {
        sFileInPlay = !MR::isEqualStageName("FileSelect");
        // A file in play: the title's stage has none, and a new file meets nothing.
        readPendingStar();
        const s32 stars = countStars();
        sEvaluatedStars = stars;
        if (stars <= 0) {
            return;
        }
        const s32 queuedBefore = sQueued;
        const bool mario = GameDataFunction::isDataMario();

        // The story, in the order it is played.
        tryUnlock("first_star");
        tryUnlock(hasGrandStar(1), "gateway");
        tryUnlock(hasGrandStar(2), "robot_reactor");
        tryUnlock(hasGrandStar(3), "star_reactor");
        tryUnlock(hasGrandStar(4), "airship_armada");
        tryUnlock(hasGrandStar(5), "dark_matter");
        tryUnlock(hasGrandStar(6), "lava_reactor");
        tryUnlock(GameDataFunction::isOnGameEventFlag("SpecialStarLuigiRescued"), "luigi_found");
        tryUnlock(stars >= 60, "halfway");
        tryUnlock(GameDataFunction::isOnGameEventFlag("ViewNormalEnding"), "ending");
        tryUnlock(GameDataFunction::getPictureBookChapterAlreadyRead() >= 9, "storybook");

        // The extras, met along the way.
        const Galaxies galaxies = checkGalaxies();
        tryUnlock(galaxies.mAnyComet, "first_comet");
        tryUnlock(GameDataFunction::isOnGameEventFlag("SpecialStarRed1"), "red_star");
        tryUnlock(!mario, "luigi_play");

        s32 fed, feedable;
        countLumasFed(&fed, &feedable);
        const bool anyFed = fed > 0;
        const bool allFed = fed == feedable;
        tryUnlock(anyFed, "first_luma");

        // The challenges.
        tryUnlock(stars >= 120 && mario, "stars120");
        tryUnlock(stars >= 120 && !mario, "luigi120");
        tryUnlock(hasStar("PeachCastleFinalGalaxy", 1), "grandfinale");
        tryUnlock(galaxies.mComets, "comets");
        tryUnlock(galaxies.mDaredevil, "daredevil");
        tryUnlock(galaxies.mCosmic, "cosmic");
        tryUnlock(galaxies.mSpeedy, "speedy");
        tryUnlock(galaxies.mPurpleCoins, "purplecoins");
        tryUnlock(galaxies.mHidden, "hidden");
        tryUnlock(GameDataFunction::isOnGameEventFlag("SpecialStarGreenAll"), "greenstars");
        tryUnlock(countTrials() == 3, "trials");
        tryUnlock(allFed && anyFed, "lumas");

        // Without a life lost: the save's own count of misses (GameDataHolder's MissNum, raised by every life lost and
        // every game over) still at zero. Bowser's Galaxy Reactor opens at 60 Power Stars.
        const bool noMiss = GameDataFunction::getCurrentGameDataHolder()->getPlayerMissNum() == 0;
        tryUnlock(noMiss && stars >= 60 && GameDataFunction::isOnGameEventFlag("ViewNormalEnding"), "no_miss_ending");
        checkCounters();
        flushStats();
        sUpdateFrames = 0;

        // Several at once (a save from before Nebula's achievements, imported or updated): one pop-up for them all.
        const s32 unlocked = sQueued - queuedBefore;
        if (unlocked >= cBatchMin) {
            sQueued = queuedBefore;
            Waiting& rWaiting = sQueue[sQueued++];
            wchar_t pattern[cNameLength];
            NebulaText::get(pattern, cNameLength, "AchievementsUnlocked");
            char digits[8];
            snprintf(digits, sizeof(digits), "%d", unlocked);
            s32 length = 0;
            for (const wchar_t* q = pattern; *q != 0 && length + 1 < cNameLength; q++) {
                if (q[0] == L'{' && q[1] == L'n' && q[2] == L'}') {
                    for (const char* d = digits; *d != 0 && length + 1 < cNameLength; d++) {
                        rWaiting.mName[length++] = static_cast< wchar_t >(*d);
                    }
                    q += 2;
                } else {
                    rWaiting.mName[length++] = *q;
                }
            }
            rWaiting.mName[length] = 0;
            rWaiting.mIcon = &cIconCrown;
        }
    }

    void update() {
        // Play time: the game's 60 steps a second, in any stage with a file (not the title and file select).
        if (sFileInPlay && ++sPlayFrames >= 60) {
            sPlayFrames = 0;
            countStat(NebulaStat_PlaySeconds, 1);
        }
        if (++sStatFlushFrames >= cStatFlushFrames) {
            flushStats();
        }
        if (++sUpdateFrames < 60) {
            return;
        }
        sUpdateFrames = 0;
        // A star recorded or collected since the last evaluation (a get sequence without the game scene's, the hub's
        // result sequence): everything it may complete, now.
        readPendingStar();
        if (countStars() != sEvaluatedStars) {
            evaluate();
            return;
        }
        if (countStars() > 0) {
            checkCounters();
        }
    }

    const wchar_t* getWaiting(const NebulaAchievementIcon** ppIcon) {
        if (sQueued == 0) {
            return nullptr;
        }
        *ppIcon = sQueue[0].mIcon;
        return sQueue[0].mName;
    }

    void popWaiting() {
        if (sQueued == 0) {
            return;
        }
        for (s32 i = 1; i < sQueued; i++) {
            sQueue[i - 1] = sQueue[i];
        }
        sQueued--;
    }

    bool isMoreWaiting() {
        return sQueued > 1;
    }

    void showForTest(const char* pId) {
        if (const Entry* pEntry = findEntry(pId)) {
            enqueue(*pEntry);
        } else {
            JUTNativeLog("test: no achievement %s\n", pId);
        }
    }

    s32 getNum() {
        return cEntryNum;
    }

    const char* getId(s32 index) {
        return index >= 0 && index < cEntryNum ? cEntries[index].mId : nullptr;
    }

    s32 getName(wchar_t* pDst, s32 capacity, const char* pId) {
        return getEntryName(pDst, capacity, pId);
    }

    s32 getGroup(s32 index) {
        return index >= 0 && index < cEntryNum ? cEntries[index].mGroup : 0;
    }

    bool isShownLocked(s32 index) {
        if (index < 0 || index >= cEntryNum) {
            return false;
        }
        switch (cEntries[index].mShown) {
        case Secret:
            return false;
        case AfterHalfway:
            return __NebulaAchievementDate("halfway") != 0;
        default:
            return true;
        }
    }

    void onGrandStarGet() {
        // Cosmic Immortal: Bowser beaten again once every Power Star is in hand, and never a life lost. The star a
        // Grand Star sequence awards is saved when it ends, so here the file's count and stars are those from before:
        // his own already held means a rematch.
        if (!MR::isEqualStageName("KoopaBattleVs3Galaxy") || MR::getPowerStarNum() < 120 ||
            !GameDataFunction::hasPowerStar("KoopaBattleVs3Galaxy", 1)) {
            return;
        }
        tryUnlock(GameDataFunction::getCurrentGameDataHolder()->getPlayerMissNum() == 0, "no_miss_120");
    }

    const NebulaAchievementIcon* getIcon(s32 index) {
        return index >= 0 && index < cEntryNum ? cEntries[index].mIcon : nullptr;
    }

    s32 getIconTextureNum() {
        return sizeof(cTextureIcons) / sizeof(cTextureIcons[0]);
    }

    const NebulaAchievementIcon* getIconTexture(s32 index) {
        return index >= 0 && index < getIconTextureNum() ? &cTextureIcons[index] : nullptr;
    }

    s32 findIconTexture(const NebulaAchievementIcon* pIcon) {
        const s32 index = static_cast< s32 >(pIcon - cTextureIcons);
        return index >= 0 && index < getIconTextureNum() ? index : -1;
    }

    s32 getUnlockDate(s32 index) {
        return index >= 0 && index < cEntryNum ? __NebulaAchievementDate(cEntries[index].mId) : 0;
    }

    void getFileProgress(NebulaFileProgress* pProgress) {
        const Galaxies galaxies = checkGalaxies();
        pProgress->mMario = GameDataFunction::isDataMario();
        pProgress->mPowerStars = MR::getPowerStarNum();
        pProgress->mPowerStarsMax = GameDataFunction::getPowerStarNumMax();
        pProgress->mStarBits = GameDataFunction::getStockedStarPieceNum();
        pProgress->mLives = GameDataFunction::getPlayerLeft();
        pProgress->mChapters = GameDataFunction::getPictureBookChapterAlreadyRead();
        pProgress->mComets = galaxies.mCometsOwned;
        pProgress->mCometsMax = galaxies.mCometsTotal;
        pProgress->mDaredevil = galaxies.mDaredevilOwned;
        pProgress->mDaredevilMax = galaxies.mDaredevilTotal;
        pProgress->mCosmic = galaxies.mCosmicOwned;
        pProgress->mCosmicMax = galaxies.mCosmicTotal;
        pProgress->mSpeedy = galaxies.mSpeedyOwned;
        pProgress->mSpeedyMax = galaxies.mSpeedyTotal;
        pProgress->mPurpleCoins = galaxies.mPurpleCoinsOwned;
        pProgress->mPurpleCoinsMax = galaxies.mPurpleCoinsTotal;
        pProgress->mHidden = galaxies.mHiddenOwned;
        pProgress->mHiddenMax = galaxies.mHiddenTotal;
        pProgress->mGreenStars = countGreenStars();
        pProgress->mTrials = countTrials();
        countLumasFed(&pProgress->mLumasFed, &pProgress->mLumasFedMax);
    }

    bool getProgress(s32 index, const NebulaFileProgress& rFile, s32* pCurrent, s32* pGoal) {
        const char* pId = getId(index);
        s32 current = -1;
        s32 goal = 0;
        if (pId == nullptr) {
            return false;
        } else if (MR::isEqualString(pId, "halfway")) {
            current = rFile.mPowerStars, goal = 60;
        } else if (MR::isEqualString(pId, "stars120") && rFile.mMario) {
            current = rFile.mPowerStars, goal = 120;
        } else if (MR::isEqualString(pId, "luigi120") && !rFile.mMario) {
            current = rFile.mPowerStars, goal = 120;
        } else if (MR::isEqualString(pId, "storybook")) {
            current = rFile.mChapters, goal = 9;
        } else if (MR::isEqualString(pId, "comets")) {
            current = rFile.mComets, goal = rFile.mCometsMax;
        } else if (MR::isEqualString(pId, "daredevil")) {
            current = rFile.mDaredevil, goal = rFile.mDaredevilMax;
        } else if (MR::isEqualString(pId, "cosmic")) {
            current = rFile.mCosmic, goal = rFile.mCosmicMax;
        } else if (MR::isEqualString(pId, "speedy")) {
            current = rFile.mSpeedy, goal = rFile.mSpeedyMax;
        } else if (MR::isEqualString(pId, "purplecoins")) {
            current = rFile.mPurpleCoins, goal = rFile.mPurpleCoinsMax;
        } else if (MR::isEqualString(pId, "hidden")) {
            current = rFile.mHidden, goal = rFile.mHiddenMax;
        } else if (MR::isEqualString(pId, "greenstars")) {
            current = rFile.mGreenStars, goal = 3;
        } else if (MR::isEqualString(pId, "trials")) {
            current = rFile.mTrials, goal = 3;
        } else if (MR::isEqualString(pId, "lumas")) {
            current = rFile.mLumasFed, goal = rFile.mLumasFedMax;
        } else if (MR::isEqualString(pId, "bank")) {
            current = rFile.mStarBits, goal = 9999;
        } else if (MR::isEqualString(pId, "bits1000")) {
            current = rFile.mStarBits, goal = 1000;
        } else if (MR::isEqualString(pId, "lives")) {
            current = rFile.mLives, goal = 99;
        } else if (MR::isEqualString(pId, "lives50")) {
            current = rFile.mLives, goal = 50;
        } else if (MR::isEqualString(pId, "coins999")) {
            const u64 coins = getStat(NebulaStat_Coins);
            current = coins > 999 ? 999 : static_cast< s32 >(coins), goal = 999;
        }
        if (current < 0 || goal <= 1) {
            return false;
        }
        *pCurrent = atMost(current, goal);
        *pGoal = goal;
        return true;
    }

    void countStat(s32 stat, s32 num) {
        if (stat >= 0 && stat < NebulaStat_Num && num > 0) {
            sStatWaiting[stat] += static_cast< u32 >(num);
        }
    }

    void flushStats() {
        sStatFlushFrames = 0;
        for (s32 i = 0; i < NebulaStat_Num; i++) {
            if (sStatWaiting[i] != 0) {
                __NebulaStatAdd(cStatNames[i], sStatWaiting[i]);
                sStatWaiting[i] = 0;
            }
        }
    }

    u64 getStat(s32 stat) {
        if (stat < 0 || stat >= NebulaStat_Num) {
            return 0;
        }
        return __NebulaStatGet(cStatNames[stat]) + sStatWaiting[stat];
    }
};  // namespace NebulaAchievements
#endif
