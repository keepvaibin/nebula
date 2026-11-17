/* The game's pause buttons in a mod's screen (ui 1.3), one case each (-DCASE=n). Run steps call MR::getRandom(0,
   3800 + k) and the hook gives 1. Both open the game's own layout PauseMenu on the HUD ("open"), play its Appear on
   the screen ("appear", which brings its two buttons NBackUp and NBackDown in to the middle) and each button's
   ButtonAppear on that button alone ("appear_up", "appear_down", play_pane).
   ju-label (1) 3816: k 1 also sets the upper button's label pane Text1 (its text Txt1 and shadow Sha1, buffers of 7 in
               the game's layout) to "Kit A two words" ("label"): the boxes grow to it, as the game's own setting
               does, so it reads whole, with no Ui.TextCut.
   ju-light (2) 3817: k 2 plays ButtonSelectIn on NBackUp only ("light", play_pane with an end function): the upper
               button lights, the lower one does not. Its end comes with the pane's handle ("end", "end_is_up",
               "end_kind"). k 3, after the test's pictures (the mod log's notices of its refusals cover the lower
               button): refusals: a handle never given ("unknown", LUMA_E_HANDLE), the screen's other pane under
               another screen's handle ("other_screen", LUMA_E_HANDLE), no such animation ("missing", LUMA_E_NOT_FOUND),
               a null name ("null_name", LUMA_E_ARGUMENT); then ButtonSelectIn on NBackDown with an end function
               ("light_down"), then at once ButtonSelectOut on it ("out_down"): the first one's end is LUMA_E_CANCELLED
               ("end_down"). */
#include "../loader/lt.h"
#include <luma_ui.h>

#ifndef CASE
#error CASE
#endif

static const LumaUiService* ui;
static LumaUiScreen screen;
static LumaUiPane up;
static LumaUiPane down;
__attribute__((unused)) static LumaUiPane label;
__attribute__((unused)) static LumaUiScreen other;

#if CASE == 2
static int32_t ends;
static void on_end(void* user, const LumaEvent* event) {
    (void)user;
    ends++;
    lt_fact("ends", ends);
    lt_fact("end", event->status);
    lt_fact("end_is_up", event->handle == up);
    lt_fact("end_kind", (int32_t)event->kind);
}

static void on_end_down(void* user, const LumaEvent* event) {
    (void)user;
    lt_fact("end_down", event->status);
    lt_fact("end_down_is_down", event->handle == down);
}
#endif

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 3800;
    if (k < 1 || k > 3) return 0;
    if (k == 1) {
        lt_fact("open", ui->open(lt_mod, "PauseMenu", LUMA_UI_HUD, &screen));
        lt_fact("pane_up", ui->pane(lt_mod, screen, "NBackUp", &up));
        lt_fact("pane_down", ui->pane(lt_mod, screen, "NBackDown", &down));
        lt_fact("appear", ui->play(lt_mod, screen, "Appear", false, 0, 0));
        lt_fact("appear_up", ui->play_pane(lt_mod, screen, up, "ButtonAppear", false, 0, 0));
        lt_fact("appear_down", ui->play_pane(lt_mod, screen, down, "ButtonAppear", false, 0, 0));
#if CASE == 1
        lt_fact("pane_label", ui->pane(lt_mod, screen, "Text1", &label));
        lt_fact("label", ui->set_text(lt_mod, label, "Kit A two words"));
#endif
    }
#if CASE == 2
    if (k == 2) {
        lt_fact("light", ui->play_pane(lt_mod, screen, up, "ButtonSelectIn", false, on_end, 0));
    }
    if (k == 3) {
        LumaUiPane other_pane = 0;
        lt_fact("unknown", ui->play_pane(lt_mod, screen, 999999, "ButtonSelectIn", false, 0, 0));
        lt_fact("open_other", ui->open(lt_mod, "CoinCounter", LUMA_UI_HUD, &other));
        lt_fact("pane_other", ui->pane(lt_mod, other, "Counter", &other_pane));
        lt_fact("other_screen", ui->play_pane(lt_mod, screen, other_pane, "ButtonSelectIn", false, 0, 0));
        lt_fact("missing", ui->play_pane(lt_mod, screen, down, "JuNoSuchAnimation", false, 0, 0));
        lt_fact("null_name", ui->play_pane(lt_mod, screen, down, 0, false, 0, 0));
        lt_fact("light_down", ui->play_pane(lt_mod, screen, down, "ButtonSelectIn", false, on_end_down, 0));
        lt_fact("out_down", ui->play_pane(lt_mod, screen, down, "ButtonSelectOut", false, 0, 0));
    }
#endif
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(ui, LUMA_UI_NAME, "1.3"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
