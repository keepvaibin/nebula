/* ju-share (3815, ui 1.3 share_rows and shared_focus): two mod rows join the game's own menu focus under the pause
   menu's buttons. PauseMenu::appear (after) opens the mod's layout JuRow (share/LayoutData/JuRow.arc, cooked at
   install: one picture pane Row) twice on the pause layer, so the two rows are on two screens: row 1 placed at 0,-122
   (between the menu's lower button, centred at 0,-73, and its Settings button at 0,-161), row 2 at 0,-215 (under the
   Settings button). Facts "open_1", "open_2", "pane_1", "pane_2", "move_1", "move_2", "share"; refusals: null rows
   with a count and 33 rows LUMA_E_ARGUMENT ("null_rows", "many"), a handle never given LUMA_E_HANDLE ("unknown"),
   shared_focus with a null out LUMA_E_ARGUMENT ("focus_null"); "share" again after them (the refusals left the rows).
   A time frame handler reads shared_focus each frame: "focused" (1 row 1, 2 row 2, 0 none) when it changes, "chosen"
   (1 or 2) when a row was taken. PauseMenu::kill (after): "closed" 1, both screens closed ("close_1", "close_2"), and
   shared_focus then answers 0 and 0 ("after_focused", "after_chosen"). */
#include "../loader/lt.h"
#include <luma_time.h>
#include <luma_ui.h>

static const LumaUiService* ui;
static const LumaTimeService* tm;
static LumaUiScreen screen_1;
static LumaUiScreen screen_2;
static LumaUiPane row_1;
static LumaUiPane row_2;
static int32_t focused_was = -1;
static int watching;

static int32_t which(LumaUiPane pane) {
    return pane == 0 ? 0 : pane == row_1 ? 1 : pane == row_2 ? 2 : -1;
}

static void tick(void* user, const LumaTimeNow* now) {
    (void)user;
    (void)now;
    if (!watching) return;
    LumaUiPane focused = 0;
    LumaUiPane chosen = 0;
    if (ui->shared_focus(lt_mod, &focused, &chosen) != LUMA_OK) return;
    if (which(focused) != focused_was) {
        focused_was = which(focused);
        lt_fact("focused", focused_was);
    }
    if (chosen != 0) lt_fact("chosen", which(chosen));
}

static int32_t on_pause(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (screen_1 != 0) return 0;
    lt_fact("open_1", ui->open(lt_mod, "JuRow", LUMA_UI_PAUSE, &screen_1));
    lt_fact("open_2", ui->open(lt_mod, "JuRow", LUMA_UI_PAUSE, &screen_2));
    lt_fact("pane_1", ui->pane(lt_mod, screen_1, "Row", &row_1));
    lt_fact("pane_2", ui->pane(lt_mod, screen_2, "Row", &row_2));
    lt_fact("move_1", ui->set_position(lt_mod, row_1, 0.0f, -122.0f));
    lt_fact("move_2", ui->set_position(lt_mod, row_2, 0.0f, -215.0f));
    LumaUiMenuRow rows[2] = {{row_1, true}, {row_2, true}};
    LumaUiMenuRow many[LUMA_UI_MENU_ROWS_MOST + 1];
    for (uint32_t i = 0; i <= LUMA_UI_MENU_ROWS_MOST; i++) many[i] = rows[0];
    LumaUiMenuRow unknown = {999999, true};
    LumaUiPane out = 0;
    lt_fact("share", ui->share_rows(lt_mod, rows, 2));
    lt_fact("null_rows", ui->share_rows(lt_mod, 0, 1));
    lt_fact("many", ui->share_rows(lt_mod, many, LUMA_UI_MENU_ROWS_MOST + 1));
    lt_fact("unknown", ui->share_rows(lt_mod, &unknown, 1));
    lt_fact("focus_null", ui->shared_focus(lt_mod, 0, &out));
    lt_fact("share", ui->share_rows(lt_mod, rows, 2));
    watching = 1;
    return 0;
}

static int32_t on_menu_kill(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (screen_1 == 0 || !watching) return 0;
    watching = 0;
    lt_fact("closed", 1);
    lt_fact("close_1", ui->close(lt_mod, screen_1));
    lt_fact("close_2", ui->close(lt_mod, screen_2));
    LumaUiPane focused = 7;
    LumaUiPane chosen = 7;
    if (ui->shared_focus(lt_mod, &focused, &chosen) == LUMA_OK) {
        lt_fact("after_focused", focused == 0 ? 0 : 1);
        lt_fact("after_chosen", chosen == 0 ? 0 : 1);
    }
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(ui, LUMA_UI_NAME, "1.3"),
    LUMA_SERVICE(tm, LUMA_TIME_NAME, "1.0"),
    LUMA_HOOK_ON("PauseMenu::appear", "void()", LUMA_AFTER, on_pause),
    LUMA_HOOK_ON("PauseMenu::kill", "void()", LUMA_AFTER, on_menu_kill),
};

LT_START({ lt_fact("on_frame", tm->on_frame(lt_mod, tick, 0)); })
LUMA_MOD(imports, start, 0);
