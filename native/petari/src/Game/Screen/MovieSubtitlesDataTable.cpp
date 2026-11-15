#include "Game/Screen/MovieSubtitlesDataTable.hpp"
#include "Game/System/LumaMovie.hpp"
#include "Game/Util/StringUtil.hpp"

#define ENTRY(name, step, appear) {name, step, appear}

namespace {
    static const SubtitlesMessageInfo sPrologueA[] = {
        ENTRY("Layout_StoryDemoKoopaTalk000", 3887, 132),
        ENTRY("Layout_StoryDemoKoopaTalk001", 4020, 154),
        ENTRY("Layout_StoryDemoKoopaTalk002", 4175, 120),
        ENTRY("Layout_StoryDemoKoopaTalk003", 4680, 179),
        ENTRY("Layout_StoryDemoKoopaTalk004", 4560, 120),
    };

    static const SubtitlesMessageInfo sPrologueB[] = {
        ENTRY("Layout_StoryDemoPeachTalk000", 4920, 120),
        ENTRY("Layout_StoryDemoKameckTalk000", 5460, 180),
    };

    static const SubtitlesMessageInfo sFinalBattle[] = {
        ENTRY("Layout_StoryDemoRosettaTalk000", 2970, 159),
        ENTRY("Layout_StoryDemoRosettaTalk006", 3130, 180),
    };

    static const SubtitlesMessageInfo sEpilogueA[] = {
        ENTRY("Layout_StoryDemoKoopaTalk005", 4607, 187),
        ENTRY("Layout_StoryDemoKoopaTalk006", 4795, 226),
    };

    static const SubtitlesMessageInfo sEndingA[] = {
        ENTRY("Layout_StoryDemoRosettaTalk001", 4980, 119),
        ENTRY("Layout_StoryDemoRosettaTalk002", 5100, 149),
        ENTRY("Layout_StoryDemoRosettaTalk003", 5250, 120),
    };

    static const SubtitlesMessageInfo sEndingB[] = {
        ENTRY("Layout_StoryDemoRosettaTalk004", 1860, 134),
        ENTRY("Layout_StoryDemoRosettaTalk005", 1995, 275),
    };

    static const SubtitlesInfo sSubtitlesInfo[] = {
        {"/MovieData/PrologueA.thp", sPrologueA, ARRAY_SIZE(sPrologueA)},
        {"/MovieData/PrologueB.thp", sPrologueB, ARRAY_SIZE(sPrologueB)},
        {"/MovieData/FinalBattle.thp", sFinalBattle, ARRAY_SIZE(sFinalBattle)},
        {"/MovieData/EpilogueA.thp", sEpilogueA, ARRAY_SIZE(sEpilogueA)},
        {"/MovieData/EpilogueB.thp", nullptr, 0},
        {"/MovieData/EndingA.thp", sEndingA, ARRAY_SIZE(sEndingA)},
        {"/MovieData/EndingB.thp", sEndingB, ARRAY_SIZE(sEndingB)},
    };

    const SubtitlesInfo* getSubtitlesInfo(const char* pMovieName) {
        // A mod's movie in the game movie's place with its own lines beside it (<name>.subs): the game's
        // lines were timed for the game's movie and are not shown over it; luma::movie draws the mod's.
        if (luma::movie::hasSubtitles(pMovieName)) {
            return nullptr;
        }
        for (u32 i = 0; i < ARRAY_SIZE(sSubtitlesInfo); i++) {
            if (MR::isEqualString(pMovieName, sSubtitlesInfo[i].mMovieName)) {
                return &sSubtitlesInfo[i];
            }
        }

        return nullptr;
    }
};  // namespace

namespace MovieSubtitlesUtil {
    const SubtitlesMessageInfo* getSubtitlesMessageInfo(const char* pMovieName, s32 idx) {
        const SubtitlesInfo* pInfo = ::getSubtitlesInfo(pMovieName);

        if (pInfo == nullptr || idx < 0 || idx >= pInfo->mMessageNum) {
            return nullptr;
        }

        return &pInfo->mMessageInfo[idx];
    }

    bool isExistSubtitles(const char* pMovieName, s32 idx) {
        return getSubtitlesMessageInfo(pMovieName, idx) != nullptr;
    }

    const char* getSubtitlesMessageId(const char* pMovieName, s32 idx) {
        const SubtitlesMessageInfo* pSubtitles = getSubtitlesMessageInfo(pMovieName, idx);

        if (pSubtitles != nullptr) {
            return pSubtitles->mMessageID;
        }

        return nullptr;
    }

    s32 getSubtitlesStartStep(const char* pMovieName, s32 idx) {
        const SubtitlesMessageInfo* pSubtitles = getSubtitlesMessageInfo(pMovieName, idx);

        if (pSubtitles != nullptr) {
            return pSubtitles->mStartStep;
        }

        return 0;
    }

    s32 getSubtitlesAppearTime(const char* pMovieName, s32 idx) {
        const SubtitlesMessageInfo* pSubtitles = getSubtitlesMessageInfo(pMovieName, idx);

        if (pSubtitles != nullptr) {
            return pSubtitles->mAppearTime;
        }

        return 0;
    }

    s32 getSubtitlesMessageNum(const char* pMovieName) {
        const SubtitlesInfo* pInfo = ::getSubtitlesInfo(pMovieName);

        if (pInfo != nullptr) {
            return pInfo->mMessageNum;
        }

        return 0;
    }
};  // namespace MovieSubtitlesUtil
