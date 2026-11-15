---
title: capture services
shortTitle: capture
intro: Read samples from a microphone and pictures from a camera.
contentType: reference
---

## About the capture services

`audio.microphone` gives your mod a microphone's samples, and `capture.camera` gives it a camera's pictures. Each needs its permission, and the operating system may also ask the player. On platforms with no capture backend, the calls answer `LUMA_E_ABSENT`.

| Service | Version | Permission | C import | LumaScript |
|---|---|---|---|---|
| `audio.microphone` | 1.0 | `audio.microphone` | `LUMA_SERVICE(slot, "audio.microphone", "1.0")` | `use svc.audio.microphone` |
| `capture.camera` | 1.0 | `capture.camera` | `LUMA_SERVICE(slot, "capture.camera", "1.0")` | `use svc.capture.camera` |

The header is `native/include/luma_capture.h`. In LumaScript, a service is called by the last part of its name, such as `microphone.` for `audio.microphone`. Each function below shows its LumaScript form first and its C form second. For how the forms are made, see [Calling services](calling-services.md).

### Details from the header

LumaCore's capture services: the player's microphone (`audio.microphone` 1.0) and camera (`capture.camera` 1.0), each its own permission of the same name, granted on the consent screen and checked when the mod binds. The game's own picture and mix need no permission: they are draw's and audio.tap's. Every call is the game thread's and never blocks: the device opens on a thread of its own and what it hears or sees comes as events at a frame's start. A mod that stops closes them the same frame. Where the platform or the device has none: open answers `LUMA_E_ABSENT`, or (the device found missing or refused on its own thread) ENDED with `LUMA_E_ABSENT` follows.

## Limits and constants

| Name | Value | Description |
|---|---|---|
| `LUMA_AUDIO_MICROPHONE_BLOCK_FRAMES` | `1024` | Frames in a SAMPLES block. |
| `LUMA_CAPTURE_CAMERA_WIDTH_MOST` | `3840` |  |
| `LUMA_CAPTURE_CAMERA_HEIGHT_MOST` | `2160` |  |
| `LUMA_CAPTURE_CAMERA_FPS_MOST` | `60` |  |

## Enumerations

### `LumaAudioMicrophoneEvent`

The microphone's events (`LumaEvent`.kind); `handle` is the open's.

| Name | Description |
|---|---|
| `LUMA_AUDIO_MICROPHONE_EVENT_SAMPLES = 1` | Data, `LUMA_AUDIO_MICROPHONE_BLOCK_FRAMES` frames of float (the desc's channels, interleaved; valid during the call); value: the frames; flags: the rate; status `LUMA_E_BUSY` when blocks before it were lost. |
| `LUMA_AUDIO_MICROPHONE_EVENT_ENDED = 3` | The device stopped on its own: `LUMA_E_ABSENT` (none, or the player's system refused it), `LUMA_E_IO` (unplugged, taken by another program). The handle is closed after it. |

### `LumaCaptureCameraEvent`

The camera's events (`LumaEvent`.kind); `handle` is the open's.

| Name | Description |
|---|---|
| `LUMA_CAPTURE_CAMERA_EVENT_FRAME = 2` | Data, a `LumaCaptureCameraFrame` (valid during the call), the newest picture (pictures the mod had no frame for are skipped: status `LUMA_E_BUSY`). |
| `LUMA_CAPTURE_CAMERA_EVENT_ENDED = 3` | The device stopped on its own: `LUMA_E_ABSENT` (none, or the player's system refused it), `LUMA_E_IO` (unplugged, taken by another program). The handle is closed after it. |

## Structures

### `LumaAudioMicrophoneDesc`

| Name | Description |
|---|---|
| `uint32_t size` | Sizeof(`LumaAudioMicrophoneDesc`). |
| `uint32_t rate` | 8000 to 192000 Hz (resampled from the device's); 0: 48000. |
| `uint32_t channels` | 1 or 2. |
| `uint32_t reserved` |  |

### `LumaCaptureCameraDesc`

| Name | Description |
|---|---|
| `uint32_t size` | Sizeof(`LumaCaptureCameraDesc`). |
| `uint32_t width` | Wanted, to `LUMA_CAPTURE_CAMERA_WIDTH_MOST`; 0: the camera's own (FRAME says what it gives). |
| `uint32_t height` | To `LUMA_CAPTURE_CAMERA_HEIGHT_MOST`; 0 with width 0. |
| `uint32_t fps` | Wanted, to `LUMA_CAPTURE_CAMERA_FPS_MOST`; 0: the camera's own. |

### `LumaCaptureCameraFrame`

A FRAME's data: RGBA, 8 bits each, rows top first (draw's texture from pixels takes it as is).

| Name | Description |
|---|---|
| `uint32_t size` | Sizeof(`LumaCaptureCameraFrame`). |
| `uint32_t width` |  |
| `uint32_t height` |  |
| `uint32_t stride` | Bytes from a row to the next: width × 4. |
| `uint64_t time_ns` | When the camera took it, steady clock. |
| `const uint8_t* pixels` |  |

## `audio.microphone` functions

In C, each function takes your mod (`const LumaMod* mod`) first. LumaScript passes it for you.

### `open`

```lumascript
microphone.open(desc: microphone.Desc, fn(e: microphone.Event)) -> microphone.Request?
```

```c
int32_t open(const LumaMod* mod, const LumaAudioMicrophoneDesc* desc, LumaEventFn fn, void* user, LumaHandle* out);
```

Opens the player's default microphone: SAMPLES to fn (not null) from a frame after. One a mod (a 2nd: `LUMA_E_LIMIT` (reported)). A desc of the wrong size or out of range, a null fn or out: `LUMA_E_ARGUMENT` (reported); no microphone on this platform: `LUMA_E_ABSENT`.

Events: `SAMPLES(socket, status, data, value, flags)`, `ENDED(socket, status)`.

### `close`

```lumascript
microphone.close(microphone: microphone.Request)
```

```c
int32_t close(const LumaMod* mod, LumaHandle microphone);
```

Closes it; no event after. A handle not the mod's: `LUMA_E_HANDLE` (reported).

## `capture.camera` functions

In C, each function takes your mod (`const LumaMod* mod`) first. LumaScript passes it for you.

### `open`

```lumascript
camera.open(desc: camera.Desc, fn(e: camera.Event)) -> camera.Request?
```

```c
int32_t open(const LumaMod* mod, const LumaCaptureCameraDesc* desc, LumaEventFn fn, void* user, LumaHandle* out);
```

Opens the player's first camera: FRAME to fn (not null), at most one a game frame. One a mod (a 2nd: `LUMA_E_LIMIT` (reported)). A desc of the wrong size or out of range, a null fn or out: `LUMA_E_ARGUMENT` (reported); no camera on this platform: `LUMA_E_ABSENT`.

Events: `FRAME(socket, status, data)`, `ENDED(socket, status)`.

### `close`

```lumascript
camera.close(camera: camera.Request)
```

```c
int32_t close(const LumaMod* mod, LumaHandle camera);
```

Closes it; no event after. A handle not the mod's: `LUMA_E_HANDLE` (reported).

## Further reading

* [Calling services](calling-services.md)
* [Services reference](index.md)
* [About services](../../concepts/services.md)
* [Permissions](../../concepts/permissions.md)
