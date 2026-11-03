#include "Game/System/Language.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/System/NebulaRegion.hpp"
#include "Game/System/NebulaSystemSettings.hpp"

namespace {
    // The disc's release's row (Japan, the Americas, PAL), as NebulaRegion names it.
    s32 regionRow() {
        return NebulaRegion::isJapan() ? 0 : NebulaRegion::isPal() ? 2 : 1;
    }

    // Each release's game language for the host's language setting (NebulaSystemSettings::language: Japanese,
    // English, German, French, Spanish, Italian, Dutch, then three that fall back to English).
    const u8 cLanguageByRegion[][10] = {
        {
            LANGUAGE_JPJAPANESE,
            LANGUAGE_USENGLISH,
            LANGUAGE_EUGERMAN,
            LANGUAGE_EUFRENCH,
            LANGUAGE_EUSPANISH,
            LANGUAGE_EUITALIAN,
            LANGUAGE_EUDUTCH,
            LANGUAGE_EUENGLISH,
            LANGUAGE_EUENGLISH,
            LANGUAGE_EUENGLISH,
        },
        {
            LANGUAGE_JPJAPANESE,
            LANGUAGE_USENGLISH,
            LANGUAGE_EUGERMAN,
            LANGUAGE_USFRENCH,
            LANGUAGE_USSPANISH,
            LANGUAGE_EUITALIAN,
            LANGUAGE_EUDUTCH,
            LANGUAGE_USENGLISH,
            LANGUAGE_USENGLISH,
            LANGUAGE_USENGLISH,
        },
        {
            LANGUAGE_JPJAPANESE,
            LANGUAGE_EUENGLISH,
            LANGUAGE_EUGERMAN,
            LANGUAGE_EUFRENCH,
            LANGUAGE_EUSPANISH,
            LANGUAGE_EUITALIAN,
            LANGUAGE_EUDUTCH,
            LANGUAGE_EUENGLISH,
            LANGUAGE_EUENGLISH,
            LANGUAGE_EUENGLISH,
        },
    };
    const Language cLanguages[] = {
        {LANGUAGE_JPJAPANESE, "JpJapanese"},       {LANGUAGE_USENGLISH, "UsEnglish"},
        {LANGUAGE_USSPANISH, "UsSpanish"},         {LANGUAGE_USFRENCH, "UsFrench"},
        {LANGUAGE_EUENGLISH, "EuEnglish"},         {LANGUAGE_EUSPANISH, "EuSpanish"},
        {LANGUAGE_EUFRENCH, "EuFrench"},           {LANGUAGE_EUGERMAN, "EuGerman"},
        {LANGUAGE_EUITALIAN, "EuItalian"},         {LANGUAGE_EUDUTCH, "EuDutch"},
    };
};  // namespace

namespace MR {
    u32 getDecidedLanguageFromIPL() {
        s32 language = NebulaSystemSettings::language();
        s32 i;

        if (language < 0) {
            i = 0;
        } else {
            s32 size = ARRAY_SIZE(::cLanguageByRegion[regionRow()]);

            i = language < size ? language : size - 1;
        }

        return ::cLanguageByRegion[regionRow()][i];
    }

    u32 getLanguage() {
        return SingletonHolder< GameSystem >::get()->mObjHolder->mLanguage;
    }

    const char* getCurrentLanguagePrefix() {
        u32 id = getLanguage();

        for (int i = 0; i < getLanguageNum(); i++) {
            if (id == ::cLanguages[i].mId) {
                return ::cLanguages[i].mName;
            }
        }

        return "English";
    }

    const char* getCurrentRegionPrefix() {
        char prefix[2];
        MR::extractString(prefix, getCurrentLanguagePrefix(), sizeof(prefix), 3);

        if (MR::isEqualString(prefix, "Jp")) {
            return "Jp";
        }

        if (MR::isEqualString(prefix, "Us")) {
            return "Us";
        }

        if (MR::isEqualString(prefix, "Eu")) {
            return "Eu";
        }
        return nullptr;
    }

    u32 getLanguageNum() {
        return ARRAY_SIZE(::cLanguages);
    }

    const char* getLanguagePrefixByIndex(u32 index) {
        return ::cLanguages[index].mName;
    }
};  // namespace MR
