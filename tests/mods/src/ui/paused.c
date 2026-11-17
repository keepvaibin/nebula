/* ju-paused (3813): a ui screen first made while the game is paused. PauseMenu::appear (after) opens CoinCounter on
   the pause layer and sets its Counter to "34": the screen is made then, in a stopped scene. Facts: "open", "pane",
   "text" (statuses), "opens" (how many opens). The ui.screen fact's "drawn" says the game draws it this frame, which
   needs it connected to its draw list. */
#include "../loader/lt.h"
#include <luma_ui.h>

static const LumaUiService* ui;
static LumaUiScreen screen;
static LumaUiPane pane;
static int32_t opens;

static int32_t on_pause(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (screen != 0) return 0;
    const int32_t status = ui->open(lt_mod, "CoinCounter", LUMA_UI_PAUSE, &screen);
    lt_fact("open", status);
    if (status != LUMA_OK) return 0;
    opens++;
    lt_fact("opens", opens);
    lt_fact("pane", ui->pane(lt_mod, screen, "Counter", &pane));
    lt_fact("text", ui->set_text(lt_mod, pane, "34"));
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(ui, LUMA_UI_NAME, "1.0"),
    LUMA_HOOK_ON("PauseMenu::appear", "void()", LUMA_AFTER, on_pause),
};

LT_START({})
LUMA_MOD(imports, start, 0);
