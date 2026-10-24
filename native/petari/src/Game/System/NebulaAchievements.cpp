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
    struct Entry {
        const char* mId;
        s32 mGroup;
        const NebulaAchievementIcon* mIcon;
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
        {"stars120", 1, &cIconCrown},
        {"luigi120", 1, &cIconLuigi},
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
        {"first_comet", 2, &cIconComet},
        {"red_star", 2, &cIconRedStar},
        {"first_luma", 2, &cIconHungryLuma},
        {"luigi_play", 2, &cIconLuigi},
        {"bits1000", 2, &cIconStarBit},
    };
    const s32 cEntryNum = sizeof(cEntries) / sizeof(cEntries[0]);

    const s32 cQueueMax = 32;
    const s32 cNameLength = 64;

    struct Waiting {
        wchar_t mName[cNameLength];
        const NebulaAchievementIcon* mIcon;
    };

    Waiting sQueue[cQueueMax];
    s32 sQueued = 0;
    s32 sUpdateFrames = 0;

    const Entry* findEntry(const char* pId) {
        for (s32 i = 0; i < cEntryNum; i++) {
            if (MR::isEqualString(cEntries[i].mId, pId)) {
                return &cEntries[i];
            }
        }
        return nullptr;
    }

    // Waits for its pop-up, with its name in the played language (NebulaText's Achievement.<id>).
    void enqueue(const Entry& rEntry) {
        if (sQueued >= cQueueMax) {
            return;
        }
        char textId[48];
        snprintf(textId, sizeof(textId), "Achievement.%s", rEntry.mId);
        Waiting& rWaiting = sQueue[sQueued++];
        NebulaText::get(rWaiting.mName, cNameLength, textId);
        rWaiting.mIcon = rEntry.mIcon;
    }

    // Asks the host to unlock what is met; a new one waits for its pop-up.
    void tryUnlock(bool met, const char* pId) {
        const Entry* pEntry = met ? findEntry(pId) : nullptr;
        if (pEntry == nullptr || sQueued >= cQueueMax || __NebulaAchievement(pId, nullptr, 0) == 0) {
            return;
        }
        enqueue(*pEntry);
        OSReport("achievement: %s\n", pId);
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
        tryUnlock(starBits >= 1000, "bits1000");
        tryUnlock(starBits >= 9999, "bank");
        tryUnlock(GameDataFunction::getPlayerLeft() >= 99, "lives");
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
        tryUnlock("first_star");
        tryUnlock(GameDataFunction::isOnGameEventFlag("SpecialStarGrand1"), "gateway");
        tryUnlock(GameDataFunction::isOnGameEventFlag("SpecialStarGrand2"), "robot_reactor");
        tryUnlock(GameDataFunction::isOnGameEventFlag("SpecialStarGrand3"), "star_reactor");
        tryUnlock(GameDataFunction::isOnGameEventFlag("SpecialStarGrand4"), "airship_armada");
        tryUnlock(GameDataFunction::isOnGameEventFlag("SpecialStarGrand5"), "dark_matter");
        tryUnlock(GameDataFunction::isOnGameEventFlag("SpecialStarGrand6"), "lava_reactor");
        tryUnlock(GameDataFunction::isOnGameEventFlag("SpecialStarLuigiRescued"), "luigi_found");
        tryUnlock(stars >= 60, "halfway");
        tryUnlock(GameDataFunction::isOnGameEventFlag("ViewNormalEnding"), "ending");
        tryUnlock(GameDataFunction::getPictureBookChapterAlreadyRead() >= 9, "storybook");

        // The extras, met along the way.
        const Galaxies galaxies = checkGalaxies();
        tryUnlock(galaxies.mAnyComet, "first_comet");
        tryUnlock(GameDataFunction::isOnGameEventFlag("SpecialStarRed1"), "red_star");
        tryUnlock(!mario, "luigi_play");

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
        tryUnlock(anyFed, "first_luma");

        // The challenges.
        tryUnlock(stars >= 120 && mario, "stars120");
        tryUnlock(stars >= 120 && !mario, "luigi120");
        tryUnlock(GameDataFunction::hasPowerStar("PeachCastleFinalGalaxy", 1), "grandfinale");
        tryUnlock(galaxies.mComets, "comets");
        tryUnlock(galaxies.mDaredevil, "daredevil");
        tryUnlock(galaxies.mCosmic, "cosmic");
        tryUnlock(galaxies.mSpeedy, "speedy");
        tryUnlock(galaxies.mPurpleCoins, "purplecoins");
        tryUnlock(galaxies.mHidden, "hidden");
        tryUnlock(GameDataFunction::isOnGameEventFlag("SpecialStarGreenAll"), "greenstars");
        tryUnlock(GameDataFunction::hasPowerStar("TamakoroExLv2Galaxy", 1) && GameDataFunction::hasPowerStar("SurfingLv2Galaxy", 1) &&
                      GameDataFunction::hasPowerStar("CubeBubbleExLv2Galaxy", 1),
                  "trials");
        tryUnlock(allFed && anyFed, "lumas");
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
            OSReport("test: no achievement %s\n", pId);
        }
    }

    s32 getNum() {
        return cEntryNum;
    }

    const char* getId(s32 index) {
        return index >= 0 && index < cEntryNum ? cEntries[index].mId : nullptr;
    }

    s32 getGroup(s32 index) {
        return index >= 0 && index < cEntryNum ? cEntries[index].mGroup : 0;
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
};  // namespace NebulaAchievements
#endif
