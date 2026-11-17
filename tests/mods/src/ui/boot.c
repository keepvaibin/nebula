/* ju-boot (3812): a ui screen asked for from `start`, as a HUD mod does. Until a scene runs with the game's own fonts
   (at boot the program's built-in message font stands in, without digits, and the system archive's fonts replace it)
   open answers LUMA_E_BUSY with Ui.NoScene; the mod asks again each frame while it has no live screen, and sets the
   text "1230" on CoinCounter's Counter as soon as open gives one. A screen ends with its scene, so each scene gets
   its own. Facts: "start_open" (start's status), "open", "pane", "text" (each open that was not BUSY), "opens" (how
   many), "busy" (BUSY answers, written at the first and every 60th); run step MR::getRandom(0, 3812) on the stage:
   "stage_alive" (1 when the stage's screen is live), "stage_text" (the text set again). */
#include "../loader/lt.h"
#include <luma_time.h>
#include <luma_ui.h>

static const LumaUiService* ui;
static const LumaTimeService* tm;
static LumaUiScreen screen;
static LumaUiPane pane;
static int32_t opens;
static int32_t busy;

static int32_t try_open(void) {
    LumaUiScreen made = 0;
    const int32_t status = ui->open(lt_mod, "CoinCounter", LUMA_UI_HUD, &made);
    if (status == LUMA_E_BUSY) {
        if (busy++ % 60 == 0) lt_fact("busy", busy);
        return status;
    }
    lt_fact("open", status);
    if (status != LUMA_OK) return status;
    screen = made;
    opens++;
    lt_fact("opens", opens);
    lt_fact("pane", ui->pane(lt_mod, screen, "Counter", &pane));
    lt_fact("text", ui->set_text(lt_mod, pane, "1230"));
    return status;
}

static void tick(void* user, const LumaTimeNow* now) {
    (void)user;
    (void)now;
    bool live = false;
    if (screen != 0 && ui->alive(lt_mod, screen, &live) == LUMA_OK && live) return;
    screen = 0;
    try_open();
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) != 3812) return 0;
    bool live = false;
    lt_fact("stage_alive", screen != 0 && ui->alive(lt_mod, screen, &live) == LUMA_OK && live ? 1 : 0);
    lt_fact("stage_text", ui->set_text(lt_mod, pane, "1230"));
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(ui, LUMA_UI_NAME, "1.1"),
    LUMA_SERVICE(tm, LUMA_TIME_NAME, "1.0"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({
    lt_fact("start_open", try_open());
    lt_fact("on_frame", tm->on_frame(lt_mod, tick, 0));
})
LUMA_MOD(imports, start, 0);
