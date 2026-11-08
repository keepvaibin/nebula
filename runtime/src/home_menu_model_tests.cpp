#include "galaxy/ui/home_menu_model.h"

#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* label) {
    if (!value) throw std::runtime_error(label);
}
}

int main() {
    using galaxy::ui::HomeAction;
    try {
        galaxy::ui::HomeMenuModel menu;
        menu.visible = true;
        menu.key(HomeAction::Previous, false);
        check(menu.row == 2, "list wraps backwards");
        menu.key(HomeAction::Next, true);
        check(menu.row == 0, "navigation repeats and wraps");
        menu.key(HomeAction::Next, false);
        menu.key(HomeAction::Accept, true);
        check(menu.asking == 0, "accept does not repeat");
        menu.key(HomeAction::Accept, false);
        check(menu.asking == 2 && menu.row == 0 && menu.answer_count() == 2, "reset defaults to no");
        menu.key(HomeAction::Accept, false);
        check(menu.asking == 0 && menu.row == 1 && menu.visible, "no restores reset row");
        menu.key(HomeAction::Next, false);
        menu.key(HomeAction::Accept, false);
        menu.key(HomeAction::Back, false);
        check(menu.asking == 0 && menu.row == 2, "back restores quit row");
        menu.key(HomeAction::Accept, false);
        menu.key(HomeAction::Next, false);
        menu.key(HomeAction::Accept, false);
        check(menu.choice == 1 && !menu.visible, "confirmed quit");
        menu.visible = true;
        menu.key(HomeAction::Close, false);
        check(menu.choice == 0 && !menu.visible, "close returns to game");
        galaxy::ui::HomeMenuModel reset;
        reset.visible = true;
        reset.choose(1);
        reset.choose(1);
        check(reset.choice == 2 && !reset.visible, "confirmed reset");
        galaxy::ui::HomeMenuModel resume;
        resume.visible = true;
        resume.choose(0);
        check(resume.choice == 0 && !resume.visible, "return row closes immediately");
        resume.open();
        check(resume.poll() == -1 && resume.view() == 0, "open resets selection and result");
        resume.point(2);
        check(resume.view() == 2, "pointer selects list row");
        resume.click(2);
        check(resume.view() == 256, "question view");
        resume.point(2);
        check(resume.view() == 256, "invalid answer ignored");
        resume.point(1);
        check(resume.view() == 257, "question answer view");
        resume.click(1);
        check(resume.poll() == 1 && resume.view() == -1, "pointer confirms quit");
        std::cout << "HOME model: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
