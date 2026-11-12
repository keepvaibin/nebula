#include "Game/System/Language.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/System/NebulaRegion.hpp"
#include "Game/System/NebulaSystemSettings.hpp"

namespace {
    // The game data's release row (Japan, the Americas, Europe), as NebulaRegion names it.
    s32 regionRow() {
        return NebulaRegion::isJapan() ? 0 : NebulaRegion::isEurope() ? 2 : 1;
    }

    const s32 cSystemLanguageNum = 10;

    // Each release's game language for the host's language setting (NebulaSystemSettings::language: Japanese,
    // English, German, French, Spanish, Italian, Dutch, then three that fall back to English).
    const u8 cLanguageByRegion[][cSystemLanguageNum] = {
        {
            LanguageId_JpJapanese,
            LanguageId_UsEnglish,
            LanguageId_EuGerman,
            LanguageId_EuFrench,
            LanguageId_EuSpanish,
            LanguageId_EuItalian,
            LanguageId_EuDutch,
            LanguageId_EuEnglish,
            LanguageId_EuEnglish,
            LanguageId_EuEnglish,
        },
        {
            LanguageId_JpJapanese,
            LanguageId_UsEnglish,
            LanguageId_EuGerman,
            LanguageId_UsFrench,
            LanguageId_UsSpanish,
            LanguageId_EuItalian,
            LanguageId_EuDutch,
            LanguageId_UsEnglish,
            LanguageId_UsEnglish,
            LanguageId_UsEnglish,
        },
        {
            LanguageId_JpJapanese,
            LanguageId_EuEnglish,
            LanguageId_EuGerman,
            LanguageId_EuFrench,
            LanguageId_EuSpanish,
            LanguageId_EuItalian,
            LanguageId_EuDutch,
            LanguageId_EuEnglish,
            LanguageId_EuEnglish,
            LanguageId_EuEnglish,
        },
    };

    const Language cLanguages[] = {
        {LanguageId_JpJapanese, "JpJapanese"}, {LanguageId_UsEnglish, "UsEnglish"}, {LanguageId_UsSpanish, "UsSpanish"},
        {LanguageId_UsFrench, "UsFrench"},     {LanguageId_EuEnglish, "EuEnglish"}, {LanguageId_EuSpanish, "EuSpanish"},
        {LanguageId_EuFrench, "EuFrench"},     {LanguageId_EuGerman, "EuGerman"},   {LanguageId_EuItalian, "EuItalian"},
        {LanguageId_EuDutch, "EuDutch"},
    };
}  // namespace

namespace MR {
    u32 decideLanguage() {
        const s32 language = NebulaSystemSettings::language();
        const s32 i = language < 0 ? 0 : language < ::cSystemLanguageNum ? language : ::cSystemLanguageNum - 1;

        return ::cLanguageByRegion[::regionRow()][i];
    }

    // Before the game's objects exist (a mod's start, the boot) the language they will take (GameSystemObjHolder's
    // mLanguage is decideLanguage's).
    u32 getLanguage() {
        const GameSystem* const pSystem = SingletonHolder< GameSystem >::get();
        return pSystem != nullptr && pSystem->mObjHolder != nullptr ? pSystem->mObjHolder->mLanguage : decideLanguage();
    }

    const char* getCurrentLanguagePrefix() {
        const u32 id = getLanguage();

        for (u32 i = 0; i < getLanguageNum(); i++) {
            if (id == ::cLanguages[i].mId) {
                return ::cLanguages[i].mName;
            }
        }

        return "English";
    }

    // Every id the game sets is in cLanguages, so its region is the prefix's first two letters.
    const char* getCurrentRegionPrefix() {
        switch (getLanguage() >> cLanguageRegionShift) {
        case LanguageRegion_Jp:
            return "Jp";
        case LanguageRegion_Us:
            return "Us";
        case LanguageRegion_Eu:
            return "Eu";
        default:
            return nullptr;
        }
    }

    u32 getLanguageNum() {
        return sizeof(::cLanguages) / sizeof(::cLanguages[0]);
    }

    const char* getLanguagePrefixByIndex(u32 index) {
        return ::cLanguages[index].mName;
    }
};  // namespace MR
