#include "Game/System/NebulaAchievements.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include <nebula_module.h>

#include "Game/System/GalaxyInfoTable.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/NebulaHost.hpp"
#include <cstring>
#include "Game/Util/Tunable.hpp"

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
        {0, "MapDomeIcon", true, "mymapdome1.tpl", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {0, "MapDomeIcon", true, "mymapdome2.tpl", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {0, "MapDomeIcon", true, "mymapdome3.tpl", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {0, "MapDomeIcon", true, "mymapdome4.tpl", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {0, "MapDomeIcon", true, "mymapdome5.tpl", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {0, "MapDomeIcon", true, "mymapdome6.tpl", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {0, "MiiIcon", true, "mymiimario.tpl", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.2f},
        {0, "MiiIcon", true, "mymiiluigi.tpl", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.2f},
        {0, "MiiIcon", true, "mymiipeach.tpl", 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.2f},
        {0, "PictureBookTexture", false, "PictureBookCoverFront.bti", 0.5f, 0.0f, 1.0f, 1.0f, 208.0f, 240.0f},
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
        {"wayfarer", 1, &cIconPowerStar},
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

    NEBULA_TUNABLE(s32, cBatchMin, 3);  // this many unlocked together show as one pop-up

    struct Waiting {
        char16_t* mName;  // in the root arena: it waits across scenes
        const NebulaAchievementIcon* mIcon;
    };

    MR::GrowBuffer< Waiting > sQueue;
    s32 sQueued = 0;

    Waiting& pushWaiting(u32 nameUnits) {
        sQueue.room(sQueued + 1);
        Waiting& rWaiting = sQueue[sQueued++];
        rWaiting.mName = new (NebulaArena::root(), 0) char16_t[nameUnits];
        return rWaiting;
    }
    s32 sUpdateFrames = 0;

    // galaxy::stats' names, in NebulaStat's order.
    const char* const cStatNames[NebulaStat_Num] = {
        "jumps", "spins", "coins", "star_bits", "lives_lost", "game_overs", "one_ups", "play_seconds",
    };
    u32 sStatWaiting[NebulaStat_Num];
    bool sFileInPlay = false;  // the stage is not the title and file select
    s32 sPlayFrames = 0;
    NEBULA_TUNABLE(s32, cStatFlushFrames, 60);
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
        return GameDataFunction::isOnGameEventFlag(MR::FormattedName("SpecialStarGrand%d", n).c_str()) || hasStar(cGrandStarGalaxies[n - 1], 1);
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
    void getEntryTextId(MR::FormattedName* pTextId, const char* pId) {
        pTextId->format("Achievement.%s.Luigi", pId);
        if (GameDataFunction::isDataMario() || !NebulaText::has(pTextId->c_str())) {
            pTextId->format("Achievement.%s", pId);
        }
    }

    // The running mods' achievements: the host's, the same for the whole run, after the game's own.
    const NebulaAchievementIcon& cIconMod = cIconPowerStar;
    s32 sModNum = -1;

    s32 modNum() {
        if (sModNum < 0) {
            const s32 num = NebulaHost::modAchievementCount();
            sModNum = num > 0 ? num : 0;
        }
        return sModNum;
    }

    bool modAt(s32 index, const char** ppKey, const char** ppName, const char** ppDescription, bool* pHidden) {
        const s32 mod = index - cEntryNum;
        return mod >= 0 && mod < modNum() && NebulaHost::modAchievement(mod, ppKey, ppName, ppDescription, pHidden);
    }

    bool isModKey(const char* pId) {
        return pId != nullptr && pId[0] == 'm' && pId[1] == 'o' && pId[2] == 'd' && pId[3] == ':';
    }

    // The host's UTF-8 as the game's 16-bit text (past 16 bits, a surrogate pair); the units it takes, its end included.
    s32 fromUtf8(char16_t* pDst, const char* pSrc) {
        const u8* p = reinterpret_cast< const u8* >(pSrc);
        s32 length = 0;
        while (*p != 0) {
            const u32 lead = *p++;
            s32 more = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
            u32 c = lead & (more == 3 ? 0x07 : more == 2 ? 0x0F : more == 1 ? 0x1F : 0x7F);
            for (; more > 0 && (*p & 0xC0) == 0x80; more--) {
                c = (c << 6) | (*p++ & 0x3F);
            }
            // Cut short, a stray continuation byte, past Unicode, or a surrogate's own code: the replacement character.
            if (more != 0 || (lead >= 0x80 && lead < 0xC0) || lead >= 0xF8 || c > 0x10FFFF || (c >= 0xD800 && c < 0xE000)) {
                c = 0xFFFD;
            }
            if (c >= 0x10000) {
                if (pDst != nullptr) {
                    pDst[length] = static_cast< char16_t >(0xD800 + ((c - 0x10000) >> 10));
                    pDst[length + 1] = static_cast< char16_t >(0xDC00 + ((c - 0x10000) & 0x3FF));
                }
                length += 2;
            } else {
                if (pDst != nullptr) {
                    pDst[length] = static_cast< char16_t >(c);
                }
                length++;
            }
        }
        if (pDst != nullptr) {
            pDst[length] = 0;
        }
        return length + 1;
    }

    void textFromUtf8(MR::Text16& rText, const char* pSrc) {
        fromUtf8(rText.room(fromUtf8(nullptr, pSrc)), pSrc);
    }

    // A mod's achievement waits for its pop-up, with its name.
    void enqueueMod(s32 mod) {
        const char* pName = nullptr;
        if (!NebulaHost::modAchievement(mod, nullptr, &pName, nullptr, nullptr) || pName == nullptr) {
            return;
        }
        Waiting& rWaiting = pushWaiting(static_cast< u32 >(fromUtf8(nullptr, pName)));
        fromUtf8(rWaiting.mName, pName);
        rWaiting.mIcon = &cIconMod;
    }

    // Waits for its pop-up, with its name in the played language.
    void enqueue(const Entry& rEntry) {
        MR::FormattedName textId;
        getEntryTextId(&textId, rEntry.mId);
        const s32 units = NebulaText::length(textId.c_str()) + 1;
        Waiting& rWaiting = pushWaiting(units);
        NebulaText::get(rWaiting.mName, units, textId.c_str());
        rWaiting.mIcon = rEntry.mIcon;
    }

    bool sRosalinaWelcome = false;

    // Asks the host to unlock what is met; a new one waits for its pop-up.
    void tryUnlock(bool met, const char* pId) {
        const Entry* pEntry = met ? findEntry(pId) : nullptr;
        if (pEntry == nullptr || NebulaHost::achievement(pId, nullptr, 0) == 0) {
            return;
        }
        enqueue(*pEntry);
        nebula_log("achievement: %s\n", pId);
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
        bool mWayfarer;
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
        Galaxies result = {false, true, true, true, true, true, true, true, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        s32 comets = 0, daredevil = 0, cosmic = 0, speedy = 0, purpleCoins = 0, hidden = 0, galaxyNum = 0;
        Table galaxies;
        galaxies.attach(NebulaDataTables::getGalaxyInfoTable());
        for (s32 i = 0; i < galaxies.getNumEntries(); i++) {
            const char* pPane = nullptr;
            const char* pName = nullptr;
            if (!galaxies.getValue(i, "MapPaneName", &pPane) || !galaxies.getValue(i, "name", &pName) || pName == nullptr ||
                MR::isEqualString(pPane, "") || MR::isEqualString(pPane, "dummy")) {
                continue;
            }
            const GalaxyStatusAccessor galaxy = MR::makeGalaxyStatusAccessor(pName);
            // Galactic Wayfarer: a galaxy with stars has one of them in hand, the one being collected included.
            bool anyStar = galaxy.getPowerStarNum() <= 0;
            for (s32 scenario = 1; scenario <= galaxy.getPowerStarNum() && !anyStar; scenario++) {
                anyStar = hasStar(pName, scenario);
            }
            result.mWayfarer = result.mWayfarer && anyStar;
            galaxyNum++;
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
        result.mWayfarer = result.mWayfarer && galaxyNum > 0;
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
            count += GameDataFunction::isOnGameEventFlag(MR::FormattedName("SpecialStarGreen%d", i).c_str()) ? 1 : 0;
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
        // Rosalina's talk in the Observatory, or a save already past it.
        tryUnlock(sRosalinaWelcome || hasGrandStar(2), "gateway");
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
        tryUnlock(galaxies.mWayfarer, "wayfarer");
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
            for (s32 i = queuedBefore; i < sQueued; i++) {
                delete[] sQueue[i].mName;
            }
            sQueued = queuedBefore;
            const s32 patternUnits = NebulaText::length("AchievementsUnlocked") + 1;
            MR::Text16 pattern;
            NebulaText::get(pattern.room(patternUnits), patternUnits, "AchievementsUnlocked");
            char digits[12];  // any s32
            nebula_snprintf(digits, sizeof(digits), "%d", unlocked);
            const s32 digitCount = static_cast< s32 >(strlen(digits));
            // {n} is the count.
            s32 units = 1;
            for (const char16_t* q = pattern.c_str(); *q != 0; q++) {
                if (q[0] == u'{' && q[1] == u'n' && q[2] == u'}') {
                    units += digitCount;
                    q += 2;
                } else {
                    units++;
                }
            }
            Waiting& rWaiting = pushWaiting(units);
            s32 length = 0;
            for (const char16_t* q = pattern.c_str(); *q != 0; q++) {
                if (q[0] == u'{' && q[1] == u'n' && q[2] == u'}') {
                    for (const char* d = digits; *d != 0; d++) {
                        rWaiting.mName[length++] = static_cast< char16_t >(*d);
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
        // A mod's achievement unlocked (its plugin, or the event it waits for): its pop-up, now.
        for (s32 mod = NebulaHost::modAchievementUnlocked(); mod >= 0; mod = NebulaHost::modAchievementUnlocked()) {
            enqueueMod(mod);
        }
        // Play time: the game's 60 steps a second, in any stage with a file (not the title and file select).
        if (sFileInPlay && ++sPlayFrames >= NebulaFrameClock::stepsOfSeconds(1)) {
            sPlayFrames = 0;
            countStat(NebulaStat_PlaySeconds, 1);
        }
        if (++sStatFlushFrames >= cStatFlushFrames) {
            flushStats();
        }
        if (++sUpdateFrames < NebulaFrameClock::stepsOfSeconds(1)) {
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

    const char16_t* getWaiting(const NebulaAchievementIcon** ppIcon) {
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
        delete[] sQueue[0].mName;
        for (s32 i = 1; i < sQueued; i++) {
            sQueue[i - 1] = sQueue[i];
        }
        sQueued--;
    }

    bool isMoreWaiting() {
        return sQueued > 1;
    }

    s32 getNum() {
        return cEntryNum + modNum();
    }

    s32 getModNum() {
        return modNum();
    }

    bool isMod(s32 index) {
        return index >= cEntryNum && index < cEntryNum + modNum();
    }

    void getModDescription(MR::Text16& rText, s32 index) {
        const char* pDescription = nullptr;
        textFromUtf8(rText, modAt(index, nullptr, nullptr, &pDescription, nullptr) && pDescription != nullptr ? pDescription : "");
    }

    const char* getId(s32 index) {
        if (index >= 0 && index < cEntryNum) {
            return cEntries[index].mId;
        }
        const char* pKey = nullptr;
        return modAt(index, &pKey, nullptr, nullptr, nullptr) ? pKey : nullptr;
    }

    s32 getName(MR::Text16& rText, const char* pId) {
        if (isModKey(pId)) {
            for (s32 i = cEntryNum; i < cEntryNum + modNum(); i++) {
                const char* pKey = nullptr;
                const char* pName = nullptr;
                if (modAt(i, &pKey, &pName, nullptr, nullptr) && MR::isEqualString(pKey, pId) && pName != nullptr) {
                    textFromUtf8(rText, pName);
                    return fromUtf8(nullptr, pName) - 1;
                }
            }
            textFromUtf8(rText, "");
            return 0;
        }
        MR::FormattedName textId;
        getEntryTextId(&textId, pId);
        const s32 units = NebulaText::length(textId.c_str()) + 1;
        return NebulaText::get(rText.room(units), units, textId.c_str());
    }

    s32 getGroup(s32 index) {
        return index >= 0 && index < cEntryNum ? cEntries[index].mGroup : isMod(index) ? 3 : 0;
    }

    bool isShownLocked(s32 index) {
        if (isMod(index)) {
            bool hidden = false;
            return modAt(index, nullptr, nullptr, nullptr, &hidden) && !hidden;
        }
        if (index < 0 || index >= cEntryNum) {
            return false;
        }
        switch (cEntries[index].mShown) {
        case Secret:
            return false;
        case AfterHalfway:
            return NebulaHost::achievementDate("halfway") != 0;
        default:
            return true;
        }
    }

    void onRosalinaWelcome() {
        sRosalinaWelcome = true;
        sEvaluatedStars = -1;  // the next update evaluates, with no new star to prompt it
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
        return index >= 0 && index < cEntryNum ? cEntries[index].mIcon : isMod(index) ? &cIconMod : nullptr;
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
        if (index >= 0 && index < cEntryNum) {
            return NebulaHost::achievementDate(cEntries[index].mId);
        }
        const char* pKey = nullptr;
        return modAt(index, &pKey, nullptr, nullptr, nullptr) ? NebulaHost::achievementDate(pKey) : 0;
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
                NebulaHost::statAdd(cStatNames[i], sStatWaiting[i]);
                sStatWaiting[i] = 0;
            }
        }
    }

    u64 getStat(s32 stat) {
        if (stat < 0 || stat >= NebulaStat_Num) {
            return 0;
        }
        return NebulaHost::statGet(cStatNames[stat]) + sStatWaiting[stat];
    }
};  // namespace NebulaAchievements
