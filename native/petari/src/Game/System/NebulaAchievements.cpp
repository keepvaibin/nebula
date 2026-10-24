#include "Game/System/NebulaAchievements.hpp"

#ifdef TARGET_PC
#include "Game/System/GalaxyInfoTable.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/Util/JMapInfo.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstdio>
#include <revolution/os.h>

namespace {
    // The picture font's codes (MR::addPictureFontTag) for each pop-up's icon.
    const s32 cIconPowerStar = '7';
    const s32 cIconStarBit = ';';
    const s32 cIconMario = 'B';
    const s32 cIconSilverStar = 'J';
    const s32 cIconGrandStar = 'K';
    const s32 cIconLuigi = 'L';
    const s32 cIconPurpleCoin = 'N';
    const s32 cIconGreenStar = 'O';
    const s32 cIconCrown = 'P';
    const s32 cIconRedStar = 'S';
    const s32 cIconRainbowStar = 'W';
    const s32 cIconPeach = 'X';
    const s32 cIconLetter = 'Y';
    const s32 cIconQuestion = 'Z';
    const s32 cIconOneUp = 'a';
    const s32 cIconHungryLuma = 'c';
    const s32 cIconComet = 'e';

    const s32 cQueueMax = 32;
    const s32 cNameLength = 64;

    struct Waiting {
        wchar_t mName[cNameLength];
        s32 mIcon;
    };

    Waiting sQueue[cQueueMax];
    s32 sQueued = 0;
    s32 sUpdateFrames = 0;

    // Asks the host to unlock; a new one waits for its pop-up, with its name in the played language (NebulaText's
    // Achievement.<id>).
    void tryUnlock(const char* pId, s32 icon) {
        if (sQueued >= cQueueMax || __NebulaAchievement(pId, nullptr, 0) == 0) {
            return;
        }
        char textId[48];
        snprintf(textId, sizeof(textId), "Achievement.%s", pId);
        Waiting& rWaiting = sQueue[sQueued++];
        NebulaText::get(rWaiting.mName, cNameLength, textId);
        rWaiting.mIcon = icon;
        OSReport("achievement: %s\n", pId);
    }

    void tryUnlock(bool met, const char* pId, s32 icon) {
        if (met) {
            tryUnlock(pId, icon);
        }
    }

    struct Galaxies {
        bool mAnyComet;
        bool mComets;
        bool mDaredevil;
        bool mCosmic;
        bool mSpeedy;
        bool mPurpleCoins;
        bool mHidden;
    };

    // Every galaxy of the map (the game's galaxy table, as GalaxyMap lists them): its Prankster Comet by kind, its
    // purple coin star and its hidden stars. An "every" goal is met only when it found something to check.
    Galaxies checkGalaxies() {
        Galaxies result = {false, true, true, true, true, true, true};
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
                result.mComets = result.mComets && owned;
                result.mAnyComet = result.mAnyComet || owned;
                if (pKind != nullptr && MR::isEqualString(pKind, "Dark")) {
                    daredevil++;
                    result.mDaredevil = result.mDaredevil && owned;
                } else if (pKind != nullptr && MR::isEqualString(pKind, "Ghost")) {
                    cosmic++;
                    result.mCosmic = result.mCosmic && owned;
                } else if (pKind != nullptr && MR::isEqualString(pKind, "Red")) {
                    speedy++;
                    result.mSpeedy = result.mSpeedy && owned;
                }
            }
            const s32 purple = galaxy.getCoin100CometScenarioNo();
            if (purple > 0) {
                const bool owned = galaxy.hasPowerStar(purple);
                purpleCoins++;
                result.mPurpleCoins = result.mPurpleCoins && owned;
                result.mAnyComet = result.mAnyComet || owned;
            }
            for (s32 scenario = 1; scenario <= galaxy.getScenarioNum(); scenario++) {
                if (galaxy.isHiddenStar(scenario)) {
                    hidden++;
                    result.mHidden = result.mHidden && galaxy.hasPowerStar(scenario);
                }
            }
        }
        result.mComets = result.mComets && comets > 0;
        result.mDaredevil = result.mDaredevil && daredevil > 0;
        result.mCosmic = result.mCosmic && cosmic > 0;
        result.mSpeedy = result.mSpeedy && speedy > 0;
        result.mPurpleCoins = result.mPurpleCoins && purpleCoins > 0;
        result.mHidden = result.mHidden && hidden > 0;
        return result;
    }

    // Lives and Star Bits change mid-stage; checked at each stage start and once a second.
    void checkCounters() {
        const s32 starBits = GameDataFunction::getStockedStarPieceNum();
        tryUnlock(starBits >= 1000, "bits1000", cIconStarBit);
        tryUnlock(starBits >= 9999, "bank", cIconStarBit);
        tryUnlock(GameDataFunction::getPlayerLeft() >= 99, "lives", cIconOneUp);
    }
};  // namespace

namespace NebulaAchievements {
    void evaluate() {
        // A file in play: the title's stage has none, and a new file meets nothing.
        const s32 stars = MR::getPowerStarNum();
        if (stars <= 0) {
            return;
        }
        const bool mario = GameDataFunction::isDataMario();

        // The story, in the order it is played.
        tryUnlock("first_star", cIconPowerStar);
        tryUnlock(GameDataFunction::isOnGameEventFlag("SpecialStarGrand1"), "gateway", cIconGrandStar);
        tryUnlock(GameDataFunction::isOnGameEventFlag("SpecialStarGrand2"), "robot_reactor", cIconGrandStar);
        tryUnlock(GameDataFunction::isOnGameEventFlag("SpecialStarGrand3"), "star_reactor", cIconGrandStar);
        tryUnlock(GameDataFunction::isOnGameEventFlag("SpecialStarGrand4"), "airship_armada", cIconGrandStar);
        tryUnlock(GameDataFunction::isOnGameEventFlag("SpecialStarGrand5"), "dark_matter", cIconGrandStar);
        tryUnlock(GameDataFunction::isOnGameEventFlag("SpecialStarGrand6"), "lava_reactor", cIconGrandStar);
        tryUnlock(GameDataFunction::isOnGameEventFlag("SpecialStarLuigiRescued"), "luigi_found", cIconLuigi);
        tryUnlock(stars >= 60, "halfway", cIconPowerStar);
        tryUnlock(GameDataFunction::isOnGameEventFlag("ViewNormalEnding"), "ending", cIconPeach);
        tryUnlock(GameDataFunction::getPictureBookChapterAlreadyRead() >= 9, "storybook", cIconLetter);

        // The extras, met along the way.
        const Galaxies galaxies = checkGalaxies();
        tryUnlock(galaxies.mAnyComet, "first_comet", cIconComet);
        tryUnlock(GameDataFunction::isOnGameEventFlag("SpecialStarRed1"), "red_star", cIconRedStar);
        tryUnlock(!mario, "luigi_play", cIconLuigi);

        // The Hungry Lumas that make a galaxy: the alms with a real target (the others are capped at 32767).
        bool anyFed = false, allFed = true;
        for (s32 i = 0; i < 16; i++) {
            const s32 target = GameDataFunction::getStarPieceNumMaxGivingToTicoSeed(i);
            if (target >= 32767) {
                continue;
            }
            if (GameDataFunction::getStarPieceNumGivingToTicoSeed(i) >= target) {
                anyFed = true;
            } else {
                allFed = false;
            }
        }
        tryUnlock(anyFed, "first_luma", cIconHungryLuma);

        // The challenges.
        tryUnlock(stars >= 120 && mario, "stars120", cIconCrown);
        tryUnlock(stars >= 120 && !mario, "luigi120", cIconLuigi);
        tryUnlock(GameDataFunction::hasPowerStar("PeachCastleFinalGalaxy", 1), "grandfinale", cIconRainbowStar);
        tryUnlock(galaxies.mComets, "comets", cIconComet);
        tryUnlock(galaxies.mDaredevil, "daredevil", cIconComet);
        tryUnlock(galaxies.mCosmic, "cosmic", cIconMario);
        tryUnlock(galaxies.mSpeedy, "speedy", cIconComet);
        tryUnlock(galaxies.mPurpleCoins, "purplecoins", cIconPurpleCoin);
        tryUnlock(galaxies.mHidden, "hidden", cIconQuestion);
        tryUnlock(GameDataFunction::isOnGameEventFlag("SpecialStarGreenAll"), "greenstars", cIconGreenStar);
        tryUnlock(GameDataFunction::hasPowerStar("TamakoroExLv2Galaxy", 1) && GameDataFunction::hasPowerStar("SurfingLv2Galaxy", 1) &&
                      GameDataFunction::hasPowerStar("CubeBubbleExLv2Galaxy", 1),
                  "trials", cIconSilverStar);
        tryUnlock(allFed && anyFed, "lumas", cIconHungryLuma);
        checkCounters();
        sUpdateFrames = 0;
    }

    void update() {
        if (++sUpdateFrames < 60) {
            return;
        }
        sUpdateFrames = 0;
        if (MR::getPowerStarNum() > 0) {
            checkCounters();
        }
    }

    const wchar_t* getWaiting(s32* pIcon) {
        if (sQueued == 0) {
            return nullptr;
        }
        *pIcon = sQueue[0].mIcon;
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
};  // namespace NebulaAchievements
#endif
