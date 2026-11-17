/* J's ui conformance mods (contract ui; native/include/luma_ui.h), one case each (-DCASE=n). Run steps call
   MR::getRandom(0, 3800 + k) and the hook gives 1; k 1 to 6 are the case's steps. Each call's status is the fact
   mod.<id>.<name>. CoinCounter is a game layout (LayoutData/CoinCounter.arc): its pane "Counter" is a text box, its
   pane "CoinCounter" is not. JuNoSuchLayout is in no store.
   ju-open   (1) 3801: k 1 open CoinCounter on the HUD ("open"), its pane Counter ("pane"), the text "1234" ("text");
                  k 2 the pane hidden ("hide"); k 3 shown again ("show"); k 4 the screen hidden ("screen_hide").
   ju-args   (2) 3802: k 1 the refusals: a null and an empty layout, layer 2, a null out, JuNoSuchLayout ("missing":
                  not found); one good open ("good"); pane JuNoPane ("no_pane"), a pane of screen 999999
                  ("pane_unknown"), a null name; set_text on CoinCounter ("not_text"), bytes that are not UTF-8
                  ("not_utf8"), a null text ("null_text"), on pane 999999 ("text_unknown"); set_visible on 999999;
                  close ("close"), close again ("close2"), pane on the closed screen ("pane_closed").
   ju-text   (3) 3803: k 1 open, Counter; 300 'A's ("long": cut to 255, Ui.TextCut); k 2 Japanese ("japanese");
                  k 3 "" ("empty").
   ju-again  (4) 3804: k 1 open, Counter hidden, close; k 2 open again ("again": the same screen, reused), Counter
                  found again ("pane2": shown again).
   ju-limit  (5) 3805: k 1 16 opens of CoinCounter, each LUMA_OK ("ok": how many), the 17th ("over"); k 2 one close
                  ("close") and one open again ("again").
   ju-anim   (6) 3806: k 1 open, play Flash once with an end function ("play"), JuNoAnim ("missing"); the end comes
                  as "end" (its status) and "ends" (how many); k 2 play Wait looping ("loop"); k 3 stop ("stop": the
                  loop's end is LUMA_E_CANCELLED).
   ju-layout (7) 3807: the mod's own layout GlHud (G's gl-hud source, LayoutData/GlHud.arc/GlHud.layout.json, cooked at
                  install: Hud > Icon picture, Count text of 16, Hit bounding hidden; Banner > Title text). k 1 open it
                  on the HUD ("open"), Count ("count") set to "x 99" ("text"), Title ("title") set to Japanese
                  ("title_text"), Hit ("hit") shown ("hit_show"), Icon ("icon") given text ("icon_not_text"), 20
                  characters into Count ("cut": cut to 16, Ui.TextCut).
   ju-font   (8) 3808/3809: the mod's own layout GlFont (G's gl-font source; its texts name the mod's font
                  Luma/<id>/fonts/GlTitle, a TTF cooked at install). k 1 open it on the HUD ("open"), Small ("small")
                  set to "TOTAL 1230 " and an Omega the font lacks ("text": drawn by its fallback, MessageFont26).
                  ju-font-bad is the same code on gl-font-bad, whose font the cook refuses.
   ju-scale  (9) 3810 (ui 1.1): k 1 open CoinCounter, Counter set to "1234", scaled 2 across and 1.5 down ("scale"),
                  alive ("alive" LUMA_OK, "alive_is" 1); a NaN scale ("scale_nan"), 100 across ("scale_far"); k 2
                  close, alive again ("alive_closed" 0), a handle never given ("alive_unknown"), a null out
                  ("alive_null"), set_scale on the closed screen's pane ("scale_closed").
   ju-menu   (10) 3811 (ui 1.2): k 1 open CoinCounter and a second screen (CometTimer is not needed: the same layout
                  on the pause layer), menu_rows with Counter as its one row, on ("rows"), 33 rows ("rows_many"), null
                  rows with a count ("rows_null"), the other screen's pane ("rows_other"), menu_chosen at once
                  ("chosen_now" LUMA_OK, "chosen_row" -1), a null row ("chosen_null"), a time frame handler
                  ("on_frame"); from then on each frame's handler facts "taken", the row menu_chosen gives, when it is
                  not -1 (the harness holds A at 130); k 3 count 0 ("rows_none"). */
#include "../loader/lt.h"
#include <luma_ui.h>
#include <luma_time.h>

#ifndef CASE
#error CASE
#endif

static const LumaUiService* ui;
__attribute__((unused)) static LumaUiScreen screen;
__attribute__((unused)) static LumaUiPane pane;

#if CASE == 10
static const LumaTimeService* tm;
static LumaUiScreen other;
static LumaUiPane other_pane;
static int watching;
/* Each frame: the row taken at the last reading of the controls (ui 1.2), while the case watches. */
static void tick(void* user, const LumaTimeNow* now) {
    (void)user;
    (void)now;
    int32_t row = -1;
    if (watching && ui->menu_chosen(lt_mod, screen, &row) == LUMA_OK && row >= 0) lt_fact("taken", row);
}
#endif

#if CASE == 6
static int32_t ends;
static void on_end(void* user, const LumaEvent* event) {
    (void)user;
    ends++;
    lt_fact("ends", ends);
    lt_fact("end", event->status);
    lt_fact("end_kind", (int32_t)event->kind);
    lt_fact("end_screen", event->handle == screen);
}
#endif

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 3800;
    if (k < 1 || k > 7) return 0;
#if CASE == 1
    if (k == 1) {
        lt_fact("open", ui->open(lt_mod, "CoinCounter", LUMA_UI_HUD, &screen));
        lt_fact("pane", ui->pane(lt_mod, screen, "Counter", &pane));
        lt_fact("text", ui->set_text(lt_mod, pane, "12"));
    }
    if (k == 2) lt_fact("hide", ui->set_visible(lt_mod, pane, false));
    if (k == 3) lt_fact("show", ui->set_visible(lt_mod, pane, true));
    if (k == 4) lt_fact("screen_hide", ui->show(lt_mod, screen, false));
#elif CASE == 2
    if (k == 1) {
        LumaUiScreen none = 0;
        LumaUiPane no_pane = 0;
        lt_fact("null_layout", ui->open(lt_mod, 0, LUMA_UI_HUD, &none));
        lt_fact("empty_layout", ui->open(lt_mod, "", LUMA_UI_HUD, &none));
        lt_fact("bad_layer", ui->open(lt_mod, "CoinCounter", (LumaUiLayer)2, &none));
        lt_fact("null_out", ui->open(lt_mod, "CoinCounter", LUMA_UI_HUD, 0));
        lt_fact("missing", ui->open(lt_mod, "JuNoSuchLayout", LUMA_UI_HUD, &none));
        lt_fact("none_written", none == 0);
        lt_fact("good", ui->open(lt_mod, "CoinCounter", LUMA_UI_HUD, &screen));
        lt_fact("no_pane", ui->pane(lt_mod, screen, "JuNoPane", &no_pane));
        lt_fact("pane_unknown", ui->pane(lt_mod, 999999, "Counter", &no_pane));
        lt_fact("null_name", ui->pane(lt_mod, screen, 0, &no_pane));
        lt_fact("no_pane_written", no_pane == 0);
        LumaUiScreen hud = 0;
        LumaUiPane box = 0;
        lt_fact("hud", ui->open(lt_mod, "GlHud", LUMA_UI_HUD, &hud));
        lt_fact("box", ui->pane(lt_mod, hud, "Icon", &box));
        lt_fact("not_text", ui->set_text(lt_mod, box, "1"));
        lt_fact("pane", ui->pane(lt_mod, screen, "Counter", &pane));
        lt_fact("not_utf8", ui->set_text(lt_mod, pane, "\xC0\xAF"));
        lt_fact("null_text", ui->set_text(lt_mod, pane, 0));
        lt_fact("text_unknown", ui->set_text(lt_mod, 999999, "1"));
        lt_fact("visible_unknown", ui->set_visible(lt_mod, 999999, true));
        lt_fact("close", ui->close(lt_mod, screen));
        lt_fact("close2", ui->close(lt_mod, screen));
        lt_fact("pane_closed", ui->pane(lt_mod, screen, "Counter", &no_pane));
        lt_fact("text_closed", ui->set_text(lt_mod, pane, "1"));
    }
#elif CASE == 3
    if (k == 1) {
        static char many[301];
        for (int i = 0; i < 300; i++) many[i] = 'A';
        many[300] = 0;
        lt_fact("open", ui->open(lt_mod, "GlHud", LUMA_UI_HUD, &screen));
        lt_fact("pane", ui->pane(lt_mod, screen, "Title", &pane));
        lt_fact("long", ui->set_text(lt_mod, pane, many));
    }
    if (k == 2) lt_fact("japanese", ui->set_text(lt_mod, pane, "\xE3\x83\x9E\xE3\x83\xAA\xE3\x82\xAA 12"));
    if (k == 3) lt_fact("empty", ui->set_text(lt_mod, pane, ""));
#elif CASE == 4
    if (k == 1) {
        lt_fact("open", ui->open(lt_mod, "CoinCounter", LUMA_UI_HUD, &screen));
        lt_fact("pane", ui->pane(lt_mod, screen, "Counter", &pane));
        lt_fact("hide", ui->set_visible(lt_mod, pane, false));
        lt_fact("close", ui->close(lt_mod, screen));
    }
    if (k == 2) {
        lt_fact("again", ui->open(lt_mod, "CoinCounter", LUMA_UI_HUD, &screen));
        lt_fact("pane2", ui->pane(lt_mod, screen, "Counter", &pane));
    }
#elif CASE == 5
    static LumaUiScreen all[17];
    if (k == 1) {
        int32_t ok = 0;
        for (int i = 0; i < 16; i++) ok += ui->open(lt_mod, "CoinCounter", LUMA_UI_HUD, &all[i]) == LUMA_OK;
        lt_fact("ok", ok);
        lt_fact("over", ui->open(lt_mod, "CoinCounter", LUMA_UI_HUD, &all[16]));
    }
    if (k == 2) {
        lt_fact("close", ui->close(lt_mod, all[0]));
        lt_fact("again", ui->open(lt_mod, "CoinCounter", LUMA_UI_HUD, &all[0]));
    }
#elif CASE == 6
    if (k == 1) {
        lt_fact("open", ui->open(lt_mod, "CoinCounter", LUMA_UI_HUD, &screen));
        lt_fact("play", ui->play(lt_mod, screen, "Flash", false, on_end, 0));
        lt_fact("missing", ui->play(lt_mod, screen, "JuNoAnim", false, on_end, 0));
    }
    if (k == 2) lt_fact("loop", ui->play(lt_mod, screen, "Wait", true, on_end, 0));
    if (k == 3) lt_fact("stop", ui->stop(lt_mod, screen));
#elif CASE == 7
    if (k == 1) {
        LumaUiPane title = 0, hit = 0, icon = 0;
        lt_fact("open", ui->open(lt_mod, "GlHud", LUMA_UI_HUD, &screen));
        lt_fact("count", ui->pane(lt_mod, screen, "Count", &pane));
        lt_fact("text", ui->set_text(lt_mod, pane, "x 99"));
        lt_fact("title", ui->pane(lt_mod, screen, "Title", &title));
        lt_fact("title_text", ui->set_text(lt_mod, title, "\xE3\x83\x9E\xE3\x83\xAA\xE3\x82\xAA"));
        lt_fact("hit", ui->pane(lt_mod, screen, "Hit", &hit));
        lt_fact("hit_show", ui->set_visible(lt_mod, hit, true));
        lt_fact("icon", ui->pane(lt_mod, screen, "Icon", &icon));
        lt_fact("icon_not_text", ui->set_text(lt_mod, icon, "1"));
    }
    if (k == 2) lt_fact("cut", ui->set_text(lt_mod, pane, "x 12345678901234567"));
#elif CASE == 8
    if (k == 1) {
        lt_fact("open", ui->open(lt_mod, "GlFont", LUMA_UI_HUD, &screen));
        lt_fact("small", ui->pane(lt_mod, screen, "Small", &pane));
        lt_fact("text", ui->set_text(lt_mod, pane, "TOTAL 1230 \xCE\xA9"));
    }
#elif CASE == 10
    if (k == 1) {
        int32_t row = 5;
        LumaUiMenuRow one, other_row, many[LUMA_UI_MENU_ROWS_MOST + 1];
        lt_fact("open", ui->open(lt_mod, "CoinCounter", LUMA_UI_HUD, &screen));
        lt_fact("open_other", ui->open(lt_mod, "CoinCounter", LUMA_UI_PAUSE, &other));
        lt_fact("pane", ui->pane(lt_mod, screen, "Counter", &pane));
        lt_fact("pane_other", ui->pane(lt_mod, other, "Counter", &other_pane));
        one.pane = pane;
        one.enabled = true;
        other_row.pane = other_pane;
        other_row.enabled = true;
        lt_fact("rows", ui->menu_rows(lt_mod, screen, &one, 1));
        for (int i = 0; i <= (int)LUMA_UI_MENU_ROWS_MOST; i++) many[i] = one;
        lt_fact("rows_many", ui->menu_rows(lt_mod, screen, many, LUMA_UI_MENU_ROWS_MOST + 1));
        lt_fact("rows_null", ui->menu_rows(lt_mod, screen, 0, 1));
        lt_fact("rows_other", ui->menu_rows(lt_mod, screen, &other_row, 1));
        lt_fact("chosen_now", ui->menu_chosen(lt_mod, screen, &row));
        lt_fact("chosen_row", row);
        lt_fact("chosen_null", ui->menu_chosen(lt_mod, screen, 0));
        lt_fact("on_frame", tm->on_frame(lt_mod, tick, 0));
        watching = 1;
    }
    if (k == 3) {
        lt_fact("rows_none", ui->menu_rows(lt_mod, screen, 0, 0));
        watching = 0;
    }
#elif CASE == 9
    if (k == 1) {
        bool is = false;
        lt_fact("open", ui->open(lt_mod, "CoinCounter", LUMA_UI_HUD, &screen));
        lt_fact("pane", ui->pane(lt_mod, screen, "Counter", &pane));
        lt_fact("text", ui->set_text(lt_mod, pane, "1234"));
        lt_fact("scale", ui->set_scale(lt_mod, pane, 2.0f, 1.5f));
        lt_fact("alive", ui->alive(lt_mod, screen, &is));
        lt_fact("alive_is", is ? 1 : 0);
        lt_fact("scale_nan", ui->set_scale(lt_mod, pane, __builtin_nanf(""), 1.0f));
        lt_fact("scale_far", ui->set_scale(lt_mod, pane, 100.0f, 1.0f));
    }
    if (k == 2) {
        bool is = true;
        lt_fact("close", ui->close(lt_mod, screen));
        lt_fact("alive_closed_status", ui->alive(lt_mod, screen, &is));
        lt_fact("alive_closed", is ? 1 : 0);
        lt_fact("alive_unknown", ui->alive(lt_mod, 999999, &is));
        lt_fact("alive_null", ui->alive(lt_mod, screen, 0));
        lt_fact("scale_closed", ui->set_scale(lt_mod, pane, 1.0f, 1.0f));
    }
#endif
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
#if CASE == 10
    LUMA_SERVICE(ui, LUMA_UI_NAME, "1.2"),
    LUMA_SERVICE(tm, LUMA_TIME_NAME, "1.0"),
#elif CASE == 9
    LUMA_SERVICE(ui, LUMA_UI_NAME, "1.1"),
#else
    LUMA_SERVICE(ui, LUMA_UI_NAME, "1.0"),
#endif
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
