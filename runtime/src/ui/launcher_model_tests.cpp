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

        Model bench;
        bench.open();
        bench.key(LauncherAction::Next, false);
        bench.key(LauncherAction::Next, false);
        check(bench.row == Model::RowBenchmark, "benchmark is the row after settings");
        check(bench.key(LauncherAction::Accept, false) == Model::Effect::None && bench.poll() == Model::kChoiceBenchmark &&
                  !bench.visible,
              "benchmark closes with its own choice");
        bench.open();
        bench.click(Model::RowBenchmark);
        check(bench.poll() == Model::kChoiceBenchmark, "a click on benchmark chooses it");

        Model three;
        three.benchmark = false;
        three.open();
        check(three.view() == static_cast<int>(Model::kLauncherView | Model::kNoBenchmark), "no benchmark: the view says so");
        three.key(LauncherAction::Previous, false);
        check(three.row == 2u && three.row_at(three.row) == Model::RowQuit, "no benchmark: three rows, quit last");
        three.click(2u);
        check(three.poll() == Model::kChoiceQuit, "no benchmark: the third answer quits");
        three.open();
        check(three.click(3u) == Model::Effect::None && three.visible, "no benchmark: no fourth answer");

        Model back;
        back.offer_message();
        back.offer_notice();
        back.open();
        check(back.view() == static_cast<int>(Model::kMessagePage | Model::kLauncherView),
              "a benchmark's message shows before a notice");
        back.key(LauncherAction::Next, false);
        check(back.row == Model::RowPlay && back.click(1u) == Model::Effect::None && back.view() ==
                  static_cast<int>(Model::kMessagePage | Model::kLauncherView),
              "the message's page has one answer and keeps the row");
        back.key(LauncherAction::Accept, false);
        check(back.view() == static_cast<int>(Model::kNoticePage | Model::kLauncherView), "its answer: the notice's page");
        back.key(LauncherAction::Back, false);
        check(back.view() == static_cast<int>(Model::kLauncherView), "then the rows");
        back.click(Model::RowPlay);
        back.open();
        check(!back.message_page && back.notice_page, "the message shows once, the notice on every open");
    } catch (const std::exception& failure) {
        std::cerr << "launcher_model_tests: " << failure.what() << '\n';
        return 1;
    }
    std::cout << "launcher_model_tests: ok\n";
    return 0;
}
