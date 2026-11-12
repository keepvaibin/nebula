/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's system services (contract services-system): facts and the display (`system`), the clipboard
   (`system.clipboard`, permission system.clipboard) and web pages (`system.links`, permission system.links). */
#ifndef LUMA_SYSTEM_H
#define LUMA_SYSTEM_H

#include <luma_services.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_SYSTEM_NAME "system"
#define LUMA_SYSTEM_MAJOR 1u
#define LUMA_SYSTEM_MINOR 0u
#define LUMA_SYSTEM_CLIPBOARD_NAME "system.clipboard"
#define LUMA_SYSTEM_CLIPBOARD_MAJOR 1u
#define LUMA_SYSTEM_CLIPBOARD_MINOR 0u
#define LUMA_SYSTEM_LINKS_NAME "system.links"
#define LUMA_SYSTEM_LINKS_MAJOR 1u
#define LUMA_SYSTEM_LINKS_MINOR 0u

typedef enum LumaSystemPlatform {
    LUMA_SYSTEM_WINDOWS = 0,
    LUMA_SYSTEM_LINUX = 1,   /* SteamOS included */
    LUMA_SYSTEM_ANDROID = 2,
    LUMA_SYSTEM_MACOS = 3,
    LUMA_SYSTEM_IOS = 4
} LumaSystemPlatform;
typedef enum LumaSystemArch { LUMA_SYSTEM_X64 = 0, LUMA_SYSTEM_ARM64 = 1 } LumaSystemArch;
typedef enum LumaSystemRenderer { LUMA_SYSTEM_D3D12 = 0, LUMA_SYSTEM_VULKAN = 1, LUMA_SYSTEM_METAL = 2 } LumaSystemRenderer;
/* The disc's region: RMGE01, RMGP01, RMGJ01. */
typedef enum LumaSystemRegion { LUMA_SYSTEM_US = 0, LUMA_SYSTEM_EU = 1, LUMA_SYSTEM_JP = 2 } LumaSystemRegion;

/* What does not change in a session. Strings are UTF-8, NUL-terminated, cut at a character's end to fit. */
typedef struct LumaSystemFacts {
    uint32_t size;            /* sizeof(LumaSystemFacts) as the mod was built */
    LumaSystemPlatform platform; /* each enum 4 bytes, a C enum */
    LumaSystemArch arch;
    LumaSystemRenderer renderer;
    LumaSystemRegion region;
    uint32_t lumacore_major;  /* 1 */
    uint32_t lumacore_minor;  /* 0 */
    uint32_t clipboard_images; /* 1: this platform's clipboard carries images; 0: get_image/set_image answer ABSENT */
    char game_id[8];          /* "RMGE01" */
    char language[16];        /* the language the game plays in, by the game's folder name: "UsEnglish", "JpJapanese" */
    char game_version[32];    /* Nebula's version, e.g. "2.0.0" */
    char locale[40];          /* the player's system locale, BCP 47: "en-US", "pt-BR", "zh-Hant-TW" */
    char os[64];              /* the system's name and version for people, e.g. "Windows 11 (26200)"; "" when unknown */
    char gpu[128];            /* the renderer's device, e.g. "NVIDIA GeForce RTX 4070"; "" when unknown */
} LumaSystemFacts;          /* 1.0: 320 bytes */

/* The game window's display now. Pixels are the window's drawable pixels (what the game renders into). */
typedef struct LumaSystemDisplay {
    uint32_t size;            /* sizeof(LumaSystemDisplay) as the mod was built */
    uint32_t changes;         /* grows by 1 each time any field below changes (1 at the first snapshot) */
    uint32_t width, height;   /* the window's drawable size */
    uint32_t display_width, display_height; /* the display the window is on, in pixels, its current mode */
    uint32_t scale;           /* content scale, thousandths (1000: 96 dpi on Windows, 1x on Apple and Android) */
    uint32_t refresh_mhz;     /* the display's refresh rate, thousandths of a hertz; 0 unknown */
    uint32_t safe_left, safe_top, safe_right, safe_bottom; /* insets of the safe area (notches, rounded corners,
                                 system bars) from each edge of the window, in its pixels; 0 where there are none */
    uint32_t fullscreen;      /* 1: the window fills its display */
    uint32_t focused;         /* 1: the game window is in front */
} LumaSystemDisplay;        /* 1.0: 56 bytes */

typedef struct LumaSystemService {
    LumaServiceTable base;
    /* 1.0. Any thread. Fills `out` (out->size bytes, whole fields). LUMA_OK. A null out, or a size below 1.0's (320):
       LUMA_E_ARGUMENT, reported (Service.Argument "out", Service.StructSize). */
    int32_t (*facts)(const LumaMod* mod, LumaSystemFacts* out);
    /* 1.0. Game thread. The display now. LUMA_OK. A null out, or a size below 1.0's (56): LUMA_E_ARGUMENT, reported
       (Service.Argument "out", Service.StructSize). */
    int32_t (*display)(const LumaMod* mod, LumaSystemDisplay* out);
} LumaSystemService;

/* Events of system.clipboard (LumaEvent.kind); `handle` is the request's (services-registry 3's form). */
typedef enum LumaSystemClipboardEvent {
    LUMA_SYSTEM_CLIPBOARD_EVENT_TEXT = 1,  /* data: none; status OK: value = the text's bytes (no NUL); take_text in the call */
    LUMA_SYSTEM_CLIPBOARD_EVENT_IMAGE = 2, /* data: none; status OK: value = width << 32 | height; take_image in the call */
    LUMA_SYSTEM_CLIPBOARD_EVENT_SET = 3    /* data: none; value: 0; status OK: the clipboard holds it */
} LumaSystemClipboardEvent;
/* Statuses an event may carry: LUMA_OK; LUMA_E_NOT_FOUND (no text / no image on the clipboard); LUMA_E_TOO_LARGE
   (over 16 MiB of text, over 8192 a side or 64 MiB of pixels); LUMA_E_BUSY (the game was not in front); LUMA_E_IO
   (the system refused, e.g. another program held the clipboard for 250 ms); LUMA_E_FORMAT (an image the game
   cannot decode); LUMA_E_CANCELLED (never given: a cancelled request has no event). */

typedef struct LumaSystemClipboardService {
    LumaServiceTable base;
    /* 1.0. Game thread. Asks for the clipboard's text. LUMA_PENDING with the request in *out; a TEXT event follows
       (to fn), closed after its event. A null fn or out: LUMA_E_ARGUMENT; a 5th open request: LUMA_E_LIMIT; both
       reported. */
    int32_t (*get_text)(const LumaMod* mod, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.0. Game thread, inside the TEXT event's call for that handle only, once. Copies min(value, size - 1) bytes
       and a NUL into buf (size >= 1) and returns value (the whole text's bytes). Outside the call, another handle or
       a second time: LUMA_E_HANDLE, reported; a null buf or size 0: LUMA_E_ARGUMENT, reported. The request is closed
       after the call whether or not this was called. */
    int64_t (*take_text)(const LumaMod* mod, LumaHandle request, char* buf, uint64_t size);
    /* 1.0. Game thread. Asks for the clipboard's image. LUMA_PENDING with the request in *out; an IMAGE event
       follows (to fn), closed after its event. Where clipboard_images is 0: LUMA_E_ABSENT (an answer, not reported). A
       null fn or out: LUMA_E_ARGUMENT; a 5th open request: LUMA_E_LIMIT; both reported. */
    int32_t (*get_image)(const LumaMod* mod, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.0. Game thread, inside the IMAGE event's call, once: copies the pixels into rgba (size >= width * height * 4,
       else LUMA_E_ARGUMENT "size", reported). LUMA_OK. Outside the call or a second time: LUMA_E_HANDLE, reported. */
    int32_t (*take_image)(const LumaMod* mod, LumaHandle request, uint8_t* rgba, uint64_t size);
    /* 1.0. Game thread. Puts `bytes` bytes of UTF-8 text on the clipboard (copied before this returns; no NUL
       needed). LUMA_PENDING; a SET event follows, closed after its event. Not UTF-8 or a NUL inside: LUMA_E_ARGUMENT "text"; over 16 MiB:
       LUMA_E_TOO_LARGE; null fn/out/text: LUMA_E_ARGUMENT; a 5th open request: LUMA_E_LIMIT; all reported. */
    int32_t (*set_text)(const LumaMod* mod, const char* text, uint64_t bytes, LumaEventFn fn, void* user,
                        LumaHandle* out);
    /* 1.0. Game thread. Puts width x height RGBA pixels on the clipboard (copied before this returns). LUMA_PENDING;
       a SET event follows, closed after its event. width: 1 to 8192, height: 1 to 8192, at most 64 MiB: else
       LUMA_E_ARGUMENT "width"/"height"; null fn/out/rgba: LUMA_E_ARGUMENT; a 5th open request: LUMA_E_LIMIT; all
       reported. Where clipboard_images is 0: LUMA_E_ABSENT, not reported. */
    int32_t (*set_image)(const LumaMod* mod, const uint8_t* rgba, uint32_t width, uint32_t height, LumaEventFn fn,
                         void* user, LumaHandle* out);
    /* 1.0. Game thread. Ends a request: no event for it is given after this returns (registry 4.8). LUMA_OK; a
       handle that is not this mod's open request: LUMA_E_HANDLE, reported. A set already handed to the system may
       still land on the clipboard. */
    int32_t (*cancel)(const LumaMod* mod, LumaHandle request);
} LumaSystemClipboardService;

typedef enum LumaSystemLinksEvent {
    LUMA_SYSTEM_LINKS_EVENT_LINK = 1 /* data: none; value: 0; status OK: the system took the link; LUMA_E_IO: it refused it */
} LumaSystemLinksEvent;

typedef struct LumaSystemLinksService {
    LumaServiceTable base;
    /* 1.0. Game thread. Opens `url` in the system browser; once the system took it, the Note System.LinkOpened
       names its site in the mod log. LUMA_PENDING; a LINK event follows, closed after its event. Not http(s), over 2048 bytes, a space or a
       control byte, a userinfo part, a host not ASCII, or null: LUMA_E_ARGUMENT "url", reported; null fn/out:
       LUMA_E_ARGUMENT, reported. Another of this mod's links in the last 10 seconds, one still open, or the game not
       in front: LUMA_E_BUSY (an answer, not reported). */
    int32_t (*open)(const LumaMod* mod, const char* url, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.0. Game thread. Ends a request: no event for it is given after this returns (registry 4.8). LUMA_OK; a
       handle that is not this mod's open request: LUMA_E_HANDLE, reported. A link already handed to the system may
       still open. */
    int32_t (*cancel)(const LumaMod* mod, LumaHandle request);
} LumaSystemLinksService;

#ifdef __cplusplus
}
#endif

#endif
