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
    /* The extension's controls are a controller's: player 1 plays with a controller (input's LUMA_INPUT_DEVICE_PAD)
       whose motor is the extension's (`motor`), whatever devices are attached. */
    NEBULA_EXT_CONTROLS_PAD = 1u << 3,
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
    /* A running mod taken out at the next frame's start, with the mods that use it (reload.md 2; the game writes the
       harness fact takeout.<id> once each one's library is closed). Null in a host without mods. */
    void (*take_out_mod)(void* host, const char* mod_id);
    /* UTF-8 typed into LumaCore's open text request (input.text_request) by the path the window's keyboard and input
       method type through; nothing while none is open. While one is open, menu_key's enter, esc, backspace, left and
       right edit it instead of reaching a menu. Null in a host without text entry. */
    void (*type_text)(void* host, const char* utf8);
    /* FORGET on the Mods page's REMOVED MODS line for `mod_id`, as the player chooses it: what the removed mod kept is
       dropped and every running mod watching (mods.watch_mods) gets FORGOTTEN. An id the line does not list: nothing,
       and one log line. Null in a host without mods. */
    void (*forget_mod)(void* host, const char* mod_id);
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
    /* Any thread: each line the host writes to its log, as its tag ("game:") and its text, whole (any length), without
       the time or a line end; the line is "<tag> <text>". The extension must not log from here. */
    void (*log_line)(void* user, const char* tag, const char* text);
    /* Game thread: a service the extension offers to mods (LumaCore.service), or null. The first extension that gives
       one is used. */
    const void* (*service)(void* user, const char* name, uint32_t major, uint32_t minor);
    /* Services the extension provides through LumaCore's registry (luma_service_provider.h; services-registry 14), each
       named "test." something, registered by the host before the first mod loads: a test's own services, with versions
       and permissions it controls. Any other name refuses the extension at attach. They stay valid for the run. */
    const struct LumaServiceProvider* const* luma_providers;
    uint32_t luma_provider_count;
    /* Game thread, with NEBULA_EXT_CONTROLS_PAD: player 1's motor strength as the host drives it (0 still, 255 full; the
       game's or a mod's, the stronger), at each change. */
    void (*motor)(void* user, uint8_t strength);
    /* Any thread, once, as the process ends (no frame or sound follows): what the extension writes is to be complete.
       `again` nonzero: the game starts again after it, with the same arguments and environment and
       NEBULA_STARTED_AGAIN=<starts so far> (an exec in the same process on Linux and macOS). */
    void (*ending)(void* user, uint32_t again);
    /* Game thread, at each frame of a 3D scene while a menu waits: nonzero while the extension prepares the game unseen
       (a test's setup, before the test's first frame). A menu the boot's close hid with its question standing opens
       again at the first 3D scene that no extension holds, so what the extension prepares runs with the scene going. */
    uint32_t (*holds_menus)(void* user);
} NebulaHostExtension;

typedef const NebulaHostExtension* (*NebulaExtensionAttach)(const NebulaHostForExtension* host);

#ifdef __cplusplus
}
#endif

#endif /* NEBULA_HOST_EXTENSION_H */
