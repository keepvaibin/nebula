/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's ui service (plan 2.4 layer 4 on 3.8 layer 1; contract ui): a screen layout of the mod's or the game's,
   shown over the game as the game shows its own (the game's layout drawer: picture and text panes, its fonts, its
   16:9 widening), its panes found by name, their text set and their visibility. Import: LUMA_SERVICE(slot, "ui",
   "1.0"). */
#ifndef LUMA_UI_H
#define LUMA_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "luma_services.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_UI_NAME "ui"
#define LUMA_UI_MAJOR 1u
#define LUMA_UI_MINOR 0u

/* Screens one mod has open at once, and panes it has found across them. */
#define LUMA_UI_MOST_SCREENS 16u
#define LUMA_UI_MOST_PANES 256u
/* A text pane's text: at most this many UTF-16 units (a longer text is shown cut, with a note). */
#define LUMA_UI_TEXT_MOST 255u

/* An open screen: owned by the mod, ended by close, by its scene's end, or by the mod's. */
typedef LumaHandle LumaUiScreen;
/* A pane of an open screen: ends with its screen. */
typedef LumaHandle LumaUiPane;

/* Where a screen is drawn. */
typedef enum LumaUiLayer {
    LUMA_UI_HUD = 0,   /* with the game's HUD: drawn and moving while the game runs, stopped under its pause menu */
    LUMA_UI_PAUSE = 1  /* drawn and moving while the game is paused as well, over its pause menu */
} LumaUiLayer;

typedef struct LumaUiService {
    LumaServiceTable base;
    /* 1.0. Game thread, in a scene. The layout `layout` (LayoutData/<layout>.arc: the mod's cooked one, 3.8, or the
       game's) opened on `layer`, shown. LUMA_OK and *out. A layout in no store: LUMA_E_NOT_FOUND, Ui.MissingLayout
       in the mod log. No scene running (the game is loading one): LUMA_E_BUSY, reported (Ui.NoScene). An empty or
       null name, a layer outside the enum, a null out: LUMA_E_ARGUMENT, reported; LUMA_UI_MOST_SCREENS open (or 128
       made in one scene): LUMA_E_LIMIT, reported. Off the game thread: LUMA_E_THREAD, reported; while the game takes
       the mod out: LUMA_E_CANCELLED. A layout opened again on the layer after this mod closed it may be that screen
       again: its panes' visibility is the layout's again, their text as the mod left it. */
    int32_t (*open)(const LumaMod* mod, const char* layout, LumaUiLayer layer, LumaUiScreen* out);
    /* 1.0. Game thread. The pane named `name` (exactly, the first in the tree's order) of `screen`. LUMA_OK and *out
       (the same handle for the same pane). No pane of that name: LUMA_E_NOT_FOUND, Ui.MissingPane in the mod log. An
       ended screen: LUMA_E_HANDLE, reported; an empty or null name, a null out: LUMA_E_ARGUMENT, reported;
       LUMA_UI_MOST_PANES found: LUMA_E_LIMIT, reported. Thread and taking out as open. */
    int32_t (*pane)(const LumaMod* mod, LumaUiScreen screen, const char* name, LumaUiPane* out);
    /* 1.0. Game thread. A text pane's text, UTF-8 (copied; "" empties it), drawn in the pane's font, size and
       alignment. Longer than LUMA_UI_TEXT_MOST units (or the pane's own buffer): cut there, LUMA_OK, Ui.TextCut in
       the mod log. Not a text pane: LUMA_E_ARGUMENT, reported (Ui.NotText); not UTF-8: LUMA_E_ARGUMENT, reported
       (Ui.NotUtf8); null: LUMA_E_ARGUMENT, reported; an ended pane: LUMA_E_HANDLE, reported. Thread and taking out
       as open. */
    int32_t (*set_text)(const LumaMod* mod, LumaUiPane pane, const char* text);
    /* 1.0. Game thread. The pane shown (`on` true) or hidden, with the panes under it. LUMA_OK; an ended pane:
       LUMA_E_HANDLE, reported. Thread and taking out as open. */
    int32_t (*set_visible)(const LumaMod* mod, LumaUiPane pane, bool on);
    /* 1.0. Game thread. The whole screen shown or hidden (its panes keep their own visibility). LUMA_OK; an ended
       screen: LUMA_E_HANDLE, reported. Thread and taking out as open. */
    int32_t (*show)(const LumaMod* mod, LumaUiScreen screen, bool on);
    /* 1.0. Game thread. The screen is no longer drawn; it and its panes' handles end. LUMA_OK; an ended screen:
       LUMA_E_HANDLE, reported. Thread and taking out as open. */
    int32_t (*close)(const LumaMod* mod, LumaUiScreen screen);
} LumaUiService;

#ifdef __cplusplus
}
#endif

#endif
