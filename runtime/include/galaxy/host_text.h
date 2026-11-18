#pragma once
// The host's own texts: its windows, menus and file dialogs on every platform, and the crash dialog. They are the "C."
// and "H." rows of localization/NebulaText.tsv (with the Setup rows tools/nebula_text.py's HOST_ROWS names), written to
// runtime/src/host_text_table.inc and found by their English text; tools/nebula_text.py --check (in the gate) fails a
// host_text call whose English has no row.

#include "galaxy/runtime_settings.h"

#include <initializer_list>
#include <string>
#include <string_view>

namespace galaxy::host_text {

// The table column ("EuGerman") the host's texts are shown in: the game's LANGUAGE setting where it names a language,
// else the system's. `system` is a BCP 47 tag or a POSIX locale name ("fr-CA", "zh-Hant-TW", "pt_BR.UTF-8"); its region
// picks between a language's two columns (French of Canada: UsFrench; Spanish of Spain: EuSpanish; English of no region,
// the United States, Canada, Puerto Rico or the Philippines: UsEnglish; Chinese written in Traditional characters, or of
// Taiwan, Hong Kong or Macao: TwTradChinese). A language the table lacks is English (UsEnglish).
[[nodiscard]] const char* language_of(GameLanguage setting, std::string_view system) noexcept;

// This system's display language as a tag: Windows' user interface language, the Mac's first preferred language,
// elsewhere LC_ALL, LC_MESSAGES then LANG. Empty when none is set.
[[nodiscard]] std::string system_tag();

// `english` in `language`'s column; `english` itself when no row has it or the row's cell is empty. A row's text is
// static storage, as is what `english` points at when it is a literal.
[[nodiscard]] std::string_view translated(std::string_view english, std::string_view language) noexcept;

// The column for the LANGUAGE setting now in effect and this system.
[[nodiscard]] inline const char* language() {
    return language_of(get_runtime_settings().language, system_tag());
}

// `english` in language(), as a string.
[[nodiscard]] inline std::string text(std::string_view english) {
    return std::string(translated(english, language()));
}

// The file dialogs' words, the same on every host (tools/nebula_text.py checks each has a row).
inline constexpr char kImportMiis[] = "Import Miis";
inline constexpr char kImportModFolders[] = "Import mod folders";
inline constexpr char kImportSaves[] = "Import saves";
inline constexpr char kImportMods[] = "Import mods";
inline constexpr char kExportSaves[] = "Export saves";
inline constexpr char kMiiFiles[] = "Mii files (Wii, DS, 3DS, Wii U, Switch, Mii Studio, Mii Creator)";
inline constexpr char kSaveFiles[] = "Saves (zip, Nebula.sav, GameData.bin, Wii SD data.bin)";
inline constexpr char kModFiles[] = "Mods and texture packs (luma, zip, 7z, rar, tar)";
inline constexpr char kZipArchive[] = "Zip archive";
inline constexpr char kAllFiles[] = "All files";

// `text` with {0}, {1}... replaced by `arguments` in order.
[[nodiscard]] std::string format(std::string_view text, std::initializer_list<std::string_view> arguments);

}  // namespace galaxy::host_text
