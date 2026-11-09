#pragma once

// A language id: the region in bits 4-7, the system language number in bits 0-3.
enum LanguageRegion {
    LanguageRegion_Eu = 0,
    LanguageRegion_Jp = 1,
    LanguageRegion_Us = 2,
};

// The host's language numbers (NebulaSystemSettings::language).
enum SystemLanguage {
    SystemLanguage_Japanese = 0,
    SystemLanguage_English = 1,
    SystemLanguage_German = 2,
    SystemLanguage_French = 3,
    SystemLanguage_Spanish = 4,
    SystemLanguage_Italian = 5,
    SystemLanguage_Dutch = 6,
};

const u32 cLanguageRegionShift = 4;

enum LanguageId {
    LanguageId_JpJapanese = (LanguageRegion_Jp << cLanguageRegionShift) | SystemLanguage_Japanese,
    LanguageId_UsEnglish = (LanguageRegion_Us << cLanguageRegionShift) | SystemLanguage_English,
    LanguageId_UsSpanish = (LanguageRegion_Us << cLanguageRegionShift) | SystemLanguage_Spanish,
    LanguageId_UsFrench = (LanguageRegion_Us << cLanguageRegionShift) | SystemLanguage_French,
    LanguageId_EuEnglish = (LanguageRegion_Eu << cLanguageRegionShift) | SystemLanguage_English,
    LanguageId_EuSpanish = (LanguageRegion_Eu << cLanguageRegionShift) | SystemLanguage_Spanish,
    LanguageId_EuFrench = (LanguageRegion_Eu << cLanguageRegionShift) | SystemLanguage_French,
    LanguageId_EuGerman = (LanguageRegion_Eu << cLanguageRegionShift) | SystemLanguage_German,
    LanguageId_EuItalian = (LanguageRegion_Eu << cLanguageRegionShift) | SystemLanguage_Italian,
    LanguageId_EuDutch = (LanguageRegion_Eu << cLanguageRegionShift) | SystemLanguage_Dutch,
};

// A language id and its prefix (the message and layout folder names).
struct Language {
    u32 mId;
    const char* mName;
};

namespace MR {
    // The language id for the host's language setting and the game data's region.
    u32 decideLanguage();
    u32 getLanguage();
    const char* getCurrentLanguagePrefix();
    // "Jp", "Us" or "Eu".
    const char* getCurrentRegionPrefix();
    u32 getLanguageNum();
    const char* getLanguagePrefixByIndex(u32 index);
};  // namespace MR
