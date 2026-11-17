/* J's draw conformance mods (contract draw; native/include/luma_draw.h, plan 2.4 layers 1 and 2), one case each
   (-DCASE=n). Run steps call MR::getRandom(0, 2400 + k) and the hook gives 1; k 1 to 4 are the case's steps. Each
   call's status is the fact mod.<id>.<name> (the first frame's, for calls made every frame).
   jd-shapes (1) 2401: k 1 draw at hud.after every frame: a red rectangle (40,40 120x60), a green outline 4 wide
                  (200,40 120x60), a blue line 6 wide (360,40 to 480,100), a triangle red/green/blue (520,100
                  580,40 640,100), text "Draw 1.0 ABC" in MessageFont26 at 24 (40,120), the same centred on 304
                  (300 wide wrap), a 4x4 checker from pixels drawn nearest at 64x64 (40,200), a yellow rectangle
                  (280,180 120x120) clipped to (300,200 60x60), a white 40x40 square turned 45 degrees about
                  (500,240); facts: each status, "measure_w"/"measure_h" (text_measure, whole units), "advance".
                  k 2 at(hud.after, null): nothing drawn from the next frame ("off").
   jd-args   (2) 2402: the refusals: k 1 outside a point rect ("outside": E_STATE), at point 7 ("bad_point": E_ARGUMENT),
                  font "JdNoFont" ("no_font": E_NOT_FOUND), texture "Luma/jd-args/none" ("no_texture"), 8192x8 pixels
                  ("too_large": E_TOO_LARGE), a null rect at screen ("screen_null"), then at hud.after once: clip_pop
                  with nothing ("pop": E_STATE), text not UTF-8 ("not_utf8": E_ARGUMENT), a NaN rect ("nan"), line3 at a
                  2-D point ("line3": E_STATE), a freed texture drawn ("freed": E_HANDLE, the checker at 40,40 64x64, drawn again every later frame),
                  20,001 rectangles ("drawn": how many LUMA_OK, "over": the 20,001st, E_LIMIT); the next frame's first
                  rectangle LUMA_OK again ("next").
   jd-points (3) 2403: k 1 a function at each of the seven points counting its calls; k 2 the counts as facts
                  ("calls0" .. "calls6"), the screen's w, h, safe_x, safe_w ("w", "h", "safe_x", "safe_w"), line3 at
                  world.after ("world_line3": LUMA_OK with FB's layer 3, fb/2.4-world), hud.before's rectangle under the HUD.
   jd-text   (4) 2404: k 1 at hud.after: text in each game font (MessageFont26, MenuFont64, NumberFont, CinemaFont26)
                  at 20 down the left; Japanese in MessageFont26 ("japanese": LUMA_OK; Draw.NoGlyph when the language
                  played has no kana); a 200-unit wrap of a long line ("wrap_h": its measured height over one line's,
                  at least 2); text under a transform scaled 2 ("scaled"). */
#include "../loader/lt.h"
#include <luma_draw.h>

#ifndef CASE
#error CASE
#endif

static const LumaDrawService* draw;
__attribute__((unused)) static int32_t frames;

#define RGBA(r, g, b, a) (((uint32_t)(r) << 24) | ((uint32_t)(g) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(a))

#if CASE == 1
static LumaDrawFont message;
static LumaDrawTexture checker;

static void paint(void* user, const LumaDrawScreen* screen) {
    (void)user;
    (void)screen;
    const int first = frames++ == 0;
    LumaDrawRect r = {40, 40, 120, 60};
    int32_t s = draw->rect(lt_mod, &r, RGBA(255, 0, 0, 255));
    if (first) lt_fact("rect", s);
    LumaDrawRect o = {200, 40, 120, 60};
    s = draw->rect_outline(lt_mod, &o, 4, RGBA(0, 255, 0, 255));
    if (first) lt_fact("outline", s);
    LumaDrawVec2 a = {360, 40};
    LumaDrawVec2 b = {480, 100};
    s = draw->line(lt_mod, a, b, 6, RGBA(0, 0, 255, 255));
    if (first) lt_fact("line", s);
    const LumaDrawVec2 tri[3] = {{520, 100}, {580, 40}, {640, 100}};
    const LumaColor colours[3] = {RGBA(255, 0, 0, 255), RGBA(0, 255, 0, 255), RGBA(0, 0, 255, 255)};
    s = draw->tri(lt_mod, tri, colours);
    if (first) lt_fact("tri", s);
    float advance = -1;
    s = draw->text(lt_mod, message, "Draw 1.0 ABC", 40, 120, 24, RGBA(255, 255, 255, 255), LUMA_DRAW_LEFT, 0, &advance);
    if (first) {
        lt_fact("text", s);
        lt_fact("advance", (long long)advance);
    }
    s = draw->text(lt_mod, message, "Centred on 304", 304, 150, 24, RGBA(255, 255, 0, 255), LUMA_DRAW_CENTRE, 300, 0);
    if (first) lt_fact("centred", s);
    LumaDrawRect img = {40, 200, 64, 64};
    s = draw->image(lt_mod, checker, &img, 0, RGBA(255, 255, 255, 255));
    if (first) lt_fact("image", s);
    LumaDrawRect clip = {300, 200, 60, 60};
    s = draw->clip_push(lt_mod, &clip);
    if (first) lt_fact("clip_push", s);
    LumaDrawRect big = {280, 180, 120, 120};
    (void)draw->rect(lt_mod, &big, RGBA(255, 255, 0, 255));
    s = draw->clip_pop(lt_mod);
    if (first) lt_fact("clip_pop", s);
    /* 45 degrees about (500, 240): translate(500,240) . rotate(45) . translate(-20,-20). */
    const float c = 0.70710678f;
    const float m[6] = {c, -c, 500.0f, c, c, 240.0f};
    s = draw->transform_push(lt_mod, m);
    if (first) lt_fact("transform_push", s);
    LumaDrawRect square = {-20, -20, 40, 40};
    (void)draw->rect(lt_mod, &square, RGBA(255, 255, 255, 255));
    s = draw->transform_pop(lt_mod);
    if (first) lt_fact("transform_pop", s);
}
#elif CASE == 2
static LumaDrawTexture freed;
/* A font of this mod's, so the text refused is refused for its bytes (Draw.NotUtf8), not its handle. */
static LumaDrawFont good;

static void paint(void* user, const LumaDrawScreen* screen) {
    (void)user;
    (void)screen;
    if (frames++ >= 1) {
        if (frames == 2) {
            LumaDrawRect r = {0, 0, 1, 1};
            lt_fact("next", draw->rect(lt_mod, &r, RGBA(0, 0, 0, 0)));
        }
        /* Every later frame draws the freed texture again (its report counted, not said again), so the picture shows
           the checker it draws in its place. */
        LumaDrawRect img = {40, 40, 64, 64};
        (void)draw->image(lt_mod, freed, &img, 0, RGBA(255, 255, 255, 255));
        return;
    }
    lt_fact("pop", draw->clip_pop(lt_mod));
    lt_fact("not_utf8", draw->text(lt_mod, good, "\xC3\x28", 0, 0, 20, RGBA(255, 255, 255, 255), LUMA_DRAW_LEFT, 0, 0));
    LumaDrawRect bad = {__builtin_nanf(""), 0, 10, 10};
    lt_fact("nan", draw->rect(lt_mod, &bad, RGBA(255, 255, 255, 255)));
    LumaDrawVec3 p = {0, 0, 0};
    lt_fact("line3", draw->line3(lt_mod, &p, &p, 1, RGBA(255, 255, 255, 255)));
    LumaDrawRect img = {40, 40, 64, 64};
    lt_fact("freed", draw->image(lt_mod, freed, &img, 0, RGBA(255, 255, 255, 255)));
    /* The freed image counted one; 19,999 more are LUMA_OK, the next is over. */
    int32_t drawn = 1;
    int32_t over = 0;
    LumaDrawRect r = {200, 200, 1, 1};
    for (int i = 0; i < 20000; i++) {
        const int32_t s = draw->rect(lt_mod, &r, RGBA(0, 0, 0, 0));
        if (s == LUMA_OK) {
            drawn++;
        } else {
            over = s;
        }
    }
    lt_fact("drawn", drawn);
    lt_fact("over", over);
}
#elif CASE == 3
static int32_t calls[LUMA_DRAW_POINTS];
static int32_t world_line3 = 1;

static void counted(void* user, const LumaDrawScreen* screen) {
    const uint32_t p = (uint32_t)(uintptr_t)user;
    if (p < LUMA_DRAW_POINTS) calls[p]++;
    if (p == LUMA_DRAW_WORLD_AFTER && world_line3 == 1) {
        LumaDrawVec3 a = {0, 0, 0};
        LumaDrawVec3 b = {0, 100, 0};
        world_line3 = draw->line3(lt_mod, &a, &b, 2, RGBA(255, 0, 0, 255));
    }
    if (p == LUMA_DRAW_HUD_BEFORE) {
        LumaDrawRect r = {screen->safe_x, 0, screen->safe_w, screen->h};
        (void)draw->rect(lt_mod, &r, RGBA(0, 0, 64, 160));
    }
}
#elif CASE == 4
static LumaDrawFont fonts[4];

static void paint(void* user, const LumaDrawScreen* screen) {
    (void)user;
    (void)screen;
    const int first = frames++ == 0;
    static const char* const names[4] = {"MessageFont26", "MenuFont64", "NumberFont", "CinemaFont26"};
    for (int i = 0; i < 4; i++) {
        const int32_t s = draw->text(lt_mod, fonts[i], i == 2 ? "0123456789" : names[i], 40, 40 + 30 * i, 20,
                                     RGBA(255, 255, 255, 255), LUMA_DRAW_LEFT, 0, 0);
        if (first && i == 0) lt_fact("message", s);
    }
    int32_t s = draw->text(lt_mod, fonts[0], "\xE3\x83\x9E\xE3\x83\xAA\xE3\x82\xAA", 40, 170, 24, RGBA(255, 220, 0, 255),
                           LUMA_DRAW_LEFT, 0, 0);
    if (first) lt_fact("japanese", s);
    s = draw->text(lt_mod, fonts[0], "A long line of words that wraps inside two hundred units", 300, 40, 20,
                   RGBA(200, 255, 200, 255), LUMA_DRAW_LEFT, 200, 0);
    if (first) lt_fact("wrap", s);
    const float m[6] = {2, 0, 300, 0, 2, 220};
    (void)draw->transform_push(lt_mod, m);
    s = draw->text(lt_mod, fonts[0], "x2", 0, 0, 20, RGBA(255, 128, 128, 255), LUMA_DRAW_LEFT, 0, 0);
    (void)draw->transform_pop(lt_mod);
    if (first) lt_fact("scaled", s);
}
#endif

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 2400;
    if (k < 1 || k > 4) return 0;
#if CASE == 1
    if (k == 1) {
        lt_fact("font", draw->font(lt_mod, "MessageFont26", &message));
        uint8_t pixels[4 * 4 * 4];
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                uint8_t* p = pixels + (y * 4 + x) * 4;
                const int lit = ((x ^ y) & 1) == 0;
                p[0] = lit ? 255 : 0;
                p[1] = lit ? 160 : 0;
                p[2] = 0;
                p[3] = 255;
            }
        }
        lt_fact("pixels", draw->texture_from_pixels(lt_mod, 4, 4, pixels, LUMA_DRAW_TEXTURE_NEAREST, &checker));
        uint32_t w = 0;
        uint32_t h = 0;
        lt_fact("size", draw->texture_size(lt_mod, checker, &w, &h));
        lt_fact("size_wh", (long long)(w * 100 + h));
        LumaDrawVec2 box = {0, 0};
        lt_fact("measure", draw->text_measure(lt_mod, message, "Draw 1.0 ABC", 24, 0, &box));
        lt_fact("measure_w", (long long)box.x);
        lt_fact("measure_h", (long long)box.y);
        lt_fact("at", draw->at(lt_mod, LUMA_DRAW_HUD_AFTER, paint, 0));
    }
    if (k == 2) lt_fact("off", draw->at(lt_mod, LUMA_DRAW_HUD_AFTER, 0, 0));
#elif CASE == 2
    if (k == 1) {
        LumaDrawRect r = {0, 0, 10, 10};
        lt_fact("outside", draw->rect(lt_mod, &r, RGBA(255, 255, 255, 255)));
        lt_fact("bad_point", draw->at(lt_mod, (LumaDrawPoint)7, paint, 0));
        LumaDrawFont font = 0;
        lt_fact("no_font", draw->font(lt_mod, "JdNoFont", &font));
        lt_fact("good_font", draw->font(lt_mod, "MessageFont26", &good));
        LumaDrawTexture none = 0;
        lt_fact("no_texture", draw->texture(lt_mod, "Luma/jd-args/none", &none));
        static uint8_t row[8 * 4];
        lt_fact("too_large", draw->texture_from_pixels(lt_mod, 8192, 1, row, 0, &none));
        lt_fact("none_written", none == 0 && font == 0);
        lt_fact("screen_null", draw->screen(lt_mod, 0));
        lt_fact("make", draw->texture_from_pixels(lt_mod, 2, 1, row, 0, &freed));
        lt_fact("free", draw->texture_free(lt_mod, freed));
        lt_fact("free_again", draw->texture_free(lt_mod, freed));
        lt_fact("at", draw->at(lt_mod, LUMA_DRAW_HUD_AFTER, paint, 0));
    }
#elif CASE == 3
    if (k == 1) {
        for (uint32_t p = 0; p < LUMA_DRAW_POINTS; p++) {
            const int32_t s = draw->at(lt_mod, (LumaDrawPoint)p, counted, (void*)(uintptr_t)p);
            if (s != LUMA_OK) lt_fact("at_failed", (long long)p);
        }
        lt_fact("at", LUMA_OK);
    }
    if (k == 2) {
        char key[8] = {'c', 'a', 'l', 'l', 's', '0', 0, 0};
        for (uint32_t p = 0; p < LUMA_DRAW_POINTS; p++) {
            key[5] = (char)('0' + p);
            lt_fact(key, calls[p]);
        }
        LumaDrawScreen screen = {0};
        screen.size = sizeof(screen);
        lt_fact("screen", draw->screen(lt_mod, &screen));
        lt_fact("w", (long long)screen.w);
        lt_fact("h", (long long)screen.h);
        lt_fact("safe_x", (long long)screen.safe_x);
        lt_fact("safe_w", (long long)screen.safe_w);
        lt_fact("world_line3", world_line3);
    }
#elif CASE == 4
    if (k == 1) {
        static const char* const names[4] = {"MessageFont26", "MenuFont64", "NumberFont", "CinemaFont26"};
        for (int i = 0; i < 4; i++) {
            const int32_t s = draw->font(lt_mod, names[i], &fonts[i]);
            if (s != LUMA_OK) lt_fact("font_failed", i);
        }
        LumaDrawVec2 one = {0, 0};
        LumaDrawVec2 wrapped = {0, 0};
        (void)draw->text_measure(lt_mod, fonts[0], "A", 20, 0, &one);
        (void)draw->text_measure(lt_mod, fonts[0], "A long line of words that wraps inside two hundred units", 20, 200,
                                 &wrapped);
        lt_fact("wrap_h", one.y > 0 ? (long long)(wrapped.y / one.y) : 0);
        lt_fact("at", draw->at(lt_mod, LUMA_DRAW_HUD_AFTER, paint, 0));
    }
#endif
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(draw, LUMA_DRAW_NAME, "1.0"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
