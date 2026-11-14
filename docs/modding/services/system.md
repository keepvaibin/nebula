# Service: system

## More rules

### Services-system

The `system` services: facts about the machine and the game (platform, renderer, versions, the game's language and
region, the player's locale), the game window's display and safe areas, the clipboard (text and images) and opening
a web page in the system browser. Inherits services-registry whole: calling rules (section 4), events (6),
a mod's life (8), no cost with no mod (10), messages (11), permissions (5).

Reviewers: F (the host files, the platform layers, the harness's deterministic mode), G (LumaScript's `system`, the
self-description rows, the validator's L506 for the two permissions), J (the mod log's Note for a link, what the
Mods page says of the two permissions).

### 1. What is decided here, in one list

1. Three tables, each its own service (registry 4: a permissioned service's name is its permission's name):
   `system` 1.0 (no permission: facts and the display), `system.clipboard` 1.0 (permission `system.clipboard`),
   `system.links` 1.0 (permission `system.links`). All three are provided by the host (`runtime/src/luma_system.cpp`,
   platform-free) over one platform interface (`runtime/include/galaxy/luma_system_platform.h`) with one
   implementation per platform layer: `runtime/src/luma_system_win32.cpp` (Windows), `platform/sdl/luma_system_sdl.cpp`
   (Linux, SteamOS, Android; Android's images through JNI), `platform/apple/luma_system_apple.mm` (macOS, iOS).
2. Facts never change in a session except the display; the display is a snapshot the platform keeps, read on the
   game thread with no system call (a sequence-locked copy), with a `changes` count that grows on each change.
3. The clipboard and links never block the game: each call copies what it is given and answers `LUMA_PENDING`; the
   work runs on the thread the platform needs (Windows: the provider's worker thread with its own message-only
   window; SDL: SDL's main thread through `SDL_RunOnMainThread`, decoding and encoding on the worker; Apple: the main
   queue), and the answer is an event at a frame's start. A read's data is lent to the event's call only.
4. Images are RGBA, 8 bits a channel, straight alpha, rows top to bottom, tightly packed. On the system clipboard an
   image is written as PNG (Windows also `CF_DIBV5`), and read from PNG, else (Windows) `CF_DIBV5`/`CF_DIB`, else
   (SDL, Apple) BMP, JPEG or TIFF as the platform offers them. At most 8192 pixels a side and 64 MiB of pixels.
5. Text is UTF-8 (no NUL inside), at most 16 MiB either way. Line ends as the clipboard holds them.
6. The clipboard is read only while the game window is in front (the player is looking at the game): otherwise the
   event says `LUMA_E_BUSY` (Android refuses background reads anyway; the rule is the same everywhere).
7. A link is `https://` or `http://`, at most 2048 bytes, no space or control byte, no userinfo (an `@` before the
   host), and an ASCII host (an internationalized name in its `xn--` form). Opened only while the game is in
   front; at most one a mod in 10 seconds (`LUMA_E_BUSY`, an answer). Every link the system opened is written to
   the mod log as a Note naming its site (`System.LinkOpened`, source CODE, level NOTE, through the host's
   `galaxy::modlog::add_row`; shown as a notice, J-SS5), when the platform answers OK: the player always sees
   which mod opened a page where. The whole URL goes to the game's log (`nebula.log`) only.
8. Deterministic mode (the harness's `test_frame_step`) never touches the machine's clipboard or browser: the
   clipboard is a private one in the process (empty at start), a link is "opened" by writing its Note only; the
   window counts as in front. Facts are the real ones except `os` and `gpu` (empty) and `locale` (`en-US`), so runs
   compare.
9. No cost with no mod: nothing runs until a mod calls; the worker thread starts at the first clipboard or link call
   and the snapshot is kept by the platform's window events it already handles.
10. Up to 4 clipboard requests and 1 link request open a mod at once (`LUMA_E_LIMIT`, reported). A request is
    closed after its event's call whether or not `take_*` was called; `take_text`/`take_image` may be called once per
    event.
11. Reading the clipboard is said once a mod a session: the Note `System.ClipboardRead` (row 141, log only) at the
    mod's first read that gives text or an image.

### 2. The header: `native/include/luma_system.h`

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's system services (docs/modding/services/system.md): facts and the display (`system`), the clipboard
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
    uint32_t platform;        /* LumaSystemPlatform */
    uint32_t arch;            /* LumaSystemArch */
    uint32_t renderer;        /* LumaSystemRenderer */
    uint32_t region;          /* LumaSystemRegion */
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
    /* 1.0. Game thread. The display now, as `facts`; 1.0's size 56. */
    int32_t (*display)(const LumaMod* mod, LumaSystemDisplay* out);
} LumaSystemService;

/* Events of system.clipboard (LumaEvent.kind); `handle` is the request's. */
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
    /* 1.0. Game thread. Asks for the clipboard's text. LUMA_PENDING with the request in *out; the answer is an event
       to fn. A null fn or out: LUMA_E_ARGUMENT; a 5th open request: LUMA_E_LIMIT; both reported. */
    int32_t (*get_text)(const LumaMod* mod, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.0. Game thread, inside the TEXT event's call for that handle only, once. Copies min(value, size - 1) bytes
       and a NUL into buf (size >= 1) and returns value (the whole text's bytes). Outside the call, another handle or
       a second time: LUMA_E_HANDLE, reported; a null buf or size 0: LUMA_E_ARGUMENT, reported. The request is closed
       after the call whether or not this was called. */
    int64_t (*take_text)(const LumaMod* mod, LumaHandle request, char* buf, uint64_t size);
    /* 1.0. As get_text, for an image. Where clipboard_images is 0: LUMA_E_ABSENT (an answer, not reported). */
    int32_t (*get_image)(const LumaMod* mod, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.0. Game thread, inside the IMAGE event's call, once: copies the pixels into rgba (size >= width * height * 4,
       else LUMA_E_ARGUMENT "size", reported). LUMA_OK. Outside the call or a second time: LUMA_E_HANDLE, reported. */
    int32_t (*take_image)(const LumaMod* mod, LumaHandle request, uint8_t* rgba, uint64_t size);
    /* 1.0. Game thread. Puts `bytes` bytes of UTF-8 text on the clipboard (copied before this returns; no NUL
       needed). LUMA_PENDING; a SET event follows. Not UTF-8 or a NUL inside: LUMA_E_ARGUMENT "text"; over 16 MiB:
       LUMA_E_TOO_LARGE; null fn/out/text: LUMA_E_ARGUMENT; a 5th open request: LUMA_E_LIMIT; all reported. */
    int32_t (*set_text)(const LumaMod* mod, const char* text, uint64_t bytes, LumaEventFn fn, void* user,
                        LumaHandle* out);
    /* 1.0. As set_text, for width x height RGBA pixels (copied). 0 or over 8192 a side, or over 64 MiB:
       LUMA_E_ARGUMENT "width"/"height", reported. Where clipboard_images is 0: LUMA_E_ABSENT, not reported. */
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
       names its site in the mod log. LUMA_PENDING; a LINK event follows. Not http(s), over 2048 bytes, a space or a
       control byte, a userinfo part, a host not ASCII, or null: LUMA_E_ARGUMENT "url", reported; null fn/out:
       LUMA_E_ARGUMENT, reported. Another of this mod's links in the last 10 seconds, one still open, or the game not
       in front: LUMA_E_BUSY (an answer, not reported). */
    int32_t (*open)(const LumaMod* mod, const char* url, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.0. As the clipboard's cancel. */
    int32_t (*cancel)(const LumaMod* mod, LumaHandle request);
} LumaSystemLinksService;

#ifdef __cplusplus
}
#endif

#endif
```

### 3. Facts

- `platform`, `arch`: of the build (the platform layer's file and the compiler's target).
- `renderer`: the renderer the game draws with this session: the Windows host's build (D3D12, or Vulkan in the
  Vulkan build: `NEBULA_GPU_VULKAN`, inside `luma_system_win32.cpp` only), Vulkan on SDL, Metal on Apple.
- `game_id`, `region`: the disc's id (`galaxy::mods_game_id()`), its fourth letter E, P, J.
- `language`: the game's running language (`running_game_language`, its folder by `galaxy::language_folder(game_id, language)` from the
  region), fixed at boot; before the game asked (no mod runs then) it is not reached.
- `game_version`: `NEBULA_VERSION`. `lumacore_major/minor`: the loader's (`luma.h`), 1 and 0.
- `locale`: Windows `GetUserDefaultLocaleName`; SDL `SDL_GetPreferredLocales()[0]` (`language_COUNTRY` written as
  `language-COUNTRY`); Apple `NSLocale.preferredLanguages[0]`. Unknown: `und`.
- `os`: Windows `RtlGetVersion` ("Windows 10" below build 22000, else "Windows 11", with the build); Linux `uname`'s
  sysname and release, with `/etc/os-release`'s `PRETTY_NAME` when present; Android "Android " and
  `SDL_GetAndroidSDKVersion`'s API level; Apple `operatingSystemVersionString`.
- `gpu`: the renderer's device name as its context holds it (`vk::Context::device_name`, D3D12's adapter
  description, `mtl::Context::device_name`), given to the platform layer by the renderer at start
  (`luma_system::platform::set_gpu_name`); "" until then.
- `clipboard_images`: 1 on Windows, macOS, iOS, Android (JNI), Linux (X11 and Wayland through SDL's MIME data); a
  Linux session whose clipboard offers no MIME data (no video driver): 0.

### 4. The display

The platform keeps a snapshot written on the thread that sees the window's changes and read by `display` with a
sequence lock (no system call on the game thread):

- Windows (`host_window_win32.cpp`'s window procedure on `WM_SIZE`, `WM_DPICHANGED`, `WM_DISPLAYCHANGE`,
  `WM_MOVE`, `WM_WINDOWPOSCHANGED` (a move between monitors by the system's snap), `WM_ACTIVATE`): the client
  size; `MonitorFromWindow` + `GetMonitorInfoW` + `EnumDisplaySettingsW`
  (`ENUM_CURRENT_SETTINGS`) for the display's size and refresh; `GetDpiForWindow` * 1000 / 96; safe area 0;
  fullscreen when the client covers the monitor's rectangle; focused while active.
- SDL (`host_window_sdl.cpp`'s one event switch, the main thread): new case labels
  `SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED`, `SDL_EVENT_DISPLAY_ORIENTATION`, `SDL_EVENT_WINDOW_DISPLAY_CHANGED`,
  `SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED`, `SDL_EVENT_WINDOW_SAFE_AREA_CHANGED`, `SDL_EVENT_WINDOW_ENTER_FULLSCREEN`,
  `SDL_EVENT_WINDOW_LEAVE_FULLSCREEN`, and the existing `WINDOW_PIXEL_SIZE_CHANGED`, `WINDOW_FOCUS_GAINED`/`_LOST`,
  `WINDOW_MINIMIZED`/`_RESTORED` cases, all into one snapshot-and-call (Android rotates through the same loop):
  `SDL_GetWindowSizeInPixels`; `SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow)`; `SDL_GetWindowDisplayScale`;
  `SDL_GetWindowSafeArea` (points) times `SDL_GetWindowPixelDensity`, as insets; `SDL_WINDOW_FULLSCREEN`;
  `SDL_WINDOW_INPUT_FOCUS`.
- Apple: the view's drawable size, `NSScreen`/`UIScreen`'s mode and `maximumFramesPerSecond`, `backingScaleFactor`
  /`nativeScale`, `safeAreaInsets` times the scale.
- The platform calls `luma_system::platform::display_changed(const LumaSystemDisplay&)` with each new snapshot (the
  same values: no call); `changes` is counted there.

### 5. The clipboard

- Requests live in a fixed table (4 a mod) of nodes from the kernel's memory for the mod, the node first, as time's
  timers (services-time 6): the worker answers by `post`; `deliver` calls `fn` with the request's data lent, then
  frees the data; `cancel` closes the request (no call; freed when its node is idle); `mod_gone` closes all.
- Windows: one worker thread (`luma_system_win32.cpp`) with a message-only window (`HWND_MESSAGE`) that owns the
  clipboard when the game writes. `OpenClipboard` is tried for 250 ms (10 ms apart), then `LUMA_E_IO`. Text:
  `CF_UNICODETEXT` to and from UTF-8. Image write: `EmptyClipboard`, PNG under the registered format "PNG" and a
  `CF_DIBV5` (32-bit BGRA, top-down, alpha mask set). Image read: "PNG", else `CF_DIBV5` (alpha when its mask has
  one, else opaque), else `CF_DIB` (24 or 32 bits, opaque), bottom-up rows turned.
- SDL (Linux, SteamOS): `SDL_RunOnMainThread(…, false)`: text by `SDL_GetClipboardText`/`SDL_SetClipboardText`;
  images by `SDL_GetClipboardData` for the first of "image/png", "image/bmp", "image/jpeg" that
  `SDL_HasClipboardData` gives, the bytes handed to the worker to decode; written with `SDL_SetClipboardData`
  offering "image/png" (encoded on the worker first; the callback gives the bytes, kept until SDL's cleanup call).
- Android: text through SDL as above; images through JNI to the activity (`ClipboardManager`): read the primary
  clip's first item with an image MIME type through `ContentResolver.openInputStream`, the bytes to the worker;
  write the PNG to the app's cache (`cache/clipboard/image.png`, replaced each time) and set a `ClipData` with its
  `FileProvider` URI (authority `<package>.clipboard`, readable by the receiver). The `FileProvider` and its paths
  file are build items.
- Apple: the main queue. macOS `NSPasteboard.generalPasteboard`: text `NSPasteboardTypeString`; image read PNG,
  else TIFF (`NSBitmapImageRep` to RGBA), write PNG and TIFF. iOS `UIPasteboard.generalPasteboard`: `string`,
  `image` (to RGBA through Core Graphics) and `setImage` (iOS shows its own paste notice; nothing to add).
- Decoding and encoding: stb_image (PNG, BMP, JPEG) and stb_image_write (PNG) in `luma_system.cpp`, compiled
  static there (`STB_IMAGE_STATIC`, `STBI_ONLY_PNG`/`BMP`/`JPEG`), so no symbol meets the render layer's copy.

### 6. Links

- The provider checks the URL (section 1 item 7), the 10-second spacing and that the window is focused (the display
  snapshot's `focused`, the flag the notice uses), writes the whole URL to the game's log, and hands it to the
  platform: Windows `ShellExecuteW(nullptr, L"open", url, …)` on the worker (a return above 32: OK); SDL `SDL_OpenURL`
  on the main thread (Android: SDL's intent); macOS `NSWorkspace openURL:`; iOS `UIApplication openURL:options:
  completionHandler:` (its answer is the event's status).
- When the platform answers OK, on the game thread at that frame's start, the Note:
  `galaxy::modlog::add_row(LUMA_MODLOG_CODE, LUMA_MODLOG_NOTE, mod, 140, site, "System.LinkOpened", {{"site", site}})`
  with `file` = the site, line and column 0. `site` = the URL's host lowercased, no port, no path.
- Deterministic mode: no browser; the OK answer and the Note as above.
- The first read of a mod in a session that gives text or an image (status OK) adds `System.ClipboardRead` the same
  way (`file` empty, key the mod, so repeats count; log only).

### 7. The platform interface: `runtime/include/galaxy/luma_system_platform.h`

```cpp
namespace galaxy::luma_system::platform {
// What the platform layer gives; every function any thread unless it says.
void facts(LumaSystemFacts& out) noexcept;      // platform, arch, renderer, locale, os, gpu, clipboard_images
void set_gpu_name(std::string_view name) noexcept;  // the renderer, once its device is made
void display_changed(const LumaSystemDisplay& now) noexcept;  // the platform's window thread (section 4)
bool focused() noexcept;
struct Image { uint32_t width = 0, height = 0; std::vector<uint8_t> rgba; std::vector<uint8_t> encoded; };
// Each calls `done` exactly once, from any thread; `status` as the events' statuses.
void read_text(std::function<void(int32_t status, std::string text)> done);
void read_image(std::function<void(int32_t status, Image image)> done);  // rgba, or encoded bytes to decode
void write_text(std::string text, std::function<void(int32_t status)> done);
void write_image(Image image, std::function<void(int32_t status)> done);  // rgba and its PNG, both filled
void open_link(std::string url, std::function<void(int32_t status)> done);
}
```
`luma_system.cpp` holds the tables, the checks, the requests, the Note, the deterministic clipboard and the codecs,
and calls only this interface.

### 8. Messages (codes 140-159)

| Code | Row | English | Args |
|---|---|---|---|
| 140 | `System.LinkOpened` | {mod} opened a page on {site} in your browser. | site |
| 141 | `System.ClipboardRead` | {mod} read your clipboard. | (none) |

Every other mistake uses the registry's rows: `Service.Argument` (out, url, text, width, height, size, fn),
`Service.Limit` (1 link), `Service.Handle`, `Service.Thread`, `Service.StructSize` (LumaSystemFacts 320,
LumaSystemDisplay 56).

### 9. LumaScript

`system.facts()`, `system.display()`; the clipboard's four calls as pendings of `Text`, `Image` and `Status`;
`system.links.open(url) -> pending<Status>`; the take functions are lumart's, inside the event's call; literal URLs
breaking item 7 and literal texts with U+0000 are compile errors.

### 10. Files

- New: `native/include/luma_system.h`; `runtime/include/galaxy/luma_system_platform.h`, `runtime/src/luma_system.cpp`,
  `runtime/src/luma_system_win32.cpp`, `platform/sdl/luma_system_sdl.cpp`, `platform/apple/luma_system_apple.mm`;
  Android: a Kotlin `ClipboardBridge` beside the activity and its JNI in `luma_system_sdl.cpp`.
- Registry 5.1's `Permission.system.clipboard.Text` becomes "Reads what you copy, text and pictures, and can replace
  it.", 14 languages, H.
- Changed: `luma_services_host.cpp` (`provide_all` provides the three system providers after `own`);
  `host_window_win32.cpp`, `host_window_apple.mm` (one `display_changed` call where each already handles the window's
  changes; Windows adds the `WM_WINDOWPOSCHANGED` case); `host_window_sdl.cpp` (the case labels of section 4 into one
  snapshot-and-call); `record_render_adapter`, which every renderer already calls once its device is made, keeps the
  description in memory before its early return, and a `recorded_render_adapter()` getter beside it that `facts`
  reads: no renderer file changes); the CMake lists adding the new files beside `luma_services_host.cpp`;
  `localization/NebulaText.tsv` (rows 140, 141; the permission text); `LumaServices.cpp`'s catalog (three rows:
  `system` 1.0 none, `system.clipboard` 1.0 `system.clipboard`, `system.links` 1.0 `system.links`).

### 11. Tests (deterministic mode)

| Test | Proves |
|---|---|
| 2221 | `facts`: platform 0, arch 0, renderer 0 or 1 (the run's), region and game_id of the run's disc, language its folder, lumacore 1.0, locale `en-US`, os and gpu "" |
| 2222 | `display`: width and height above 0, safe insets inside the window, changes at least 1, focused 1 |
| 2223 | `facts` with 8 bytes: Service.StructSize with 8 and 320; `display` with 8: 8 and 56 |
| 2224 | `system.clipboard` declared, required and declined: the mod off before the start check (start-check 1a), `Consent.OffDeclined` in the mod log naming the permission, no code started, no L506 |
| 2224b | `system.clipboard` imported, not declared: L506 at import, the same text as `lumalink check` |
| 2225 | set_text "Luma ✓ 星" then get_text: SET OK, TEXT with value 12 and the same bytes; take_text with size 5: 4 bytes and NUL, returns 12 |
| 2226 | set_image 3x2 then get_image: value 3 << 32 \| 2, the same 24 bytes |
| 2227 | get_text on the empty clipboard: TEXT with LUMA_E_NOT_FOUND; get_image: IMAGE with NOT_FOUND |
| 2228 | take_text after the event's call: LUMA_E_HANDLE, reported; set_text with invalid UTF-8: LUMA_E_ARGUMENT "text" |
| 2229 | a 5th request while 4 are open: LUMA_E_LIMIT, Service.Limit with 4 |
| 2230 | cancel before the answer: no event; cancel again: LUMA_E_HANDLE |
| 2231 | `open("https://example.org/luma")`: LINK OK; the fact `modlog` {source code, level note, code 140, file "example.org", text "<mod> opened a page on example.org in your browser."}; a second at once: LUMA_E_BUSY, no second Note; 10 s later (the monotonic clock) a link to example.com: a second entry; 10 s later example.org again: count 2 |
| 2231b | `ftp://x`, `https://a@example.org/`, `https://bücher.example/`: each LUMA_E_ARGUMENT "url", reported |
| 2232 | `system.links` declared and declined: as 2224 |
| 2232b | `system.links` imported, not declared: as 2224b |
| 2233 | get_text twice: one `System.ClipboardRead` entry, count 2 |

Not harness tests (2.2-commands, by a person at the machine, once per platform): the real clipboard both ways
with another program (text, a PNG from a browser, a screenshot), and a link opening the browser, on Windows, Linux,
Android, macOS.

### 12. As written: where the code differs from sections 4-10

Read before reviewing the branch; each is the code's, and the reason. Nothing here changes the header (section 2),
the codes or the tests' facts.
- The platform interface is a table (`galaxy::luma_system::Platform` of function pointers in
  luma_system_platform.h: facts, read_text, read_image, write_text, write_image, open_link), installed by the window
  layer with `set_platform` (`win32_platform()`, `sdl_platform()`, `apple_platform()`) when it makes the game window.
  Free functions of section 7 would need one definition a build and a stub for the module-only builds; the table
  lets luma_system.cpp compile once in every host and answer LUMA_E_ABSENT before a window exists. The display entry
  points are named for their layers (`win32_window_changed`, `sdl_display_event` + `sdl_window_changed`,
  `apple_window_changed`), each building the snapshot and calling `display_changed`.
- The renderer is pushed, not read (other direction): `record_render_adapter` calls
  `set_renderer(LUMA_SYSTEM_D3D12 / VULKAN / METAL, description)` (by the build's renderer macro) before its early
  return; no getter. Still no renderer file changes.
- Requests live on the host's heap (`new (std::nothrow)`), not the mod's: a platform thread may answer after the
  mod's memory is freed. mod_gone orphans them; a late answer frees itself and posts nothing; a queued
  node is the registry's to drop. So `takeout.<id>` never counts a system request.
- The Notes are written at the answer, on the thread that answers (the worker, SDL's main thread or the main
  queue), not at the frame's start: add_row takes any thread (mod-log 3128), and a Note written before the post
  survives the mod being taken out before the delivery (2234). `System.ClipboardRead` is added on every read that
  gives something to a request not cancelled (counted repeats, 2233), not only the first.
- The played language for `facts.language`: `galaxy::mods::played_language()`, a getter added beside the setter
  in mods.h/mods.cpp (the folder by `language_folder(game_id, …)`).
- The monotonic clock: the module exports `luma_services_monotonic_ns()` (LumaTime.cpp; deterministic: the
  frames' clock, so 10 s = 600 frames); provide_all looks it up after `own` and hands it to the providers with the
  log function. With no module export, spacing uses the host's steady clock.
- Windows image write: the DIBV5 is bottom-up (positive height), not top-down: several readers (older Office,
  some chat programs) take a negative height for a broken image. PNG goes first under "PNG". Focus also follows
  WM_SETFOCUS/WM_KILLFOCUS beside WM_ACTIVATE (an owned dialog takes focus without deactivating).
- SDL: `sdl_display_event(type)` is checked for every event in pump_events and one snapshot is taken after the
  loop (not one per case label), so a burst of resize events costs one compare.
- Android: `ClipboardBridge` is Java (`platform/android/app/src/main/java/dev/nebula/galaxy/ClipboardBridge.java`,
  as the activity), found through the activity's class loader; the FileProvider is the app's existing one (authority
  `<package>.files`, `res/xml/shared_files.xml` gains `<cache-path name="clipboard" path="clipboard/"/>`), not a
  second provider with `<package>.clipboard` and `clipboard_paths.xml`; proguard keeps the bridge. readImage gives
  null on a failure and an empty array for more than the limit (LUMA_E_TOO_LARGE).
- iOS display: not hooked (no iOS host window yet); `display` gives the zero snapshot there, `changes` 0, until
  that host calls `apple_window_changed`. With no snapshot ever given (`changes` 0) the window counts as in front
  for the clipboard and links (the game runs only in front there), so requests are not BUSY for good.

### J: row 141 in the 14 languages (columns and forms of address as row 140's table)

| Column | Text |
|---|---|
| UsEnglish | {mod} read your clipboard. |
| UsFrench, EuFrench | {mod} a lu votre presse-papiers. |
| UsSpanish, EuSpanish | {mod} leyó tu portapapeles. |
| EuGerman | {mod} hat deine Zwischenablage gelesen. |
| EuItalian | {mod} ha letto i tuoi appunti. |
| EuDutch | {mod} heeft je klembord gelezen. |
| UsPortuguese | {mod} leu sua área de transferência. |
| JpJapanese | {mod}がクリップボードをよみました。 |
| KrKorean | {mod}이(가) 클립보드를 읽었습니다. |
| CnSimpChinese | {mod}读取了你的剪贴板。 |
| TwTradChinese | {mod}讀取了你的剪貼簿。 |
