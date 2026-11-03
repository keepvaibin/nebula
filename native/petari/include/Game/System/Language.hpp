#pragma once


#define REGION_EU 0
#define REGION_JP 1
#define REGION_US 2
#define REGION_CN 3
#define REGION_KR 4
#define REGION_SHIFT 0x4
#define REGION_SHIFT_EU (REGION_EU << REGION_SHIFT)
#define REGION_SHIFT_JP (REGION_JP << REGION_SHIFT)
#define REGION_SHIFT_US (REGION_US << REGION_SHIFT)
#define REGION_SHIFT_CN (REGION_CN << REGION_SHIFT)
#define REGION_SHIFT_KR (REGION_KR << REGION_SHIFT)
#define REGION_MASK (0xF << REGION_SHIFT)

// The system's language numbers (the host's NEBULA_SC_LANGUAGE values).
#define SYSTEM_LANG_JAPANESE 0u
#define SYSTEM_LANG_ENGLISH 1u
#define SYSTEM_LANG_GERMAN 2u
#define SYSTEM_LANG_FRENCH 3u
#define SYSTEM_LANG_SPANISH 4u
#define SYSTEM_LANG_ITALIAN 5u
#define SYSTEM_LANG_DUTCH 6u
#define SYSTEM_LANG_SIMP_CHINESE 7u
#define SYSTEM_LANG_KOREAN 9u

#define LANGUAGE_JPJAPANESE (REGION_SHIFT_JP | SYSTEM_LANG_JAPANESE)
#define LANGUAGE_USENGLISH (REGION_SHIFT_US | SYSTEM_LANG_ENGLISH)
#define LANGUAGE_USSPANISH (REGION_SHIFT_US | SYSTEM_LANG_SPANISH)
#define LANGUAGE_USFRENCH (REGION_SHIFT_US | SYSTEM_LANG_FRENCH)
#define LANGUAGE_EUENGLISH (REGION_SHIFT_EU | SYSTEM_LANG_ENGLISH)
#define LANGUAGE_EUSPANISH (REGION_SHIFT_EU | SYSTEM_LANG_SPANISH)
#define LANGUAGE_EUFRENCH (REGION_SHIFT_EU | SYSTEM_LANG_FRENCH)
#define LANGUAGE_EUGERMAN (REGION_SHIFT_EU | SYSTEM_LANG_GERMAN)
#define LANGUAGE_EUITALIAN (REGION_SHIFT_EU | SYSTEM_LANG_ITALIAN)
#define LANGUAGE_EUDUTCH (REGION_SHIFT_EU | SYSTEM_LANG_DUTCH)
#define LANGUAGE_CNSIMPCHINESE (REGION_SHIFT_CN | SYSTEM_LANG_SIMP_CHINESE)
#define LANGUAGE_KRKOREAN (REGION_SHIFT_KR | SYSTEM_LANG_KOREAN)
#define LANGUAGE_MASK (~REGION_MASK)

/// @brief A regional variant of a language.
struct Language {
    /// @brief The region-language identifier.
    u32 mId;

    /// @brief The region-language label.
    const char* mName;
};

namespace MR {
    /// @brief Returns the region-language identifier based on the language setting.
    /// @return The region-language identifier.
    u32 getDecidedLanguageFromIPL();

    /// @brief Returns the working region-language identifier.
    /// @return The region-language identifier.
    u32 getLanguage();

    /// @brief Extracts the language setting from the working region-language identifier.
    /// @return The language setting.

    /// @brief Returns the region-language label.
    /// @return The pointer to the null-terminated label.
    const char* getCurrentLanguagePrefix();

    /// @brief Returns the region label.
    /// @return The pointer to the null-terminated label.
    const char* getCurrentRegionPrefix();

    /// @brief Returns the number of region-language pairs.
    /// @return The number of region-language pairs.
    u32 getLanguageNum();

    /// @brief Returns the region-language label by index.
    /// @param index The position in the lookup table.
    /// @return The pointer to the null-terminated label.
    const char* getLanguagePrefixByIndex(u32 index);
};  // namespace MR
