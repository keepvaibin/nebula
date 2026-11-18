#pragma once

// What the uninstall questions' answers ask for (setup.cpp SetupForm::AskUninstall). The first question uninstalls or
// cancels; under it a box, unticked, asks to delete the saves and settings too. Only a ticked box followed by Yes to the
// second question, which names the folder and says it cannot be undone, removes them. Declining either question
// uninstalls nothing: a player who meant to delete the saves and then backed out has not chosen to keep them.

namespace nebula::setup {

enum class UninstallChoice { Cancel, KeepData, RemoveData };

struct UninstallAnswers {
    bool uninstall = false;        // Yes to "Uninstall Nebula?" (or "Remove this portable copy of Nebula?")
    bool deleteTicked = false;     // the box "Also delete the saves and settings", ticked
    bool deleteConfirmed = false;  // Yes to "Delete the saves and settings?", asked only when the box was ticked
};

constexpr UninstallChoice ChooseUninstall(const UninstallAnswers& answers) {
    if (!answers.uninstall) return UninstallChoice::Cancel;
    if (!answers.deleteTicked) return UninstallChoice::KeepData;
    return answers.deleteConfirmed ? UninstallChoice::RemoveData : UninstallChoice::Cancel;
}

}  // namespace nebula::setup
