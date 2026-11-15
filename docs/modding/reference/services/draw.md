---
title: draw service
shortTitle: draw
intro: Draw shapes, text and pictures at the game's draw points.
contentType: reference
---

## About the draw service

The `draw` service lets your mod draw at the game's draw points. In LumaScript, you draw in an `on draw POINT` handler.

| Service | Version | Permission | C import | LumaScript |
|---|---|---|---|---|
| `draw` | 1.0 | None | `LUMA_SERVICE(slot, "draw", "1.0")` | `use svc.draw` |

The header is `native/include/luma_draw.h`. In LumaScript, a service is called by the last part of its name, such as `draw.` for `draw`. Each function below shows its LumaScript form first and its C form second. For how the forms are made, see [Calling services](calling-services.md).

### Details from the header

LumaCore's draw service: a mod draws at named points of the game's frame. In 2-D over the game (rectangles, lines, triangles, images, text in the game's fonts and the mod's own, every language), clipped and transformed; in 3-D in the world, depth tested (lines, triangles, models, billboards). Textures come by content name or from the mod's own pixels. Everything a mod draws is drawn the frame it asks and never again: nothing stays in the renderer, and a mod that is off draws nothing from that frame. Import: `LUMA_SERVICE(slot, "draw", "1.0")`. Units. 2-D: the game's layout units, the frame its HUD is laid out in: (0, 0) the screen's top left, x right, y down, 456 units high, as wide as the screen's shape makes it (608 at 4:3, 832 at 16:9, more on wider screens); the HUD's own 4:3 frame (608 x 456) centerd in it is the safe frame (screen's safe_*). 3-D: the game's world units (centimetres), its axes.

## Limits and constants

| Name | Value | Description |
|---|---|---|
| `LUMA_DRAW_PRIMITIVES_MOST` | `20000` | Per mod. Primitives a frame (a rect, line, triangle, image, glyph or 3-D primitive each counts 1; a mesh its triangles): past it the call draws nothing and returns `LUMA_E_LIMIT` (reported once a frame, `Service.Limit`). |
| `LUMA_DRAW_TEXTURES_MOST` | `256` | Textures alive at once (by name and from pixels), and their bytes (width x height x 4 summed, from pixels). |
| `LUMA_DRAW_TEXTURE_BYTES_MOST` | `(64ull * 1024ull * 1024ull)` |  |
| `LUMA_DRAW_TEXTURE_SIDE_MOST` | `4096` | A texture from pixels: at most this wide and high. |
| `LUMA_DRAW_STACK_MOST` | `16` | Clip and transform pushes deep at once, in one point's call. |
| `LUMA_DRAW_FONTS_MOST` | `32` | Fonts one mod has found. |
| `LUMA_DRAW_TEXT_BYTES_MOST` | `4096` | A text call's text: at most this many bytes (longer: drawn cut, `LUMA_OK`, `Draw.TextCut` in the mod log). |
| `LUMA_DRAW_POINTS` | `7` |  |

## Types

| Name | Declaration | Description |
|---|---|---|
| `LumaDrawTexture` | `typedef LumaHandle LumaDrawTexture;` | A texture: by content name (texture) or from pixels (texture_from_pixels). Owned by the mod until texture_free or its end. |
| `LumaDrawFont` | `typedef LumaHandle LumaDrawFont;` | A font: by name (font). Kept until the mod's end. |
| `LumaDrawFn` | `typedef void (*LumaDrawFn)(void* user, const LumaDrawScreen* screen);` | A point's function: called on the game thread at that point of every frame while it is set, through the kernel's guard (a fault takes the mod out); `screen` valid during the call only. The draw calls are valid inside it only. |

## Enumerations

### `LumaDrawPoint`

The frame's points (draw.at). The world points are 3-D: drawn in the scene's camera, depth tested; the 2-D calls there return `LUMA_E_STATE`. The others are 2-D: the 3-D calls there return `LUMA_E_STATE`.

| Name | Description |
|---|---|
| `LUMA_DRAW_WORLD_BEFORE = 0` | The scene's camera set, before the game draws its world. |
| `LUMA_DRAW_WORLD_OPAQUE_AFTER = 1` | After the world's opaque surfaces, before its translucent ones. |
| `LUMA_DRAW_WORLD_AFTER = 2` | After the world and its effects, before the screen's 2-D. |
| `LUMA_DRAW_HUD_BEFORE = 3` | The screen's 2-D: before the game's HUD (under it). |
| `LUMA_DRAW_HUD_AFTER = 4` | Over the game's HUD, under its pause menu and its wipes. |
| `LUMA_DRAW_LOADING = 5` | While the game loads a scene (its loading screen), 2-D over it. |
| `LUMA_DRAW_PRESENT = 6` | Every frame the game shows, over everything (pause menu, wipes, loading). |

### `LumaDrawAlign`

Text placing (text, text_measure): where x is on each line.

| Name | Description |
|---|---|
| `LUMA_DRAW_LEFT = 0` | Lines start at x. |
| `LUMA_DRAW_CENTRE = 1` | Lines centerd on x. |
| `LUMA_DRAW_RIGHT = 2` | Lines end at x. |

### Other constants

Texture_from_pixels flags.

| Name | Description |
|---|---|
| `LUMA_DRAW_TEXTURE_NEAREST = 1u` | Sampled nearest (pixel art); else linear. |
| `LUMA_DRAW_TEXTURE_REPEAT = 2u` | Wraps outside 0-1 (image's src past the texture); else clamps. |

## Structures

### `LumaDrawVec2`

| Name | Description |
|---|---|
| `float x, y` |  |

### `LumaDrawVec3`

| Name | Description |
|---|---|
| `float x, y, z` |  |

### `LumaDrawRect`

| Name | Description |
|---|---|
| `float x, y` | Top left. |
| `float w, h` | 0 or more. |

### `LumaDrawScreen`

The 2-D frame now (screen, and the second argument of a point's function).

| Name | Description |
|---|---|
| `uint32_t size` | Sizeof(`LumaDrawScreen`) as the mod was built. |
| `uint32_t point` | In a point's function: its `LumaDrawPoint`; from screen(): `LUMA_DRAW_POINTS`. |
| `uint64_t frame` | Frames presented since start (time's `frame`). |
| `float w, h` | The 2-D frame, layout units (h is 456). |
| `float safe_x, safe_y, safe_w, safe_h` | The HUD's 4:3 frame in it. |
| `uint32_t reserved[2]` | 0. |

## `draw` functions

In C, each function takes your mod (`const LumaMod* mod`) first. LumaScript passes it for you.

### `at`

```lumascript
draw.at(point: draw.Point, fn: fn(e: draw.Event))
```

```c
int32_t at(const LumaMod* mod, LumaDrawPoint point, LumaDrawFn fn, void* user);
```

---- Points ---- 1.0. Call it on the game thread. fn(user, screen) at `point` of every frame from the next one on (a null fn removes it); one function a point a mod, the last set wins. Mods at one point are called in the Mods list's order, the bottom one first (the top one draws over). `LUMA_OK`. A point outside the enum: `LUMA_E_ARGUMENT` (reported).

### `screen`

```lumascript
draw.screen() -> draw.Screen
```

```c
int32_t screen(const LumaMod* mod, LumaDrawScreen* out);
```

Game thread, anywhere. The 2-D frame now. `LUMA_OK`. A null out, or a size below 1.0's struct (48): `LUMA_E_ARGUMENT` (reported) (`Service.StructSize`).

### `rect`

```lumascript
draw.rect(rect: draw.Rect, colour: color) -> bool
```

```c
int32_t rect(const LumaMod* mod, const LumaDrawRect* rect, LumaColor colour);
```

---- 2-D (the 2-D points only, else `LUMA_E_STATE` (reported)) ---- Every 2-D call: inside the clip on top (if any), through the transform on top (if any); colors 0xRRGGBBAA, blended by alpha over what is drawn. A null pointer argument, a NaN or infinite number: `LUMA_E_ARGUMENT` (reported). Past `LUMA_DRAW_PRIMITIVES_MOST`: `LUMA_E_LIMIT`. 1.0. A filled rectangle.

### `rect_outline`

```lumascript
draw.rect_outline(rect: draw.Rect, thickness: f32, colour: color)
```

```c
int32_t rect_outline(const LumaMod* mod, const LumaDrawRect* rect, float thickness, LumaColor colour);
```

A rectangle's outline `thickness` units wide, inside its edges (thickness above half the smaller side: the rectangle filled). thickness above 0.

### `line`

```lumascript
draw.line(a: draw.Vec2, b: draw.Vec2, thickness: f32, colour: color)
```

```c
int32_t line(const LumaMod* mod, LumaDrawVec2 a, LumaDrawVec2 b, float thickness, LumaColor colour);
```

A line from a to b, `thickness` units wide (above 0), square ends.

### `tri`

```lumascript
draw.tri(points: (draw.Vec2, draw.Vec2, draw.Vec2), colours: (color, color, color))
```

```c
int32_t tri(const LumaMod* mod, const LumaDrawVec2 points[3], const LumaColor colours[3]);
```

A filled triangle, a color each corner (blended between them).

### `image`

```lumascript
draw.image(texture: draw.Texture, dst: draw.Rect, src: draw.Rect?, colour: color)
```

```c
int32_t image(const LumaMod* mod, LumaDrawTexture texture, const LumaDrawRect* dst, const LumaDrawRect* src, LumaColor colour);
```

`texture`'s part `src` (null: the whole; in texels) drawn into `dst`, times `colour` (0xFFFFFFFF as it is). A texture freed or never this mod's: the placeholder (magenta and black squares) drawn, `LUMA_E_HANDLE` (reported).

### `text`

```lumascript
draw.text(font: draw.Font, text: text, x: f32, y: f32, size: f32, colour: color, align: draw.Align, wrap: f32) -> f32
```

```c
int32_t text(const LumaMod* mod, LumaDrawFont font, const char* text, float x, float y, float size, LumaColor colour, LumaDrawAlign align, float wrap, float* advance);
```

Text, UTF-8, in `font` at `size` units a line, from (x, y) its first line's top, placed by `align`; lines break at '\n', and at spaces (or between any two characters of languages written without them) past `wrap` units (0: no wrapping). "MessageFont26" draws the language played, its Japanese, Korean and Chinese faces included; a mod's font draws a character it lacks from its fallback game font (3.8); a character no font here has is drawn as the font's stand-in, `Draw.NoGlyph` in the mod log. `advance` (null: not written): where the text ended, from x (the end of its last line for `LUMA_DRAW_LEFT`), so text continues after it. Not UTF-8: `LUMA_E_ARGUMENT` (reported) (Draw.NotUtf8); a font not this mod's: `LUMA_E_HANDLE` (reported); size above 0 and at most 456.

### `text_measure`

```lumascript
draw.text_measure(font: draw.Font, text: text, size: f32, wrap: f32) -> draw.Vec2
```

```c
int32_t text_measure(const LumaMod* mod, LumaDrawFont font, const char* text, float size, float wrap, LumaDrawVec2* out);
```

Anywhere (no point needed). The box text() would fill from (0, 0): out->x the widest line, out->y its height (lines times the line's height). Arguments as text.

### `clip_push`

```lumascript
draw.clip_push(rect: draw.Rect)
```

```c
int32_t clip_push(const LumaMod* mod, const LumaDrawRect* rect);
```

Drawing from here on is cut to `rect` (in the units the transform on top gives; made screen-aligned: the box around it) within the clip under it. Over `LUMA_DRAW_STACK_MOST`: `LUMA_E_LIMIT` (reported).

### `clip_pop`

```lumascript
draw.clip_pop() -> bool
```

```c
int32_t clip_pop(const LumaMod* mod);
```

The clip under it again. None pushed in this point's call: `LUMA_E_STATE` (reported).

### `transform_push`

```lumascript
draw.transform_push(m: (f32, f32, f32, f32, f32, f32))
```

```c
int32_t transform_push(const LumaMod* mod, const float m[6]);
```

Points from here on go through `m` (x' = m[0] x + m[1] y + m[2], y' = m[3] x + m[4] y + m[5]) and then the transform under it. Over `LUMA_DRAW_STACK_MOST`: `LUMA_E_LIMIT` (reported).

### `transform_pop`

```lumascript
draw.transform_pop() -> bool
```

```c
int32_t transform_pop(const LumaMod* mod);
```

The transform under it again. None pushed: `LUMA_E_STATE` (reported).

### `font`

```lumascript
draw.font(name: text) -> draw.Font?
```

```c
int32_t font(const LumaMod* mod, const char* name, LumaDrawFont* out);
```

---- Fonts and textures (anywhere on the game thread) ---- 1.0. A font by name: the game's ("MessageFont26": the language played's, with its Japanese, Korean and Chinese faces; "MenuFont64", "NumberFont", "PictureFont", "CinemaFont26") or the mod's own cooked font ("Luma/<id>/<path>", 3.8). `LUMA_OK` and `*out` (the same handle for the same name). A name neither: `LUMA_E_NOT_FOUND` (`Content.MissingFont` in the mod log); `LUMA_DRAW_FONTS_MOST` found: `LUMA_E_LIMIT` (reported).

### `texture`

```lumascript
draw.texture(name: text) -> draw.Texture?
```

```c
int32_t texture(const LumaMod* mod, const char* name, LumaDrawTexture* out);
```

A cooked texture by its path in the game's asset store: the mod's ("Luma/<id>/<path>", from its pack) or one of the game's (the path the importer wrote it to). It returns `LUMA_OK` and sets `*out`. Not found: `LUMA_E_NOT_FOUND` (`Content.MissingTexture` in the mod log); `LUMA_DRAW_TEXTURES_MOST` alive: `LUMA_E_LIMIT` (reported).

### `texture_from_pixels`

```lumascript
draw.texture_from_pixels(w: u32, h: u32, rgba8: bytes, flags: u32) -> draw.Texture?
```

```c
int32_t texture_from_pixels(const LumaMod* mod, uint32_t w, uint32_t h, const void* rgba8, uint32_t flags, LumaDrawTexture* out);
```

A texture w x h (1 to `LUMA_DRAW_TEXTURE_SIDE_MOST` each) from `rgba8` (w x h x 4 bytes, rows top first, RGBA, straight alpha), copied; `flags` `LUMA_DRAW_TEXTURE_`*. Its bytes count against `LUMA_DRAW_TEXTURE_BYTES_MOST`. It returns `LUMA_OK` and sets `*out`. A side of 0 or over the most: `LUMA_E_TOO_LARGE` (reported); past the textures' count or bytes: `LUMA_E_LIMIT` (reported).

### `texture_update`

```lumascript
draw.texture_update(texture: draw.Texture, x: u32, y: u32, w: u32, h: u32, rgba8: bytes, stride: u32)
```

```c
int32_t texture_update(const LumaMod* mod, LumaDrawTexture texture, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const void* rgba8, uint32_t stride);
```

Part of a texture from pixels replaced: the rectangle (x, y, w, h) in texels from `rgba8` (`stride` bytes a row, at least w x 4), seen from the next draw. A texture by name, or a rectangle outside it: `LUMA_E_ARGUMENT` (reported); not this mod's or freed: `LUMA_E_HANDLE` (reported).

### `texture_size`

```lumascript
draw.texture_size(texture: draw.Texture) -> (u32, u32)
```

```c
int32_t texture_size(const LumaMod* mod, LumaDrawTexture texture, uint32_t* w, uint32_t* h);
```

The texture's size in texels into `w` (null: not written) and `h` (null: not written). `LUMA_OK`; not this mod's or freed: `LUMA_E_HANDLE` (reported).

### `texture_free`

```lumascript
draw.texture_free(texture: draw.Texture)
```

```c
int32_t texture_free(const LumaMod* mod, LumaDrawTexture texture);
```

The texture freed (drawn this frame already: shown this frame still). `LUMA_OK`; not this mod's or freed: `LUMA_E_HANDLE` (reported).

### `line3`

```lumascript
draw.line3(a: draw.Vec3, b: draw.Vec3, thickness: f32, colour: color) -> bool
```

```c
int32_t line3(const LumaMod* mod, const LumaDrawVec3* a, const LumaDrawVec3* b, float thickness, LumaColor colour);
```

---- 3-D (the world points only, else `LUMA_E_STATE` (reported)): depth tested, unlit (mesh: its own materials in white light) ---- Arguments as the 2-D calls. 1.0. A line from a to b, `thickness` world units wide (above 0), facing the camera.

### `tri3`

```lumascript
draw.tri3(points: (draw.Vec3, draw.Vec3, draw.Vec3), colours: (color, color, color))
```

```c
int32_t tri3(const LumaMod* mod, const LumaDrawVec3 points[3], const LumaColor colours[3]);
```

A filled triangle, a color each corner, both sides.

### `mesh`

```lumascript
draw.mesh(model: text, matrix: (f32, f32, f32, f32, f32, f32, f32, f32, f32, f32, f32, f32), colour: color) -> bool
```

```c
int32_t mesh(const LumaMod* mod, const char* model, const float matrix[12], LumaColor colour);
```

A cooked model by name (the mod's "Luma/<id>/<path>", 3.2, or the game's object model name) at `matrix` (3 rows of 4: rotation and scale, then the place in the 4th column), times `colour`, in its own materials. Not found: `LUMA_E_NOT_FOUND` (`Content.MissingModel` in the mod log).

### `billboard`

```lumascript
draw.billboard(texture: draw.Texture, pos: draw.Vec3, w: f32, h: f32, colour: color)
```

```c
int32_t billboard(const LumaMod* mod, LumaDrawTexture texture, const LumaDrawVec3* pos, float w, float h, LumaColor colour);
```

`texture` on a w x h quad centerd at `pos`, facing the camera, times `colour`. A texture freed: the placeholder, `LUMA_E_HANDLE` (reported).

## Further reading

* [Calling services](calling-services.md)
* [Services reference](index.md)
* [About services](../../concepts/services.md)
* [Permissions](../../concepts/permissions.md)
