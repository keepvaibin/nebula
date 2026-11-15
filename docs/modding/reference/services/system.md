---
title: system, system.clipboard and system.links services
shortTitle: system
intro: The system services give your mod facts about the platform, the game and the display, read and write the clipboard, and open web pages in the player's browser.
contentType: reference
---

## About the system services

| | `system` | `system.clipboard` | `system.links` |
|---|---|---|---|
| What it does | Facts about the platform and the game, and the window's display | Reads and writes text and pictures on the clipboard | Opens web pages in the player's browser |
| Version | 1.0 | 1.0 | 1.0 |
| Header | `luma_system.h` | `luma_system.h` | `luma_system.h` |
| Permission | None | `system.clipboard` | `system.links` |
| LumaScript | `use svc.system` | `use svc.system.clipboard`, then `clipboard.get_text(...)` | `use svc.system.links`, then `links.open(...)` |

The clipboard and links never make the game wait. Each call copies what you give it and returns at once. The work runs on the thread the platform needs, and the answer arrives as an event at a frame's start.

In a test run's deterministic mode, the services never touch the machine's clipboard or browser. The clipboard is a private one, empty at the start. A link is opened by writing its note to the mod log only. The window counts as in front. The facts are the real ones, except `os` and `gpu`, which are empty, and `locale`, which is `en-US`.

## system functions

### facts

Fills in what does not change during a session.

```c
int32_t (*facts)(const LumaMod* mod, LumaSystemFacts* out);
```

```lumascript
system.facts() -> system.Facts
```

Any thread. Returns `LUMA_OK`. A null `out`, or an `out->size` below 320 (the 1.0 struct), is `LUMA_E_ARGUMENT`, reported.

### display

Fills in the game window's display now.

```c
int32_t (*display)(const LumaMod* mod, LumaSystemDisplay* out);
```

```lumascript
system.display() -> system.Display
```

Game thread. Returns `LUMA_OK`. A null `out`, or an `out->size` below 56 (the 1.0 struct), is `LUMA_E_ARGUMENT`, reported. The call reads a copy the platform keeps, so it makes no system call. Compare `changes` with the value you read last to know whether anything changed.

## system structs

### LumaSystemFacts

`system.Facts` in LumaScript. Strings are UTF-8, cut at a character's end to fit.

| Field | C type | LumaScript | Description |
|---|---|---|---|
| `size` | `uint32_t` | | `sizeof(LumaSystemFacts)` as the mod was built. |
| `platform` | `LumaSystemPlatform` | `system.Platform` | The platform of this build of Nebula. |
| `arch` | `LumaSystemArch` | `system.Arch` | The processor architecture of this build. |
| `renderer` | `LumaSystemRenderer` | `system.Renderer` | The renderer the game draws with this session. |
| `region` | `LumaSystemRegion` | `system.Region` | The disc's region. |
| `lumacore_major` | `uint32_t` | `u32` | LumaCore's major version: 1. |
| `lumacore_minor` | `uint32_t` | `u32` | LumaCore's minor version: 0. |
| `clipboard_images` | `uint32_t` | `u32` | 1 when this platform's clipboard carries pictures. 0 when `get_image` and `set_image` answer `LUMA_E_ABSENT`. |
| `game_id` | `char[8]` | `text` | The disc's ID, such as `RMGE01`. |
| `language` | `char[16]` | `text` | The language the game plays in, by the game's folder name, such as `UsEnglish` or `JpJapanese`. |
| `game_version` | `char[32]` | `text` | Nebula's version, such as `2.0.0`. |
| `locale` | `char[40]` | `text` | The player's system locale in BCP 47 form, such as `en-US`, `pt-BR` or `zh-Hant-TW`. `und` when unknown. |
| `os` | `char[64]` | `text` | The system's name and version for people, such as `Windows 11 (26200)`. Empty when unknown. |
| `gpu` | `char[128]` | `text` | The renderer's device, such as `NVIDIA GeForce RTX 4070`. Empty when unknown. |

`clipboard_images` is 1 on Windows, macOS, iOS, Android and Linux, except a Linux session whose clipboard offers no data types.

### LumaSystemDisplay

`system.Display` in LumaScript. Pixels are the window's drawable pixels, the ones the game renders into.

| Field | C type | LumaScript | Description |
|---|---|---|---|
| `size` | `uint32_t` | | `sizeof(LumaSystemDisplay)` as the mod was built. |
| `changes` | `uint32_t` | `u32` | Grows by 1 each time any field below changes. 1 at the first reading. |
| `width`, `height` | `uint32_t` | `u32` | The window's drawable size. |
| `display_width`, `display_height` | `uint32_t` | `u32` | The size of the display the window is on, in its current mode. |
| `scale` | `uint32_t` | `u32` | The content scale in thousandths. 1000 is 96 dpi on Windows, and 1x on Apple systems and Android. |
| `refresh_mhz` | `uint32_t` | `u32` | The display's refresh rate in thousandths of a hertz. 0 when unknown. |
| `safe_left`, `safe_top`, `safe_right`, `safe_bottom` | `uint32_t` | `u32` | Insets of the safe area from each edge of the window, for notches, rounded corners and system bars. 0 where there are none. |
| `fullscreen` | `uint32_t` | `u32` | 1 when the window fills its display. |
| `focused` | `uint32_t` | `u32` | 1 when the game window is in front. |

## system enums

### LumaSystemPlatform

`system.Platform` in LumaScript.

| C | LumaScript | Value |
|---|---|---|
| `LUMA_SYSTEM_WINDOWS` | `system.Platform.Windows` | 0 |
| `LUMA_SYSTEM_LINUX` | `system.Platform.Linux` | 1. SteamOS included. |
| `LUMA_SYSTEM_ANDROID` | `system.Platform.Android` | 2 |
| `LUMA_SYSTEM_MACOS` | `system.Platform.Macos` | 3 |
| `LUMA_SYSTEM_IOS` | `system.Platform.Ios` | 4 |

### LumaSystemArch

`system.Arch` in LumaScript.

| C | LumaScript | Value |
|---|---|---|
| `LUMA_SYSTEM_X64` | `system.Arch.X64` | 0 |
| `LUMA_SYSTEM_ARM64` | `system.Arch.Arm64` | 1 |

### LumaSystemRenderer

`system.Renderer` in LumaScript.

| C | LumaScript | Value |
|---|---|---|
| `LUMA_SYSTEM_D3D12` | `system.Renderer.D3d12` | 0 |
| `LUMA_SYSTEM_VULKAN` | `system.Renderer.Vulkan` | 1 |
| `LUMA_SYSTEM_METAL` | `system.Renderer.Metal` | 2 |

### LumaSystemRegion

`system.Region` in LumaScript. The disc's region.

| C | LumaScript | Value | Disc |
|---|---|---|---|
| `LUMA_SYSTEM_US` | `system.Region.Us` | 0 | `RMGE01` |
| `LUMA_SYSTEM_EU` | `system.Region.Eu` | 1 | `RMGP01` |
| `LUMA_SYSTEM_JP` | `system.Region.Jp` | 2 | `RMGJ01` |

## system.clipboard functions

The clipboard is read only while the game window is in front. Otherwise the event's status is `LUMA_E_BUSY`. A mod has at most 4 clipboard requests open at once. A fifth is `LUMA_E_LIMIT`, reported. A request is closed after its event's call.

Pictures are RGBA, 8 bits a channel, straight alpha, rows from top to bottom, tightly packed. At most 8192 pixels a side and 64 MiB of pixels. Text is UTF-8 with no NUL inside, at most 16 MiB either way. Line ends are as the clipboard holds them.

The first time your mod reads text or a picture in a session, the mod log gets the note `System.ClipboardRead`.

### get_text

Asks for the clipboard's text.

```c
int32_t (*get_text)(const LumaMod* mod, LumaEventFn fn, void* user, LumaHandle* out);
```

```lumascript
clipboard.get_text(fn(e: clipboard.TextEvent)) -> clipboard.Request?
wait clipboard.get_text() -> text?
```

Game thread. Returns `LUMA_PENDING` with the request. A `TEXT` event follows. A null `fn` or `out` is `LUMA_E_ARGUMENT`. A fifth open request is `LUMA_E_LIMIT`. Both are reported.

### take_text

Copies the text inside the `TEXT` event's call. C only: LumaScript takes the text for you.

```c
int64_t (*take_text)(const LumaMod* mod, LumaHandle request, char* buf, uint64_t size);
```

Call it once, inside the event's call for that handle. It copies `min(value, size - 1)` bytes and a NUL into `buf`, and returns `value`, the whole text's length. `size` is at least 1. Outside the call, for another handle, or a second time, it returns `LUMA_E_HANDLE`, reported. A null `buf` or a `size` of 0 is `LUMA_E_ARGUMENT`, reported.

### get_image

Asks for the clipboard's picture.

```c
int32_t (*get_image)(const LumaMod* mod, LumaEventFn fn, void* user, LumaHandle* out);
```

```lumascript
clipboard.get_image(fn(e: clipboard.ImageEvent)) -> clipboard.Request?
wait clipboard.get_image() -> bytes?
```

Game thread. Returns `LUMA_PENDING` with the request. An `IMAGE` event follows, whose `value` is `width << 32 | height`. Where `clipboard_images` is 0, it returns `LUMA_E_ABSENT`, an answer. A null `fn` or `out` is `LUMA_E_ARGUMENT`. A fifth open request is `LUMA_E_LIMIT`. Both are reported.

The pixels come with the event as `data`. To know the picture's size, call it with a closure and read `e.value`: the width is `e.value >> 32` and the height is `e.value & 0xFFFFFFFF`.

The clipboard's picture is read from PNG. Windows also reads its own bitmap formats, and Linux and Apple systems also read BMP, JPEG or TIFF as the platform offers them. A picture the game cannot decode gives `LUMA_E_FORMAT`.

### take_image

Copies the pixels inside the `IMAGE` event's call. C only.

```c
int32_t (*take_image)(const LumaMod* mod, LumaHandle request, uint8_t* rgba, uint64_t size);
```

Call it once, inside the event's call. `size` must be at least `width * height * 4`, else it returns `LUMA_E_ARGUMENT` naming `size`, reported. Returns `LUMA_OK`. Outside the call, or a second time, it returns `LUMA_E_HANDLE`, reported.

### set_text

Puts text on the clipboard.

```c
int32_t (*set_text)(const LumaMod* mod, const char* text, uint64_t bytes, LumaEventFn fn, void* user,
                    LumaHandle* out);
```

```lumascript
clipboard.set_text(text: text, fn(e: clipboard.SetEvent)) -> clipboard.Request?
wait clipboard.set_text(text: text) -> u64?
```

Game thread. The text is copied before the call returns. No NUL is needed. Returns `LUMA_PENDING`, and a `SET` event follows.

| Status | When |
|---|---|
| `LUMA_E_ARGUMENT` (reported) | The text is not UTF-8, or has a NUL inside; or a null `fn`, `out` or `text`. |
| `LUMA_E_TOO_LARGE` (reported) | Over 16 MiB. |
| `LUMA_E_LIMIT` (reported) | A fifth open request. |

### set_image

Puts a picture on the clipboard.

```c
int32_t (*set_image)(const LumaMod* mod, const uint8_t* rgba, uint64_t size, uint32_t width, uint32_t height,
                     LumaEventFn fn, void* user, LumaHandle* out);
```

```lumascript
clipboard.set_image(rgba: bytes, width: u32, height: u32, fn(e: clipboard.SetEvent)) -> clipboard.Request?
wait clipboard.set_image(rgba: bytes, width: u32, height: u32) -> u64?
```

| Parameter | Type | Description |
|---|---|---|
| `rgba` | `bytes` | `width * height * 4` bytes of pixels. |
| `width` | `u32` | 1 to 8192. |
| `height` | `u32` | 1 to 8192. |

Game thread. The pixels are copied before the call returns. The picture is written to the clipboard as PNG, and on Windows also as a bitmap. Returns `LUMA_PENDING`, and a `SET` event follows.

| Status | When |
|---|---|
| `LUMA_E_ARGUMENT` (reported) | `width` or `height` outside 1 to 8192, over 64 MiB, a size other than `width * height * 4`, or a null `fn`, `out` or `rgba`. |
| `LUMA_E_LIMIT` (reported) | A fifth open request. |
| `LUMA_E_ABSENT` | `clipboard_images` is 0. |

### cancel

Ends a request. No event for it is delivered after this returns. A set already handed to the system may still reach the clipboard.

```c
int32_t (*cancel)(const LumaMod* mod, LumaHandle request);
```

```lumascript
clipboard.cancel(request: clipboard.Request)
```

Returns `LUMA_OK`. A handle that is not your mod's open request is `LUMA_E_HANDLE`, reported.

## system.clipboard events

`handle` is the request's. None carries `data` of its own; the text and pictures come through the take functions.

| Kind | Value | LumaScript record | `value` on `LUMA_OK` |
|---|---|---|---|
| `LUMA_SYSTEM_CLIPBOARD_EVENT_TEXT` | 1 | `clipboard.TextEvent`, with `data: text` | The text's length in bytes, with no NUL. |
| `LUMA_SYSTEM_CLIPBOARD_EVENT_IMAGE` | 2 | `clipboard.ImageEvent`, with `data: bytes` | `width << 32 \| height`. |
| `LUMA_SYSTEM_CLIPBOARD_EVENT_SET` | 3 | `clipboard.SetEvent` | 0. The clipboard holds it. |

An event can carry these statuses:

| Status | When |
|---|---|
| `LUMA_OK` | Done. |
| `LUMA_E_NOT_FOUND` | No text, or no picture, on the clipboard. |
| `LUMA_E_TOO_LARGE` | Over 16 MiB of text, or over 8192 a side or 64 MiB of pixels. |
| `LUMA_E_BUSY` | The game was not in front. |
| `LUMA_E_IO` | The system refused, such as another program holding the clipboard for 250 ms. |
| `LUMA_E_FORMAT` | A picture the game cannot decode. |

## system.links functions

### open

Opens a web page in the system's browser.

```c
int32_t (*open)(const LumaMod* mod, const char* url, LumaEventFn fn, void* user, LumaHandle* out);
```

```lumascript
links.open(url: text, fn(e: links.LinkEvent)) -> links.Request?
wait links.open(url: text) -> u64?
```

| Parameter | Type | Description |
|---|---|---|
| `url` | `text` | An `https://` or `http://` address of at most 2048 bytes, with no space or control byte, no user name or password before the host, and an ASCII host. Write an internationalized host in its `xn--` form. |

Game thread. Returns `LUMA_PENDING`, and a `LINK` event follows. Once the system took the link, the mod log gets the note `System.LinkOpened` naming the site, so the player always sees which mod opened a page, and where. The whole address is written to the game's log only.

| Status | When |
|---|---|
| `LUMA_E_BUSY` | Your mod opened another link in the last 10 seconds, one is still open, or the game is not in front. An answer, not reported. |
| `LUMA_E_ARGUMENT` (reported) | The address breaks the rules above, or `fn` or `out` is null. |

### cancel

Ends a request. No event for it is delivered after this returns. A link already handed to the system may still open.

```c
int32_t (*cancel)(const LumaMod* mod, LumaHandle request);
```

```lumascript
links.cancel(request: links.Request)
```

Returns `LUMA_OK`. A handle that is not your mod's open request is `LUMA_E_HANDLE`, reported.

## system.links events

| Kind | Value | LumaScript record | Description |
|---|---|---|---|
| `LUMA_SYSTEM_LINKS_EVENT_LINK` | 1 | `links.LinkEvent` | `status` is `LUMA_OK` when the system took the link, or `LUMA_E_IO` when it refused it. `value` is 0. No data. |

## Example

This mod adapts to the platform and copies a share code to the clipboard.

```lumascript
use svc.system
use svc.system.clipboard

on start {
    let facts = system.facts()
    log "Nebula {facts.game_version} on {facts.platform} ({facts.arch}), {facts.renderer}, {facts.language}"
    if facts.platform == system.Platform.Android or facts.platform == system.Platform.Ios {
        let d = system.display()
        log "safe area: {d.safe_left} {d.safe_top} {d.safe_right} {d.safe_bottom}"
    }
}

task copy_code(code: text) {
    if wait clipboard.set_text(code) == none {
        log "the code was not copied"
    }
}
```

The mod's `luma.json` lists the `system.clipboard` permission.

## Mod log messages

| Code | ID | Level | Message |
|---|---|---|---|
| 140 | `System.LinkOpened` | Note | `{mod} opened a page on {site} in your browser.` |
| 141 | `System.ClipboardRead` | Note | `{mod} read your clipboard.` |

The services also use the common messages: `Service.Argument` (`out`, `url`, `text`, `width`, `height`, `size`, `fn`), `Service.Limit`, `Service.Handle`, `Service.Thread` and `Service.StructSize` (`LumaSystemFacts` 320 bytes, `LumaSystemDisplay` 56). For more information, see [Calling services](calling-services.md#mod-log-messages).

## Further reading

* [About permissions](../../concepts/permissions.md)
* [Calling services](calling-services.md)
