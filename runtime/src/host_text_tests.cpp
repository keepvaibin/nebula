// SPDX-License-Identifier: GPL-3.0-only
// The host's own texts (galaxy/host_text.h, P-106): the column a system's language and the LANGUAGE setting give, and
// the host windows' rows in every language.
#include "galaxy/host_text.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

int g_failures = 0;

void expect(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what.c_str());
        ++g_failures;
    }
}

void column(galaxy::GameLanguage setting, std::string_view system, std::string_view wanted) {
    const std::string_view got = galaxy::host_text::language_of(setting, system);
    expect(got == wanted, "'" + std::string(system) + "' (setting " + std::to_string(static_cast<int>(setting)) +
                              "): " + std::string(got) + ", not " + std::string(wanted));
}

constexpr std::string_view kTranslated[] = {"UsFrench",  "EuFrench", "UsSpanish",  "EuSpanish",     "EuGerman",
                                            "EuItalian", "EuDutch",  "JpJapanese", "UsPortuguese", "KrKorean",
                                            "CnSimpChinese", "TwTradChinese"};

// A text every language but English says in its own words.
void in_every_language(std::string_view english) {
    for (const std::string_view language : kTranslated) {
        const std::string_view got = galaxy::host_text::translated(english, language);
        expect(!got.empty() && got != english, std::string(english) + " in " + std::string(language));
    }
}

}  // namespace

int main() {
    using galaxy::GameLanguage;
    // The system's language, for the Auto setting: a BCP 47 tag (Windows, the Mac) or a POSIX locale (Linux).
    column(GameLanguage::Auto, "fr-CA", "UsFrench");
    column(GameLanguage::Auto, "fr_FR.UTF-8", "EuFrench");
    column(GameLanguage::Auto, "fr-BE", "EuFrench");
    column(GameLanguage::Auto, "es-ES", "EuSpanish");
    column(GameLanguage::Auto, "es-ES_tradnl", "EuSpanish");
    column(GameLanguage::Auto, "es-MX", "UsSpanish");
    column(GameLanguage::Auto, "es-419", "UsSpanish");
    column(GameLanguage::Auto, "es", "UsSpanish");
    column(GameLanguage::Auto, "zh-Hant-TW", "TwTradChinese");
    column(GameLanguage::Auto, "zh-TW", "TwTradChinese");
    column(GameLanguage::Auto, "zh_HK.UTF-8", "TwTradChinese");
    column(GameLanguage::Auto, "zh-MO", "TwTradChinese");
    column(GameLanguage::Auto, "zh-Hant", "TwTradChinese");
    column(GameLanguage::Auto, "zh-Hans-CN", "CnSimpChinese");
    column(GameLanguage::Auto, "zh-Hans-HK", "CnSimpChinese");
    column(GameLanguage::Auto, "zh-CN", "CnSimpChinese");
    column(GameLanguage::Auto, "zh_SG", "CnSimpChinese");
    column(GameLanguage::Auto, "pt-BR", "UsPortuguese");
    column(GameLanguage::Auto, "pt_PT.UTF-8", "UsPortuguese");
    column(GameLanguage::Auto, "ko-KR", "KrKorean");
    column(GameLanguage::Auto, "ja_JP.UTF-8", "JpJapanese");
    column(GameLanguage::Auto, "JA", "JpJapanese");
    column(GameLanguage::Auto, "de-AT", "EuGerman");
    column(GameLanguage::Auto, "it-CH", "EuItalian");
    column(GameLanguage::Auto, "nl-BE", "EuDutch");
    column(GameLanguage::Auto, "en-US", "UsEnglish");
    column(GameLanguage::Auto, "en_CA.UTF-8", "UsEnglish");
    column(GameLanguage::Auto, "en", "UsEnglish");
    column(GameLanguage::Auto, "en-GB", "EuEnglish");
    column(GameLanguage::Auto, "en_AU.UTF-8@euro", "EuEnglish");
    column(GameLanguage::Auto, "sv-SE", "UsEnglish");
    column(GameLanguage::Auto, "C", "UsEnglish");
    column(GameLanguage::Auto, "", "UsEnglish");
    // The setting wins; the system's region still picks between its language's two columns.
    column(GameLanguage::German, "fr-CA", "EuGerman");
    column(GameLanguage::Japanese, "en-US", "JpJapanese");
    column(GameLanguage::French, "fr-CA", "UsFrench");
    column(GameLanguage::French, "en-CA", "UsFrench");
    column(GameLanguage::French, "de-DE", "EuFrench");
    column(GameLanguage::Spanish, "ca-ES", "EuSpanish");
    column(GameLanguage::Spanish, "en-GB", "UsSpanish");
    column(GameLanguage::English, "en-GB", "EuEnglish");
    column(GameLanguage::English, "de-DE", "UsEnglish");
    column(GameLanguage::Italian, "", "EuItalian");
    column(GameLanguage::Dutch, "ko-KR", "EuDutch");

    // The table: a row in a language, English, no row, no such column.
    expect(galaxy::host_text::translated("The game crashed", "EuGerman") == "Das Spiel ist abgest\xC3\xBCrzt", "a row in a language");
    expect(galaxy::host_text::translated("The game crashed", "UsEnglish") == "The game crashed", "English is the words given");
    expect(galaxy::host_text::translated("no such row", "EuGerman") == "no such row", "no row: the words given");
    expect(galaxy::host_text::translated("The game crashed", "Klingon") == "The game crashed", "no such column: the words given");
    // The host windows' rows (HEAD had none: the Mac's menus and setup window and every host's file dialogs were
    // English only).
    for (const std::string_view english :
         {"About Nebula", "Hide Nebula", "Hide Others", "Show All", "Quit Nebula", "Window", "Minimize", "Enter Full Screen", "Import Miis",
          "Import mod folders", "Import saves", "Import mods", "Export saves", "All files", "Zip archive",
          "Mii files (Wii, DS, 3DS, Wii U, Switch, Mii Studio, Mii Creator)",
          "Saves (zip, Nebula.sav, GameData.bin, Wii SD data.bin)", "Mods and texture packs (luma, zip, 7z, rar, tar)",
          "Set up Nebula", "Choose disc image\xE2\x80\xA6", "Choose your Super Mario Galaxy disc image",
          "Choose your Super Mario Galaxy disc image. This is done once.", "Checking\xE2\x80\xA6",
          "Setting up\xE2\x80\xA6", "Stopping\xE2\x80\xA6", "Stopped.", "This disc image cannot be used.",
          "The game's data could not be saved.", "The game's data could not be read back from this Mac's drive. Try again.",
          "Nebula needs {0} GB free on this Mac to set up the game; {1} GB is free.", "Quit", "Cancel"}) {
        in_every_language(english);
    }
    // A word a language says as English keeps its English cell.
    expect(galaxy::host_text::translated("Zoom", "EuItalian") == "Zoom", "Zoom in Italian");
    expect(galaxy::host_text::translated("Controller", "EuGerman") == "Controller", "Controller in German");
    expect(galaxy::host_text::translated("Controller", "EuFrench") == "Manette", "Controller in French");
    expect(galaxy::host_text::translated(".", "EuGerman") == "," && galaxy::host_text::translated(".", "JpJapanese") == ".",
           "the decimal mark");

    expect(galaxy::host_text::format("{0} GB of {1}", {"4,4", "2"}) == "4,4 GB of 2", "format");
    expect(galaxy::host_text::format("{1}{0}{2}", {"a", "b"}) == "ba{2}", "format: an argument not given stays");

#if !defined(_WIN32) && !defined(__APPLE__)
    setenv("LC_ALL", "fr_CA.UTF-8", 1);
    expect(galaxy::host_text::system_tag() == "fr_CA.UTF-8", "LC_ALL is the system's language");
    unsetenv("LC_ALL");
#endif
    std::printf("host text: system '%s'\n", galaxy::host_text::system_tag().c_str());

    if (g_failures != 0) {
        std::fprintf(stderr, "host text: %d failed\n", g_failures);
        return 1;
    }
    std::printf("host text: every case holds\n");
    return 0;
}
