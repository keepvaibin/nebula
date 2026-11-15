// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "galaxy/ui/settings_model.h"

#include <array>
#include <cstdint>

namespace galaxy::ui {

// The SAVES rows' step: a picker open, the game reading the picked save (the per-file import), the player's choice
// asked, an import being written (the game restarts after it), an export being made or saved.
enum class SavesStep : unsigned { Idle, Choosing, Reading, Confirm, Importing, Exporting };

// One user file of a save in the per-file import, as the game reads it (nebula_save_import.h).
struct SaveImportFile {
    bool exists = false;   // created
    bool damaged = false;  // does not load
    bool luigi = false;    // its last character Luigi
    unsigned stars = 0;    // that character's power stars
};
inline constexpr std::size_t kSaveImportFiles = 6;
using SaveImportFiles = std::array<SaveImportFile, kSaveImportFiles>;
// Where each imported file goes: a user file, 1 to 6, or 0, left out.
using SaveImportTargets = std::array<std::uint8_t, kSaveImportFiles>;

// The first choice: each imported file that loads in a free user file of the save (its own number when that one is
// free, else the first free one), in order, and left out once none is free.
[[nodiscard]] SaveImportTargets save_import_first_targets(const SaveImportFiles& imported, const SaveImportFiles& current);
// File `file`'s (1 to 6) target `delta` steps on, through "left out" and the user files no other imported file goes
// to, wrapping around.
void save_import_step(SaveImportTargets& targets, unsigned file, int delta);

struct GeneralPageState {
    ButtonIcons auto_icons = ButtonIcons::Game;
    GameLanguage display_language = GameLanguage::English;
    std::vector<GameLanguage> languages;
    GameLanguage saved_language = GameLanguage::Auto;
    GameLanguage resolved_saved_language = GameLanguage::English;
    std::string mii_status;
    bool choosing_miis = false;
    int mii_count = 0;
    bool folders_open = true;  // the platform shows a folder's files (host_window::folders_open)
    bool focus_loss_offered = false;  // the host acts on a window that loses focus (the desktop hosts)
    bool discord_offered = false;     // Discord's desktop IPC exists here (galaxy::discord::supported)
    // SAVES: where the game has a save folder (save_transfer).
    bool saves_offered = false;
    SavesStep saves_step = SavesStep::Idle;
    bool saves_replace = false;      // Confirm: the import replaces a save that is there
    std::string saves_import_status;  // the last import's result, shown as the row's help
    std::string saves_export_status;  // the last export's
    // Confirm, file by file: the picked save's files, the save's now, and where each imported one goes. A whole import
    // (REPLACE ALL SAVES) is offered beside it when the picked file holds more than this release's save.
    bool saves_by_file = false;
    bool saves_whole_offered = false;
    SaveImportFiles import_files{};
    SaveImportFiles current_files{};
    SaveImportTargets import_targets{};
};

// ConfirmImport writes the choice (file by file, or the whole save); ReplaceAllSaves the whole save beside a choice
// file by file.
enum class GeneralPageAction {
    ImportMiis,
    OpenMiiFolder,
    ImportSaves,
    ConfirmImport,
    ReplaceAllSaves,
    CancelImport,
    ExportSaves,
    OpenSaveFolder
};
class GeneralPageHost {
public:
    virtual ~GeneralPageHost() = default;
    virtual GeneralPageState query(const RuntimeSettings& settings) const = 0;
    virtual void act(GeneralPageAction action) const = 0;
    // The per-file import's choice for imported file `file` (1 to 6), `delta` steps on (save_import_step).
    virtual void step_import_target(unsigned file, int delta) const = 0;
};

// The host and draft outlive the rows that refer to them.
std::vector<SettingsRow> general_page(const RuntimeSettings& runtime, RuntimeSettings& draft,
                                      const GeneralPageHost& host);

}  // namespace galaxy::ui
