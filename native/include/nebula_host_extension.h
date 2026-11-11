/* SPDX-License-Identifier: GPL-3.0-only
   A host extension: a library the host loads at start with --extension <file> (repeatable) and asks, at fixed points, for
   controls, the frame clock, which pictures to keep and what to do with the sound. A test driver, a replay or TAS tool,
   a recorder. C ABI. Fields are only ever added at the end; `size` says how many the other side has, and a field past it
   (or a null function) is absent.

   The library exports
       const NebulaHostExtension* nebula_extension_attach(const NebulaHostForExtension* host);
   called once, before the game's clock starts; null declines. The host and the extension keep their structures for the
   whole run. */
#ifndef NEBULA_HOST_EXTENSION_H
#define NEBULA_HOST_EXTENSION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEBULA_HOST_EXTENSION_VERSION 1u

/* NebulaExtensionControls.buttons: the Wii Remote and Nunchuk buttons. */
enum {
    NEBULA_EXT_BUTTON_A = 1u << 0,
    NEBULA_EXT_BUTTON_B = 1u << 1,
    NEBULA_EXT_BUTTON_ONE = 1u << 2,
    NEBULA_EXT_BUTTON_TWO = 1u << 3,
    NEBULA_EXT_BUTTON_PLUS = 1u << 4,
    NEBULA_EXT_BUTTON_MINUS = 1u << 5,
    NEBULA_EXT_BUTTON_HOME = 1u << 6,
    NEBULA_EXT_BUTTON_UP = 1u << 7,
    NEBULA_EXT_BUTTON_DOWN = 1u << 8,
    NEBULA_EXT_BUTTON_LEFT = 1u << 9,
    NEBULA_EXT_BUTTON_RIGHT = 1u << 10,
    NEBULA_EXT_BUTTON_C = 1u << 11,
    NEBULA_EXT_BUTTON_Z = 1u << 12,
};

/* A sample of the controls from an extension. They add to the devices': a button either holds is held; a stick or
   pointer the extension gives replaces the devices'. */
typedef struct NebulaExtensionControls {
    uint32_t buttons;        /* NEBULA_EXT_BUTTON_* */
    uint8_t has_stick;       /* stick_x/y are given */
    uint8_t has_pointer;     /* pointer_x/y are given */
    uint8_t shake;           /* a Wii Remote shake */
    uint8_t reserved;
    float stick_x, stick_y;  /* the Nunchuk stick, -1 to 1 */
    float pointer_x, pointer_y; /* the pointer on the game's picture, -1 to 1 (y down) */
} NebulaExtensionControls;

/* Where the game is, as the host counts it. */
typedef struct NebulaExtensionFrame {
    uint64_t game_frames; /* frames the game has run since its start */
} NebulaExtensionFrame;

/* NebulaHostExtension.picture_path's kind. */
enum {
    NEBULA_EXT_PICTURE_GAME = 0,      /* a picture the game made, shown for the first time */
    NEBULA_EXT_PICTURE_REPEAT = 1,    /* a picture shown again */
    NEBULA_EXT_PICTURE_GENERATED = 2, /* a frame interpolated between two of the game's */
};

/* NebulaHostForExtension.menu_key: keys to the host's menus (Nebula Settings, the HOME menu), as a player presses them. */
enum {
    NEBULA_EXT_KEY_UP = 1,
    NEBULA_EXT_KEY_DOWN,
    NEBULA_EXT_KEY_LEFT,
    NEBULA_EXT_KEY_RIGHT,
    NEBULA_EXT_KEY_ENTER,
    NEBULA_EXT_KEY_ESCAPE,
    NEBULA_EXT_KEY_TAB,
    NEBULA_EXT_KEY_BACKSPACE,
    NEBULA_EXT_KEY_SPACE,
    NEBULA_EXT_KEY_MENU, /* opens and closes Nebula Settings (F1) */
};

/* NebulaHostForExtension.menu_open's answer. */
enum {
    NEBULA_EXT_MENU_NONE = 0,
    NEBULA_EXT_MENU_SETTINGS = 1,
    NEBULA_EXT_MENU_HOME = 2,
};

/* NebulaHostExtension.flags */
enum {
    /* The game's time is its frame count: the host's frame and sound clocks do not run, the game takes each frame as soon
       as it has nothing to run, disc reads finish inside their call. Two runs of one build match frame for frame. */
    NEBULA_EXT_FIXED_STEPS = 1u << 0,
    /* No sound device is opened: the mix goes only to `sound`. */
    NEBULA_EXT_NO_SOUND_DEVICE = 1u << 1,
    /* A hidden start still shows the mods' preparing screen (and starts again after it) as a player's start does. */
    NEBULA_EXT_MODS_SCREEN = 1u << 2,
};

/* What the host offers. Any thread unless said. */
typedef struct NebulaHostForExtension {
    uint32_t version; /* NEBULA_HOST_EXTENSION_VERSION */
    uint32_t size;    /* sizeof(NebulaHostForExtension) */
    void* host;
    void (*log)(void* host, const char* line);  /* a line of the run's log */
    void (*quit)(void* host);                   /* ends the game as closing its window does */
    void (*menu_key)(void* host, uint32_t key); /* a NEBULA_EXT_KEY_* to the open menu, or the menu key */
    uint32_t (*menu_open)(void* host);          /* NEBULA_EXT_MENU_* */
    void (*open_settings)(void* host, uint32_t page); /* opens Nebula Settings on that page, its first row chosen */
    void (*resize_window)(void* host, uint32_t width, uint32_t height); /* the window's inside to that size, in pixels */
    /* The graphics device lost at the renderer's next frame, as a driver reset loses it; the renderer recovers as from
       any loss. */
    void (*lose_gpu_device)(void* host);
} NebulaHostForExtension;

/* What the extension offers. Each function may be null. */
typedef struct NebulaHostExtension {
    uint32_t version; /* NEBULA_HOST_EXTENSION_VERSION */
    uint32_t size;    /* sizeof(NebulaHostExtension) */
    void* user;
    uint32_t flags; /* NEBULA_EXT_* */
    /* Game thread, at each sample of the controls: what the extension holds. */
    void (*controls)(void* user, const NebulaExtensionFrame* frame, NebulaExtensionControls* controls);
    /* Game thread, once a game frame, before its controls. */
    void (*frame)(void* user, const NebulaExtensionFrame* frame);
    /* Render thread, at each present of the game's picture: a file to write the present to (a PPM), or null.
       `game_frame` is the game frame of the newest frame the renderer has drawn (NebulaExtensionFrame.game_frames then).
       The text stays valid until the next call. */
    const char* (*picture_path)(void* user, uint64_t game_frame, uint32_t kind);
    /* Sound thread: the mix as it goes to the device (or instead of it, NEBULA_EXT_NO_SOUND_DEVICE), interleaved. */
    void (*sound)(void* user, const float* samples, uint32_t frames, uint32_t channels, uint32_t rate);
    /* Game thread: nonzero when the game should take its mods as changed (cooked again), as Nebula Settings' Mods page
       makes them. */
    uint32_t (*mods_changed)(void* user);
    /* Nonzero: the graphics memory the renderer takes as its budget, in MB, whatever the card reports (the smallest of
       the extensions'). */
    uint32_t vram_budget_mb;
    /* Nonzero: each read of the game's files waits this long first, a slow drive (the longest of the extensions'). */
    uint32_t disc_read_delay_us;
} NebulaHostExtension;

typedef const NebulaHostExtension* (*NebulaExtensionAttach)(const NebulaHostForExtension* host);

#ifdef __cplusplus
}
#endif

#endif /* NEBULA_HOST_EXTENSION_H */
