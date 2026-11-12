/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's draw service (plan 2.4; contract draw): a mod draws at named points of the game's frame. In 2-D over the
   game (rectangles, lines, triangles, images, text in the game's fonts and the mod's own, every language), clipped and
   transformed; in 3-D in the world, depth tested (lines, triangles, models, billboards). Textures come by content name
   or from the mod's own pixels. Everything a mod draws is drawn the frame it asks and never again: nothing stays in the
   renderer, and a mod that is off draws nothing from that frame. Import: LUMA_SERVICE(slot, "draw", "1.0").

   Units. 2-D: the game's layout units, the frame its HUD is laid out in: (0, 0) the screen's top left, x right, y down,
   456 units high, as wide as the screen's shape makes it (608 at 4:3, 832 at 16:9, more on wider screens); the
   HUD's own 4:3 frame (608 x 456) centred in it is the safe frame (screen's safe_*). 3-D: the game's world units
   (centimetres), its axes. */
#ifndef LUMA_DRAW_H
#define LUMA_DRAW_H

#include <stdint.h>

#include "luma_services.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_DRAW_NAME "draw"
#define LUMA_DRAW_MAJOR 1u
#define LUMA_DRAW_MINOR 0u

/* Per mod. Primitives a frame (a rect, line, triangle, image, glyph or 3-D primitive each counts 1; a mesh its
   triangles): past it the call draws nothing and returns LUMA_E_LIMIT (reported once a frame, Service.Limit). */
#define LUMA_DRAW_PRIMITIVES_MOST 20000u
/* Textures alive at once (by name and from pixels), and their bytes (width x height x 4 summed, from pixels). */
#define LUMA_DRAW_TEXTURES_MOST 256u
#define LUMA_DRAW_TEXTURE_BYTES_MOST (64ull * 1024ull * 1024ull)
/* A texture from pixels: at most this wide and high. */
#define LUMA_DRAW_TEXTURE_SIDE_MOST 4096u
/* Clip and transform pushes deep at once, in one point's call. */
#define LUMA_DRAW_STACK_MOST 16u
/* Fonts one mod has found. */
#define LUMA_DRAW_FONTS_MOST 32u
/* A text call's text: at most this many bytes (longer: drawn cut, LUMA_OK, Draw.TextCut in the mod log). */
#define LUMA_DRAW_TEXT_BYTES_MOST 4096u

/* The frame's points (draw.at). The world points are 3-D: drawn in the scene's camera, depth tested; the 2-D calls
   there return LUMA_E_STATE. The others are 2-D: the 3-D calls there return LUMA_E_STATE. */
typedef enum LumaDrawPoint {
    LUMA_DRAW_WORLD_BEFORE = 0,        /* the scene's camera set, before the game draws its world */
    LUMA_DRAW_WORLD_OPAQUE_AFTER = 1,  /* after the world's opaque surfaces, before its translucent ones */
    LUMA_DRAW_WORLD_AFTER = 2,         /* after the world and its effects, before the screen's 2-D */
    LUMA_DRAW_HUD_BEFORE = 3,          /* the screen's 2-D: before the game's HUD (under it) */
    LUMA_DRAW_HUD_AFTER = 4,           /* over the game's HUD, under its pause menu and its wipes */
    LUMA_DRAW_LOADING = 5,             /* while the game loads a scene (its loading screen), 2-D over it */
    LUMA_DRAW_PRESENT = 6              /* every frame the game shows, over everything (pause menu, wipes, loading) */
} LumaDrawPoint;
#define LUMA_DRAW_POINTS 7u

/* A texture: by content name (texture) or from pixels (texture_from_pixels). Owned by the mod until texture_free or
   its end. */
typedef LumaHandle LumaDrawTexture;
/* A font: by name (font). Kept until the mod's end. */
typedef LumaHandle LumaDrawFont;

typedef struct LumaDrawVec2 {
    float x, y;
} LumaDrawVec2;

typedef struct LumaDrawVec3 {
    float x, y, z;
} LumaDrawVec3;

typedef struct LumaDrawRect {
    float x, y; /* top left */
    float w, h; /* 0 or more */
} LumaDrawRect;

/* The 2-D frame now (screen, and the second argument of a point's function). */
typedef struct LumaDrawScreen {
    uint32_t size;    /* sizeof(LumaDrawScreen) as the mod was built */
    uint32_t point;   /* in a point's function: its LumaDrawPoint; from screen(): LUMA_DRAW_POINTS */
    uint64_t frame;   /* frames presented since start (time's `frame`) */
    float w, h;       /* the 2-D frame, layout units (h is 456) */
    float safe_x, safe_y, safe_w, safe_h; /* the HUD's 4:3 frame in it */
    uint32_t reserved[2]; /* 0 */
} LumaDrawScreen;

/* A point's function: called on the game thread at that point of every frame while it is set, through the kernel's
   guard (a fault takes the mod out); `screen` valid during the call only. The draw calls are valid inside it only. */
typedef void (*LumaDrawFn)(void* user, const LumaDrawScreen* screen);

/* Text placing (text, text_measure): where x is on each line. */
typedef enum LumaDrawAlign {
    LUMA_DRAW_LEFT = 0,   /* lines start at x */
    LUMA_DRAW_CENTRE = 1, /* lines centred on x */
    LUMA_DRAW_RIGHT = 2   /* lines end at x */
} LumaDrawAlign;

/* texture_from_pixels flags. */
enum {
    LUMA_DRAW_TEXTURE_NEAREST = 1u, /* sampled nearest (pixel art); else linear */
    LUMA_DRAW_TEXTURE_REPEAT = 2u   /* wraps outside 0-1 (image's src past the texture); else clamps */
};

typedef struct LumaDrawService {
    LumaServiceTable base;

    /* ---- Points ---- */

    /* 1.0. Game thread. fn(user, screen) at `point` of every frame from the next one on (a null fn removes it); one
       function a point a mod, the last set wins. Mods at one point are called in the Mods list's order, the bottom one
       first (the top one draws over). LUMA_OK. A point outside the enum: LUMA_E_ARGUMENT, reported. */
    int32_t (*at)(const LumaMod* mod, LumaDrawPoint point, LumaDrawFn fn, void* user);
    /* 1.0. Game thread, anywhere. The 2-D frame now. LUMA_OK. A null out, or a size below 1.0's struct (48):
       LUMA_E_ARGUMENT, reported (Service.StructSize). */
    int32_t (*screen)(const LumaMod* mod, LumaDrawScreen* out);

    /* ---- 2-D (the 2-D points only, else LUMA_E_STATE, reported) ----
       Every 2-D call: inside the clip on top (if any), through the transform on top (if any); colours 0xRRGGBBAA,
       blended by alpha over what is drawn. A null pointer argument, a NaN or infinite number: LUMA_E_ARGUMENT,
       reported. Past LUMA_DRAW_PRIMITIVES_MOST: LUMA_E_LIMIT. */

    /* 1.0. A filled rectangle. */
    int32_t (*rect)(const LumaMod* mod, const LumaDrawRect* rect, LumaColor colour);
    /* 1.0. A rectangle's outline `thickness` units wide, inside its edges (thickness above half the smaller side: the
       rectangle filled). thickness above 0. */
    int32_t (*rect_outline)(const LumaMod* mod, const LumaDrawRect* rect, float thickness, LumaColor colour);
    /* 1.0. A line from a to b, `thickness` units wide (above 0), square ends. */
    int32_t (*line)(const LumaMod* mod, LumaDrawVec2 a, LumaDrawVec2 b, float thickness, LumaColor colour);
    /* 1.0. A filled triangle, a colour each corner (blended between them). */
    int32_t (*tri)(const LumaMod* mod, const LumaDrawVec2 points[3], const LumaColor colours[3]);
    /* 1.0. `texture`'s part `src` (null: the whole; in texels) drawn into `dst`, times `colour` (0xFFFFFFFF as it is). A
       texture freed or never this mod's: the placeholder (magenta and black squares) drawn, LUMA_E_HANDLE, reported. */
    int32_t (*image)(const LumaMod* mod, LumaDrawTexture texture, const LumaDrawRect* dst, const LumaDrawRect* src,
                     LumaColor colour);
    /* 1.0. Text, UTF-8, in `font` at `size` units a line, from (x, y) its first line's top, placed by `align`; lines
       break at '\n', and at spaces (or between any two characters of languages written without them) past `wrap`
       units (0: no wrapping). "MessageFont26" draws the language played, its Japanese, Korean and Chinese faces
       included; a mod's font draws a character it lacks from its fallback game font (3.8); a character no font here
       has is drawn as the font's stand-in, Draw.NoGlyph in the mod log. `advance` (null: not written): where the text ended,
       from x (the end of its last line for LUMA_DRAW_LEFT), so text continues after it. Not UTF-8: LUMA_E_ARGUMENT, reported (Draw.NotUtf8); a font not this mod's: LUMA_E_HANDLE,
       reported; size above 0 and at most 456. */
    int32_t (*text)(const LumaMod* mod, LumaDrawFont font, const char* text, float x, float y, float size,
                    LumaColor colour, LumaDrawAlign align, float wrap, float* advance);
    /* 1.0. Anywhere (no point needed). The box text() would fill from (0, 0): out->x the widest line, out->y its
       height (lines times the line's height). Arguments as text. */
    int32_t (*text_measure)(const LumaMod* mod, LumaDrawFont font, const char* text, float size, float wrap,
                            LumaDrawVec2* out);
    /* 1.0. Drawing from here on is cut to `rect` (in the units the transform on top gives; made screen-aligned: the
       box around it) within the clip under it. Over LUMA_DRAW_STACK_MOST: LUMA_E_LIMIT, reported. */
    int32_t (*clip_push)(const LumaMod* mod, const LumaDrawRect* rect);
    /* 1.0. The clip under it again. None pushed in this point's call: LUMA_E_STATE, reported. */
    int32_t (*clip_pop)(const LumaMod* mod);
    /* 1.0. Points from here on go through `m` (x' = m[0] x + m[1] y + m[2], y' = m[3] x + m[4] y + m[5]) and then the
       transform under it. Over LUMA_DRAW_STACK_MOST: LUMA_E_LIMIT, reported. */
    int32_t (*transform_push)(const LumaMod* mod, const float m[6]);
    /* 1.0. The transform under it again. None pushed: LUMA_E_STATE, reported. */
    int32_t (*transform_pop)(const LumaMod* mod);

    /* ---- Fonts and textures (anywhere on the game thread) ---- */

    /* 1.0. A font by name: the game's ("MessageFont26": the language played's, with its Japanese, Korean and Chinese
       faces; "MenuFont64", "NumberFont", "PictureFont", "CinemaFont26") or the mod's own cooked font
       ("Luma/<id>/<path>", 3.8). LUMA_OK and *out (the same handle for the same name). A name neither: LUMA_E_NOT_FOUND
       (Content.MissingFont in the mod log); LUMA_DRAW_FONTS_MOST found: LUMA_E_LIMIT, reported. */
    int32_t (*font)(const LumaMod* mod, const char* name, LumaDrawFont* out);
    /* 1.0. A cooked texture by its path in the game's asset store: the mod's ("Luma/<id>/<path>", from its pack) or
       one of the game's (the path the importer wrote it to). LUMA_OK and *out. Not found:
       LUMA_E_NOT_FOUND (Content.MissingTexture in the mod log); LUMA_DRAW_TEXTURES_MOST alive: LUMA_E_LIMIT,
       reported. */
    int32_t (*texture)(const LumaMod* mod, const char* name, LumaDrawTexture* out);
    /* 1.0. A texture w x h (1 to LUMA_DRAW_TEXTURE_SIDE_MOST each) from `rgba8` (w x h x 4 bytes, rows top first, RGBA,
       straight alpha), copied; `flags` LUMA_DRAW_TEXTURE_*. Its bytes count against LUMA_DRAW_TEXTURE_BYTES_MOST. LUMA_OK and
       *out. A side of 0 or over the most: LUMA_E_TOO_LARGE, reported; past the textures' count or bytes:
       LUMA_E_LIMIT, reported. */
    int32_t (*texture_from_pixels)(const LumaMod* mod, uint32_t w, uint32_t h, const void* rgba8, uint32_t flags,
                                   LumaDrawTexture* out);
    /* 1.0. Part of a texture from pixels replaced: the rectangle (x, y, w, h) in texels from `rgba8` (`stride` bytes a
       row, at least w x 4), seen from the next draw. A texture by name, or a rectangle outside it: LUMA_E_ARGUMENT,
       reported; not this mod's or freed: LUMA_E_HANDLE, reported. */
    int32_t (*texture_update)(const LumaMod* mod, LumaDrawTexture texture, uint32_t x, uint32_t y, uint32_t w,
                              uint32_t h, const void* rgba8, uint32_t stride);
    /* 1.0. The texture's size in texels into `w` (null: not written) and `h` (null: not written). LUMA_OK; not this mod's or freed: LUMA_E_HANDLE,
       reported. */
    int32_t (*texture_size)(const LumaMod* mod, LumaDrawTexture texture, uint32_t* w, uint32_t* h);
    /* 1.0. The texture freed (drawn this frame already: shown this frame still). LUMA_OK; not this mod's or freed:
       LUMA_E_HANDLE, reported. */
    int32_t (*texture_free)(const LumaMod* mod, LumaDrawTexture texture);

    /* ---- 3-D (the world points only, else LUMA_E_STATE, reported): depth tested, unlit ----
       Arguments as the 2-D calls. Plan 2.4 layer 3 (FB): until it lands each returns LUMA_E_ABSENT at a world point. */

    /* 1.0. A line from a to b, `thickness` world units wide (above 0), facing the camera. */
    int32_t (*line3)(const LumaMod* mod, const LumaDrawVec3* a, const LumaDrawVec3* b, float thickness,
                     LumaColor colour);
    /* 1.0. A filled triangle, a colour each corner, both sides. */
    int32_t (*tri3)(const LumaMod* mod, const LumaDrawVec3 points[3], const LumaColor colours[3]);
    /* 1.0. A cooked model by name (the mod's "Luma/<id>/<path>", 3.2, or the game's object model name) at `matrix`
       (3 rows of 4: rotation and scale, then the place in the 4th column), times `colour`, in its own materials.
       Not found: LUMA_E_NOT_FOUND (Content.MissingModel in the mod log). */
    int32_t (*mesh)(const LumaMod* mod, const char* model, const float matrix[12], LumaColor colour);
    /* 1.0. `texture` on a w x h quad centred at `pos`, facing the camera, times `colour`. A texture freed: the
       placeholder, LUMA_E_HANDLE, reported. */
    int32_t (*billboard)(const LumaMod* mod, LumaDrawTexture texture, const LumaDrawVec3* pos, float w, float h,
                         LumaColor colour);
} LumaDrawService;

#ifdef __cplusplus
}
#endif

#endif
