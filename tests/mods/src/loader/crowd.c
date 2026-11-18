/* lt-crowd (loader 1140-many-mods, N-3008): placed 600 times (the test's "copies"), each copy its own id and library:
   past the 256 mods the loader held, the 512 records and clocks the services and time held, the 256 mods a draw point
   held, the 1024 textures and 128 fonts across mods draw held, the 512 watches events held, and ui's 256 open screens
   and 512 animation ends across mods. MR::getRandom(1140, 1140) gives 1140 and each copy's after hook adds 1 (the call
   gives 1740 when every copy's hook runs), and on the first such call each copy watches an event (watch), draws at a
   point (at), makes two textures from pixels (tex, tex2), takes the game's message font (font), opens the game's
   CoinCounter (ui), finds its pane Counter (pane) and plays its Wait looping with an end function (play): each status
   its fact. MR::getRandom(1141, 1141): each copy whose screen, pane and animation all came adds 1. */
#include "lt.h"
#include <luma_draw.h>
#include <luma_events.h>
#include <luma_ui.h>

static const LumaDrawService* draw;
static const LumaEventsService* ev;
static const LumaUiService* ui;
static int32_t asked;
static int32_t shown;

static void drawn(void* user, const LumaDrawScreen* screen) {
    (void)user;
    (void)screen;
}

static void told(void* user, const LumaEvent* event) {
    (void)user;
    (void)event;
}

static int32_t add(LumaCall* call, void* user) {
    (void)user;
    const int32_t a = LUMA_ARG(call, 1, int32_t);
    if (a != LUMA_ARG(call, 2, int32_t)) return 0;
    if (a == 1141) {
        LUMA_RESULT(call, int32_t) += shown;
        return 0;
    }
    if (a != 1140) return 0;
    LUMA_RESULT(call, int32_t) += 1;
    if (asked) return 0;
    asked = 1;
    static const uint8_t white[4] = {255, 255, 255, 255};
    LumaDrawTexture tex = 0, tex2 = 0;
    LumaDrawFont font = 0;
    LumaUiScreen screen = 0;
    LumaUiPane pane = 0;
    lt_fact("watch", ev->watch(lt_mod, 1u << LUMA_EVENTS_EVENT_STAGE_FRAME, told, 0));
    lt_fact("at", draw->at(lt_mod, LUMA_DRAW_HUD_BEFORE, drawn, 0));
    lt_fact("tex", draw->texture_from_pixels(lt_mod, 1, 1, white, 0, &tex));
    lt_fact("tex2", draw->texture_from_pixels(lt_mod, 1, 1, white, 0, &tex2));
    lt_fact("font", draw->font(lt_mod, "MessageFont26", &font));
    const int32_t opened = ui->open(lt_mod, "CoinCounter", LUMA_UI_HUD, &screen);
    const int32_t found = opened == 0 ? ui->pane(lt_mod, screen, "Counter", &pane) : opened;
    const int32_t played = opened == 0 ? ui->play(lt_mod, screen, "Wait", true, told, 0) : opened;
    lt_fact("ui", opened);
    lt_fact("pane", found);
    lt_fact("play", played);
    shown = opened == 0 && found == 0 && played == 0;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(draw, LUMA_DRAW_NAME, "1.0"),
    LUMA_SERVICE(ev, LUMA_EVENTS_NAME, "1.0"),
    LUMA_SERVICE(ui, LUMA_UI_NAME, "1.0"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, add),
};

LT_START({})
LUMA_MOD(imports, start, 0);
