// installer/native/uninstall_choice.h: every way through Setup's uninstall questions. Before it (P-113), the one
// question's No uninstalled Nebula and deleted every save and setting.
#include "../../installer/native/uninstall_choice.h"

#include <cstdio>

using nebula::setup::ChooseUninstall;
using nebula::setup::UninstallAnswers;
using nebula::setup::UninstallChoice;

int main() {
    int failures = 0;
    auto expect = [&](UninstallAnswers answers, UninstallChoice want, const char* what) {
        if (ChooseUninstall(answers) != want) {
            std::printf("FAIL: %s\n", what);
            failures++;
        }
    };
    expect({false, false, false}, UninstallChoice::Cancel, "No to the first question uninstalls nothing");
    expect({false, true, true}, UninstallChoice::Cancel, "No to the first question, box ticked: nothing");
    expect({true, false, false}, UninstallChoice::KeepData, "Yes, box unticked: uninstall, keep the saves");
    expect({true, false, true}, UninstallChoice::KeepData, "the second answer counts only with the box ticked");
    expect({true, true, false}, UninstallChoice::Cancel, "box ticked, No to the second question: nothing");
    expect({true, true, true}, UninstallChoice::RemoveData, "box ticked and Yes to both: the saves go too");
    std::printf(failures ? "uninstall_choice_tests: %d failed\n" : "uninstall_choice_tests: pass\n", failures);
    return failures ? 1 : 0;
}
