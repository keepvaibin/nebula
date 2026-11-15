---
title: movie service
shortTitle: movie
intro: Play your mod's own movies with the game's movie player.
contentType: reference
---

## About the movie service

The `movie` service plays your mod's own movie the way the game plays its own: full screen, with its sound as the clock and input held while it plays. A movie that replaces one of the game's movies needs no call.

| Service | Version | Permission | C import | LumaScript |
|---|---|---|---|---|
| `movie` | 1.0 | None | `LUMA_SERVICE(slot, "movie", "1.0")` | `use svc.movie` |

The header is `native/include/luma_movie.h`. In LumaScript, a service is called by the last part of its name, such as `movie.` for `movie`. Each function below shows its LumaScript form first and its C form second. For how the forms are made, see [Calling services](calling-services.md).

### Details from the header

LumaCore's movie service: a mod's own movie played by the game's movie player, as the game plays its own (full screen at the movie's own shape, its sound the clock, input held while it plays). The movie is the cook's: layers/<l>/assets/<path>.movie.json becomes Luma/<id>/<path>.nmov (AV1, decoded by dav1d where the build has it, NEBULA_AV1). A movie that replaces a game movie needs no call: the game plays it. Import: `LUMA_SERVICE(slot, "movie", "1.0")`.

## Types

| Name | Declaration | Description |
|---|---|---|
| `LumaMovie` | `typedef LumaHandle LumaMovie;` | A movie playing: one at a time for the whole game (the scene's movie player); ended by its end, stop, another movie, the scene's end or the mod's. |

## Enumerations

### Other constants

The event a movie gives (`LumaEvent`.kind; handle: the movie).

| Name | Description |
|---|---|
| `LUMA_MOVIE_EVENT_ENDED = 1` | Status `LUMA_OK`: played to its end (never for a movie that loops), or the player skipped it (+, Yes; a movie whose.movie.json leaves "skip" true); `LUMA_E_CANCELLED`: stop, another movie, the scene's end or the mod's ended it first. |

## `movie` functions

In C, each function takes your mod (`const LumaMod* mod`) first. LumaScript passes it for you.

### `play`

```lumascript
movie.play(name: text, fn: fn(e: movie.Event)?) -> movie.Movie?
```

```c
int32_t play(const LumaMod* mod, const char* name, LumaEventFn fn, void* user, LumaMovie* out);
```

Game thread, in a scene. The mod's movie `name` (its asset path without.movie.json: "movies/intro" is Luma/<id>/movies/intro.nmov) played from its first frame, looping when its.movie.json says "loop", its "subtitles" drawn over it in the game's cinema font (the text keys found as the game finds a message: the mod's text rows in the played language first), the player's + skip offered unless it says "skip": false. Whatever movie the player was playing ends first (a mod's: its `LUMA_E_CANCELLED`). With `fn`, `LUMA_MOVIE_EVENT_ENDED` comes to fn(user, event) at the frame's start after it ends. It returns `LUMA_OK` and sets `*out`. No such movie in the store: `LUMA_E_NOT_FOUND`, `Content.MissingMovie` (673) in the mod log; a file there that is not a cooked movie: `LUMA_E_FORMAT` (reported). No scene running, or a scene without the movie player: `LUMA_E_BUSY` (reported). A null or empty name or a null out: `LUMA_E_ARGUMENT` (reported). Off the game thread: `LUMA_E_THREAD` (reported); while the game takes the mod out: `LUMA_E_CANCELLED`.

Events: `ENDED(socket, status)`.

### `stop`

```lumascript
movie.stop(movie: movie.Movie)
```

```c
int32_t stop(const LumaMod* mod, LumaMovie movie);
```

Call it on the game thread. The movie stops; the game goes on (its end: `LUMA_E_CANCELLED`). `LUMA_OK`; a movie that ended or is not this mod's: `LUMA_E_HANDLE` (reported). Thread and taking out as play.

### `position`

```lumascript
movie.position(movie: movie.Movie) -> (i32, i32)
```

```c
int32_t position(const LumaMod* mod, LumaMovie movie, int32_t* frame, int32_t* frames);
```

Call it on the game thread. The frame on the screen (-1 before the first) into *frame and the movie's frames into *frames (either may be null). `LUMA_OK`; an ended movie: `LUMA_E_HANDLE` (reported). Thread and taking out as play.

## Further reading

* [Calling services](calling-services.md)
* [Services reference](index.md)
* [About services](../../concepts/services.md)
* [Permissions](../../concepts/permissions.md)
