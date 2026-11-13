/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's input services (contract services-input).

   `input` 1.2, no permission: LUMA_SERVICE(slot, "input", "1.2").
   - The mod's own actions (1.0), which the player binds in Controls. An action is a `key` option of the mod's
     luma.json (luma-format revision 5): its `key` is the action's id, its `name` what Controls shows, its `default`
     the game control it follows until the player binds it (one of input_bindings.cpp's kTargetIds: "c" follows the
     game's C wherever the player has put it). The bindings live only in the game's settings (`modbind.` lines), kept
     while the mod is off or removed.
   - What the game gets from a player's controller this frame, the kind of device the player uses and an event when
     it changes, text typed with the system keyboard, and rumble (1.1).
   - What aims the main player's pointer (1.2).

   `input.inject` 1.0, permission "input.inject" (the Mods page says the mod moves the player): the mod supplies a
   player's controller.

   `input.dev` 1.0, permission "input.devices": the computer's HID devices, listed and read raw. */
#ifndef LUMA_INPUT_H
#define LUMA_INPUT_H

#include <stdint.h>

#include "luma_services.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_INPUT_NAME "input"
#define LUMA_INPUT_MAJOR 1u
#define LUMA_INPUT_MINOR 2u

/* The most actions one mod has (its key options; luma-format allows 64 options of every kind). */
#define LUMA_INPUT_ACTIONS_MOST 64u
/* The players a call names: 0 the main player, 1 the co-star (the second player's pointer). */
#define LUMA_INPUT_PLAYERS 2u
/* The longest text a text request takes, in code points. */
#define LUMA_INPUT_TEXT_MOST 4096u

/* A controller port (1.2): 0 the main player's, 1 the co-star's (LUMA_INPUT_PLAYERS). */
typedef uint32_t LumaInputPad;

/* What aims a player's pointer (1.2, pointer_source). */
typedef enum LumaInputPointerSource {
    LUMA_INPUT_POINTER_SOURCE_PLAYER = 0,   /* the player's own controls: the source that moved last (the default) */
    LUMA_INPUT_POINTER_SOURCE_INFRARED = 1, /* a Wii Remote's camera, as the player aims it */
    LUMA_INPUT_POINTER_SOURCE_GYRO = 2,     /* a controller's gyro, turned `sensitivity` times as far as the setting */
    LUMA_INPUT_POINTER_SOURCE_MOUSE = 3,    /* the mouse, where it points on the picture */
    LUMA_INPUT_POINTER_SOURCE_STICK = 4     /* the pointer stick (the right stick, as bound in Controls) */
} LumaInputPointerSource;

/* One of the mod's actions, as `action` gives it. Never 0; the same for the session. */
typedef LumaHandle LumaInputAction;

/* An action this game frame. `pressed` and `released` hold for the one frame the action went down or up; a press and
   a release inside one frame give both, with `down` as the frame ends. */
typedef struct LumaInputState {
    uint32_t size;     /* sizeof(LumaInputState) as the mod was built */
    uint8_t down;      /* 1 while held */
    uint8_t pressed;   /* 1 the frame it went down */
    uint8_t released;  /* 1 the frame it went up */
    uint8_t bound;     /* 1: the player bound it in Controls; 0: it follows its game control */
    float value;       /* 0 to 1: a trigger or stick bound to it pressed this far; 1 or 0 for a key or button */
    uint64_t frames;   /* game frames it has been down (0 while up) */
} LumaInputState;      /* 1.0: 24 bytes */

/* The game's buttons, as LumaInputGame.buttons holds them (the Wii Remote's and the Nunchuk's). */
enum {
    LUMA_INPUT_BUTTON_A = 1u << 0,
    LUMA_INPUT_BUTTON_B = 1u << 1,
    LUMA_INPUT_BUTTON_1 = 1u << 2,
    LUMA_INPUT_BUTTON_2 = 1u << 3,
    LUMA_INPUT_BUTTON_PLUS = 1u << 4,
    LUMA_INPUT_BUTTON_MINUS = 1u << 5,
    LUMA_INPUT_BUTTON_HOME = 1u << 6,
    LUMA_INPUT_BUTTON_UP = 1u << 7,
    LUMA_INPUT_BUTTON_DOWN = 1u << 8,
    LUMA_INPUT_BUTTON_LEFT = 1u << 9,
    LUMA_INPUT_BUTTON_RIGHT = 1u << 10,
    LUMA_INPUT_BUTTON_C = 1u << 11,
    LUMA_INPUT_BUTTON_Z = 1u << 12,
    LUMA_INPUT_BUTTONS_ALL = (1u << 13) - 1u
};

/* The kind of device a player plays with. */
enum {
    LUMA_INPUT_DEVICE_NONE = 0,         /* nobody plays this player, or the host samples no controls */
    LUMA_INPUT_DEVICE_KEYBOARD_MOUSE = 1,
    LUMA_INPUT_DEVICE_PAD = 2,          /* a controller: Xbox, PlayStation, Switch, Steam, a generic pad */
    LUMA_INPUT_DEVICE_TOUCH = 3,        /* a phone's on-screen controls */
    LUMA_INPUT_DEVICE_MOTION = 4        /* a Wii Remote */
};

/* A player's controller in one game frame, as the game reads it. */
typedef struct LumaInputGame {
    uint32_t size;          /* sizeof(LumaInputGame) as the mod was built */
    uint32_t buttons;       /* LUMA_INPUT_BUTTON_* held */
    float stick_x;          /* the move stick, -1 to 1: right */
    float stick_y;          /* -1 to 1: up */
    float pointer_x;        /* where the player points, -1 to 1 across the picture: right (pointer_on) */
    float pointer_y;        /* -1 to 1: down */
    uint8_t pointer_on;     /* 1: the player points at the picture */
    uint8_t shake;          /* 1: a spin this read (a shake of either hand) */
    uint8_t device;         /* LUMA_INPUT_DEVICE_*; read only (inject ignores it) */
    uint8_t injected;       /* 1: a mod supplied it (input.inject); read only */
} LumaInputGame;            /* 1.1: 28 bytes */

/* What a text request takes. */
enum {
    LUMA_INPUT_TEXT_ANY = 0,
    LUMA_INPUT_TEXT_NUMBER = 1, /* digits, one leading minus and one point */
    LUMA_INPUT_TEXT_NAME = 2    /* no line breaks, no tabs, no control characters (a file or a player's name) */
};
typedef struct LumaInputTextRequest {
    uint32_t size;          /* sizeof(LumaInputTextRequest) as the mod was built */
    uint32_t kind;          /* LUMA_INPUT_TEXT_* */
    uint32_t max_length;    /* code points, 1 to LUMA_INPUT_TEXT_MOST */
    uint32_t reserved;
    const char* initial;    /* UTF-8 the field starts with (cut to max_length); null: empty */
} LumaInputTextRequest;     /* 1.1: 24 bytes */

/* The input events (LumaEvent.kind). */
enum {
    /* A player's device kind changed (watch_device): `value` the new LUMA_INPUT_DEVICE_*, `flags` the player. */
    LUMA_INPUT_EVENT_DEVICE = 1,
    /* A text request ended (text_request), `handle` the request: status LUMA_OK with `data` the UTF-8 text (no 0 at
       its end; `data_size` its bytes, `value` its code points), or LUMA_E_CANCELLED (the player cancelled it, the mod
       did, another request took the keyboard, or the window lost the keyboard) with no data. */
    LUMA_INPUT_EVENT_TEXT = 2,
    /* A raw report from an open device (input.dev open): `handle` the device, `data` the report as the device sent it
       (its report id first), `value` the reports dropped before it (0 normally), `flags` 1 when it was cut to
       LUMA_INPUT_REPORT_MOST; or status LUMA_E_CLOSED once when the device went away (no event after). */
    LUMA_INPUT_EVENT_REPORT = 3
};

typedef struct LumaInputService {
    LumaServiceTable base;
    /* 1.0. Game thread. The mod's action `id` (its key option's key) into *out. LUMA_OK; LUMA_E_NOT_FOUND when the
       mod's luma.json has no key option of that key (an answer, not reported); a null id or out: LUMA_E_ARGUMENT,
       reported (Service.Argument). */
    int32_t (*action)(const LumaMod* mod, const char* id, LumaInputAction* out);
    /* 1.0. Game thread. The action's state this game frame into *out. LUMA_OK; an action that is not one of this mod's:
       LUMA_E_HANDLE, reported (Service.Handle); a null out: LUMA_E_ARGUMENT, a size below 1.0's: LUMA_E_ARGUMENT
       (Service.StructSize), both reported. While the player is in a menu of the host (Settings, the HOME menu) every
       action reads up. */
    int32_t (*read)(const LumaMod* mod, LumaInputAction action, LumaInputState* out);
    /* 1.1. Game thread. What the game got from `player`'s controller at its last read, after the player's bindings and
       any injection, into *out (`device` NONE: no device gives it, as in the headless host). LUMA_OK; LUMA_E_ABSENT
       when the game has not read that player (the co-star while nobody plays it): *out zeroed but its size; a player
       past LUMA_INPUT_PLAYERS, a null out or a size below 1.1's: LUMA_E_ARGUMENT, reported. */
    int32_t (*game)(const LumaMod* mod, uint32_t player, LumaInputGame* out);
    /* 1.1. Game thread. As `game`, but the player's own controller while a mod injects (what the player does, which
       the game does not get); the same as `game` otherwise. */
    int32_t (*game_raw)(const LumaMod* mod, uint32_t player, LumaInputGame* out);
    /* 1.1. Game thread. The kind of device `player` plays with into *out (LUMA_INPUT_DEVICE_*; NONE when nobody does or
       the host samples no controls). LUMA_OK; a bad player or a null out: LUMA_E_ARGUMENT, reported. */
    int32_t (*device)(const LumaMod* mod, uint32_t player, uint32_t* out);
    /* 1.1. Game thread. fn(user, event) at a frame's start each time a player's device kind changes
       (LUMA_INPUT_EVENT_DEVICE, the latest kind when it changed twice between frames); a null fn stops it. One watch a
       mod: a second replaces the first. LUMA_OK. */
    /* events: DEVICE(value, flags) */
    int32_t (*watch_device)(const LumaMod* mod, LumaEventFn fn, void* user);
    /* 1.1. Game thread. Opens the system keyboard (and its input method: composition, candidates) for the player to
       type; the game's controls are held while it is open (the game reads a controller at rest, every action reads
       up). The text arrives as LUMA_INPUT_EVENT_TEXT through fn when the player confirms (Enter) or cancels (Escape).
       *out: the request. LUMA_PENDING; a TEXT event follows (to fn), closed after its event. One request open at
       once in the game: a new one ends the open one CANCELLED. LUMA_E_ABSENT where the host has no text entry (see
       the contract); a null request, fn or out, a size below 1.1's, a bad kind, max_length 0 or past
       LUMA_INPUT_TEXT_MOST, or an initial that is not UTF-8: LUMA_E_ARGUMENT, reported. */
    int32_t (*text_request)(const LumaMod* mod, const LumaInputTextRequest* request, LumaEventFn fn, void* user,
                            LumaHandle* out);
    /* 1.1. Game thread. The open request's text so far (for the mod to draw the field): up to size - 1 bytes of UTF-8
       and a 0 into buffer (cut on a code point), its whole length in bytes into *length and the caret in code points
       into *caret (each may be null). LUMA_OK; a request that is not this mod's open one: LUMA_E_HANDLE, reported;
       a null buffer with size above 0: LUMA_E_ARGUMENT, reported. */
    int32_t (*text_peek)(const LumaMod* mod, LumaHandle request, char* buffer, uint32_t size, uint32_t* length,
                         uint32_t* caret);
    /* 1.1. Game thread. Ends the mod's open request: its event comes CANCELLED. LUMA_OK; one that is not its open
       one: LUMA_E_HANDLE, reported. */
    int32_t (*text_cancel)(const LumaMod* mod, LumaHandle request);
    /* 1.1. Game thread. Shakes `player`'s controller at `strength` (0 to 1) for `seconds` (at most 10) of game time,
       beside the game's own rumble (the stronger wins); a later call replaces the mod's earlier one, strength 0 stops
       it. The player's Vibration setting scales it as it does the game's. LUMA_OK; LUMA_E_ABSENT when the player's
       device has no motor (a keyboard, a phone's screen) or the host drives none; a bad player, a strength or seconds
       not a number, below 0 or past their most: LUMA_E_ARGUMENT, reported. */
    int32_t (*rumble)(const LumaMod* mod, uint32_t player, float strength, float seconds);
    /* 1.2. Game thread. What aims `pad`'s pointer from the next read: only `kind` moves it (the others are not
       read for it), until a mod sets another, LUMA_INPUT_POINTER_SOURCE_PLAYER gives it back to the player's
       controls, or the mod that set it stops. `sensitivity` (above 0, at most 8) for GYRO, else ignored. The
       player's settings (invert, speeds, recenter) still apply. LUMA_OK; LUMA_E_ABSENT when the player has no
       such device now (no Wii Remote for INFRARED, no controller with a gyro for GYRO, no mouse for MOUSE, no
       controller for STICK), or for pad 1 (the co-star's pointer follows its own controller); a pad past 1, an
       unknown kind or a GYRO sensitivity out of range: LUMA_E_ARGUMENT, reported. */
    int32_t (*pointer_source)(const LumaMod* mod, LumaInputPad pad, LumaInputPointerSource kind, float sensitivity);
} LumaInputService;

/* ---- input.inject 1.0 (permission "input.inject") ---------------------------------------------------------------- */

#define LUMA_INPUT_INJECT_NAME "input.inject"
#define LUMA_INPUT_INJECT_MAJOR 1u
#define LUMA_INPUT_INJECT_MINOR 0u

/* One mod's supply of one player's controller (begin to end). */
typedef LumaHandle LumaInputInject;

typedef struct LumaInputInjectService {
    LumaServiceTable base;
    /* Game thread. From the game's next read on, the mod supplies `player`'s controller: *out the injection.
       LUMA_OK; LUMA_E_BUSY when a mod injects that player already (an answer); a bad player or a null out:
       LUMA_E_ARGUMENT, reported. */
    int32_t (*begin)(const LumaMod* mod, uint32_t player, LumaInputInject* out);
    /* Game thread. What the game reads at its next read of that player: buttons (bits past LUMA_INPUT_BUTTONS_ALL are
       dropped), stick, pointer and shake; `device` and `injected` are not read. A read the mod set nothing for gets a
       controller at rest, not the last one (a mod that stops calling holds no button). A stick or pointer value that is
       not a number reads 0 and one past 1 reads 1, said once a call (Service.Argument), the call still LUMA_OK.
       LUMA_OK; an injection that is not this mod's open one: LUMA_E_HANDLE; a null state or a size below 1.1's
       LumaInputGame: LUMA_E_ARGUMENT; both reported. */
    int32_t (*set)(const LumaMod* mod, LumaInputInject injection, const LumaInputGame* state);
    /* Game thread. The player's own controller again from the next read. LUMA_OK; LUMA_E_HANDLE as set. A mod taken
       out ends its injections. */
    int32_t (*end)(const LumaMod* mod, LumaInputInject injection);
} LumaInputInjectService;

/* ---- input.dev 1.0 (permission "input.devices") ------------------------------------------------------------------- */

#define LUMA_INPUT_DEV_NAME "input.dev"
#define LUMA_INPUT_DEV_MAJOR 1u
#define LUMA_INPUT_DEV_MINOR 0u
/* The most devices `list` gives and the most a mod has open. */
#define LUMA_INPUT_DEVICES_MOST 64u
#define LUMA_INPUT_DEV_OPEN_MOST 8u
/* The longest report kept; a longer one is cut (flags 1 on its event). */
#define LUMA_INPUT_REPORT_MOST 1024u

typedef struct LumaInputDeviceInfo {
    uint32_t size;          /* sizeof(LumaInputDeviceInfo) as the mod was built */
    uint16_t vendor;        /* USB vendor id */
    uint16_t product;       /* USB product id */
    uint16_t usage_page;    /* its top collection's HID usage page and usage (1/5: a game pad) */
    uint16_t usage;
    uint16_t version;       /* release number */
    uint16_t input_bytes;   /* its longest input report, with the report id byte */
    char name[128];         /* the product's name, UTF-8 (may be empty) */
    char path[256];         /* what `open` takes: valid while it stays plugged in */
} LumaInputDeviceInfo;      /* 1.0: 400 bytes */

typedef struct LumaInputDevService {
    LumaServiceTable base;
    /* Game thread. The HID devices plugged in now, up to `capacity` into out[] (each with its size set by the mod), and
       how many there are into *found (may exceed capacity; at most LUMA_INPUT_DEVICES_MOST). The system's keyboards
       and mice are not listed (it keeps them). LUMA_OK; LUMA_E_ABSENT on a platform or host that gives no raw devices
       (see the contract); a null found, a null out with capacity above 0, or a size below 1.0's in an entry:
       LUMA_E_ARGUMENT, reported. */
    int32_t (*list)(const LumaMod* mod, LumaInputDeviceInfo* out, uint32_t capacity, uint32_t* found);
    /* Game thread. Opens the device at `path` (from list) to read: each input report it sends arrives through fn as
       LUMA_INPUT_EVENT_REPORT at a frame's start, in order (64 kept between frames; past that the oldest is dropped and
       counted on the next event). *out the device. LUMA_OK; LUMA_E_NOT_FOUND (unplugged, or not a HID path),
       LUMA_E_BUSY (another program holds it); LUMA_E_LIMIT (LUMA_INPUT_DEV_OPEN_MOST open), reported; LUMA_E_ABSENT
       as list; a null or empty path, fn or out: LUMA_E_ARGUMENT, reported. */
    /* events: REPORT(socket, status, data, value, flags) */
    int32_t (*open)(const LumaMod* mod, const char* path, LumaEventFn fn, void* user, LumaHandle* out);
    /* Game thread. Sends an output report (its report id first, 0 when the device numbers none). LUMA_OK; LUMA_E_IO
       when the device refused it; LUMA_E_HANDLE (not this mod's open device), a null data, size 0 or past
       LUMA_INPUT_REPORT_MOST: LUMA_E_ARGUMENT; both reported. */
    int32_t (*write)(const LumaMod* mod, LumaHandle device, const void* data, uint32_t size);
    /* Game thread. Closes it; no event comes after. LUMA_OK; LUMA_E_HANDLE as write. A mod taken out closes its own. */
    int32_t (*close)(const LumaMod* mod, LumaHandle device);
} LumaInputDevService;

#ifdef __cplusplus
}
#endif

#endif /* LUMA_INPUT_H */
