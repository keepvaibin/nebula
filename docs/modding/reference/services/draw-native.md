---
title: draw.native service
shortTitle: draw-native
intro: Reach the graphics device directly from a C mod.
contentType: reference
---

## About the draw.native service

The `draw.native` service gives a C mod direct access to the graphics device. Scripts cannot call it. It needs the `native` permission.

| Service | Version | Permission | C import | LumaScript |
|---|---|---|---|---|
| `draw.native` | 1.0 | `native` | `LUMA_SERVICE(slot, "draw.native", "1.0")` | `use svc.draw.native` |

The header is `native/include/luma_draw_native.h`. In LumaScript, a service is called by the last part of its name, such as `native.` for `draw.native`. Each function below shows its LumaScript form first and its C form second. For how the forms are made, see [Calling services](calling-services.md).

### Details from the header

LumaCore's draw.native service: a mod's own GPU commands at a draw point, with the device's own handles. Import: `LUMA_SERVICE(slot, "draw.native", "1.0")`; permission `native`: a mod the player did not grant it cannot bind the service. A mod's function runs on the host's render thread when the host reaches the point in the frame it draws, once for each picture it draws from the game's frame (interpolated pictures too); never on the game thread, so the mod synchronizes what that function shares with its game-thread code itself. The frame and every handle in it are valid only during the call. The function leaves the target and depth in the states and layouts given, no render pass open, nothing submitted on the host's queue; the host sets its own state again after it. No guard runs there (the `native` permission's consent says the mod has full access): the game skips the function of a stopped mod and waits for one running to return before the mod ends.

## Limits and constants

| Name | Value | Description |
|---|---|---|
| `LUMA_DRAW_NATIVE_MOST` | `16` | Functions all mods set at once. |

## Types

| Name | Declaration | Description |
|---|---|---|
| `LumaDrawNativeFn` | `typedef void (*LumaDrawNativeFn)(void* user, const LumaDrawNativeFrame* frame);` |  |

## Enumerations

### `LumaDrawNativeBackend`

| Name | Description |
|---|---|
| `LUMA_DRAW_NATIVE_D3D12 = 1` |  |
| `LUMA_DRAW_NATIVE_VULKAN = 2` |  |
| `LUMA_DRAW_NATIVE_D3D11 = 3` |  |

## Structures

### `LumaDrawNativeD3D12`

D3D12's objects (the pointers are the COM interfaces named).

| Name | Description |
|---|---|
| `void* device` | ID3D12Device*. |
| `void* queue` | ID3D12CommandQueue*: the host's; nothing submitted on it. |
| `void* command_list` | ID3D12GraphicsCommandList*: open, recording the frame. |
| `void* target` | ID3D12Resource*, in D3D12_RESOURCE_STATE_RENDER_TARGET. |
| `uint64_t target_rtv` | D3D12_CPU_DESCRIPTOR_HANDLE.ptr. |
| `uint32_t target_format` | DXGI_FORMAT. |
| `uint32_t depth_format` | DXGI_FORMAT; 0 at the present point. |
| `void* depth` | ID3D12Resource*, D3D12_RESOURCE_STATE_DEPTH_WRITE; null at the present point. |
| `uint64_t depth_dsv` | D3D12_CPU_DESCRIPTOR_HANDLE.ptr; 0 at the present point. |

### `LumaDrawNativeVulkan`

Vulkan's objects (handles as their Vk types: dispatchable as pointers, non-dispatchable as uint64_t).

| Name | Description |
|---|---|
| `void* instance` | VkInstance. |
| `void* physical_device` | VkPhysicalDevice. |
| `void* device` | VkDevice. |
| `void* queue` | VkQueue: the host's; nothing submitted on it. |
| `void* get_instance_proc_addr` | PFN_vkGetInstanceProcAddr. |
| `void* command_buffer` | VkCommandBuffer: recording, outside any render pass. |
| `uint32_t queue_family` |  |
| `uint32_t target_format` | VkFormat. |
| `uint64_t target_image` | VkImage. |
| `uint64_t target_view` | VkImageView. |
| `uint32_t target_layout` | VkImageLayout: COLOR_ATTACHMENT_OPTIMAL. |
| `uint32_t depth_format` | VkFormat; 0 at the present point. |
| `uint64_t depth_image` | VkImage; 0 at the present point. |
| `uint64_t depth_view` | VkImageView; 0 at the present point. |
| `uint32_t depth_layout` | VkImageLayout: DEPTH_STENCIL_ATTACHMENT_OPTIMAL; 0 at the present point. |
| `uint32_t reserved` |  |

### `LumaDrawNativeD3D11`

Direct3D 11's objects (the pointers are the COM interfaces named). The function runs on a context state of its own (ID3D11DeviceContext1::SwapDeviceContextState): it starts with nothing bound and binds what it draws with; the host's own state comes back after it.

| Name | Description |
|---|---|
| `void* device` | ID3D11Device1*. |
| `void* context` | ID3D11DeviceContext1*: the immediate context; the host's thread only, during the call. |
| `void* target` | ID3D11Texture2D*. |
| `void* target_rtv` | ID3D11RenderTargetView*. |
| `uint32_t target_format` | DXGI_FORMAT. |
| `uint32_t depth_format` | DXGI_FORMAT; 0 at the present point. |
| `void* depth` | ID3D11Texture2D*; null at the present point. |
| `void* depth_dsv` | ID3D11DepthStencilView*; null at the present point. |

### `LumaDrawNativeFrame`

The frame a function draws into (nebula_frame_list.h's NebulaFrame is this struct).

| Name | Description |
|---|---|
| `uint32_t size` | Sizeof(`LumaDrawNativeFrame`). |
| `uint32_t backend` | `LumaDrawNativeBackend`. |
| `uint32_t point` | `LumaDrawPoint`: WORLD_BEFORE, WORLD_OPAQUE_AFTER, WORLD_AFTER or PRESENT. |
| `uint32_t game_frame` | The game frame that made the frame list. |
| `uint32_t picture` | 0, 1,...: the pictures the host draws from one game frame. |
| `uint32_t width` | The target's pixels. |
| `uint32_t height` |  |
| `uint32_t samples` | The target's and depth's samples: 1, 2, 4 or 8 (the scene's may be multisampled). |
| `float view[16]` | The scene camera's eye from world (the game's 3x4 camera, then 0 0 0 1), rows of four; zero at PRESENT. |
| `float projection[16]` | Clip from eye as the host draws the scene into this target, in the backend's own clip convention (its depth range and direction, its jitter this picture), rows of four: a vertex drawn with projection x view meets the scene's depth exactly; zero at PRESENT. |
| `LumaDrawNativeD3D12 d3d12` | Backend D3D12. |
| `LumaDrawNativeVulkan vulkan` | Backend VULKAN. |
| `LumaDrawNativeD3D11 d3d11` | Backend D3D11. |

## `draw.native` functions

In C, each function takes your mod (`const LumaMod* mod`) first. LumaScript passes it for you.

### `at`

```c
int32_t at(const LumaMod* mod, LumaDrawPoint point, LumaDrawNativeFn fn, void* user);
```

C only. Scripts cannot call this function.

Call it on the game thread. `fn(user, frame)` runs at `point` from the next frame on (null: removed). Points `LUMA_DRAW_WORLD_BEFORE`, WORLD_OPAQUE_AFTER and WORLD_AFTER draw into the scene's target with its depth, PRESENT into the picture shown (no depth). `LUMA_OK`. Another point: `LUMA_E_ARGUMENT` (reported); `LUMA_DRAW_NATIVE_MOST` set by all mods: `LUMA_E_LIMIT` (reported). One function a point a mod: a second replaces the first.

### `backend`

```c
int32_t backend(const LumaMod* mod, uint32_t* out);
```

C only. Scripts cannot call this function.

Call it on the game thread. The host's backend (`LumaDrawNativeBackend`), the same all session. It returns `LUMA_OK` and sets `*out`; a null out: `LUMA_E_ARGUMENT` (reported).

## Further reading

* [Calling services](calling-services.md)
* [Services reference](index.md)
* [About services](../../concepts/services.md)
* [Permissions](../../concepts/permissions.md)
