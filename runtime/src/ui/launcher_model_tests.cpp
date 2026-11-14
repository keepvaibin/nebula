#include "galaxy/ui/launcher_model.h"

#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* label) {
    if (!value) throw std::runtime_error(label);
}
}

int main() {
    using galaxy::ui::LauncherAction;
    using Model = galaxy::ui::LauncherModel;
    try {
        Model menu;
        check(menu.view() == -1 && menu.poll() == -1, "closed until opened");
        menu.open();
        check(menu.view() == static_cast<int>(Model::kLauncherView), "opens on Play");
        menu.key(LauncherAction::Previous, false);
        check(menu.row == Model::RowQuit, "rows wrap backwards");
        menu.key(LauncherAction::Next, true);
        check(menu.row == Model::RowPlay, "navigation repeats and wraps");
        menu.key(LauncherAction::Next, false);
        check(menu.key(LauncherAction::Accept, true) == Model::Effect::None, "accept does not repeat");
        check(menu.key(LauncherAction::Accept, false) == Model::Effect::OpenSettings && menu.visible,
              "settings opens over the rows");
        menu.key(LauncherAction::Back, false);
        check(menu.row == Model::RowSettings && menu.visible, "back on the rows does nothing");

        menu.offer_notice();
        check(menu.view() == static_cast<int>(Model::kNoticePage | Model::kLauncherView),
              "a notice shows its page at once");
        menu.key(LauncherAction::Next, false);
        menu.point(2);
        check(menu.row == Model::RowSettings, "the notice's page keeps the row");
        menu.key(LauncherAction::Accept, false);
        check(menu.view() == static_cast<int>(Model::RowSettings | Model::kLauncherView) && menu.notice_held,
              "its answer returns to the rows, the notice still held");
        menu.offer_notice();
        check(!menu.notice_page, "a notice shows its page once");
        check(menu.click(5) == Model::Effect::None && menu.visible, "a click past the rows does nothing");
        menu.click(Model::RowQuit);
        check(menu.poll() == Model::kChoiceQuit && !menu.visible && menu.view() == -1, "quit closes");
        check(menu.key(LauncherAction::Accept, false) == Model::Effect::None && menu.poll() == Model::kChoiceQuit,
              "a closed screen takes no keys");

        Model play;
        play.offer_notice();
        play.open();
        check(play.notice_page, "a notice found before the screen opens shows first");
        play.key(LauncherAction::Back, false);
        play.point(1);
        play.point(0);
        play.key(LauncherAction::Accept, false);
        check(play.poll() == Model::kChoicePlay && !play.visible, "play closes");
        play.open();
        check(play.poll() == -1 && play.row == Model::RowPlay && play.notice_page, "open starts again, notice first");
    } catch (const std::exception& failure) {
        std::cerr << "launcher_model_tests: " << failure.what() << '\n';
        return 1;
    }
    std::cout << "launcher_model_tests: ok\n";
    return 0;
}
