/* FB's draw.native conformance mod fn-native (plan 2.4 layer 9; native/include/luma_draw_native.h; permission
   native). Run steps call MR::getRandom(0, 2490 + k) and the hook gives 1.
   2491: k 1 backend ("backend_status", "backend_known": 1 when D3D12, Vulkan or D3D11), a null out ("null_out": E_ARGUMENT),
   point 99 ("bad_point": E_ARGUMENT), its function at PRESENT ("at"): each picture it clears the whole target green
   with the device's own commands (D3D12 ClearRenderTargetView; Vulkan vkCmdClearColorImage between barriers; D3D11
   ClearRenderTargetView on the immediate context).
   k 2 the function removed ("off"), then what the render thread saw ("calls" at least 1, "same_backend" 1 when every
   frame named backend's backend, "sized" 1 when every frame's size was the struct's, "at_present" 1 when every
   point was PRESENT, "width", "height" the last frame's). */
#include "../loader/lt.h"
#include <luma_draw.h>
#include <luma_draw_native.h>

#ifndef CASE
#error CASE
#endif

static const LumaDrawNativeService* native;
static uint32_t backend_now;
static long long calls, wrong_backend, wrong_size, wrong_point;
static uint32_t width, height;

/* ---- D3D12, through the command list's table (d3d12.h's ID3D12GraphicsCommandListVtbl: ClearRenderTargetView is
   its 49th entry, index 48). */
typedef struct D3dRect {
    long left, top, right, bottom;
} D3dRect;
typedef void (*ClearRtv)(void* list, uint64_t rtv, const float colour[4], uint32_t rects, const D3dRect* rect);

static void clear_d3d12(const LumaDrawNativeD3D12* d) {
    void** const table = *(void***)d->command_list;
    const float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    ((ClearRtv)table[48])(d->command_list, d->target_rtv, green, 0, 0);
}

/* ---- D3D11, through the immediate context's table (d3d11.h's ID3D11DeviceContextVtbl: ClearRenderTargetView is its
   51st entry, index 50). The call runs on a context state of its own, so nothing needs binding to clear. */
typedef void (*ClearView11)(void* context, void* rtv, const float colour[4]);

static void clear_d3d11(const LumaDrawNativeD3D11* d) {
    void** const table = *(void***)d->context;
    const float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    ((ClearView11)table[50])(d->context, d->target_rtv, green);
}

/* ---- Vulkan, through vkGetInstanceProcAddr (vulkan_core.h's values). */
typedef void* (*GetProc)(void* owner, const char* name);
typedef struct VkRange {
    uint32_t aspect, base_level, levels, base_layer, layers;
} VkRange;
typedef struct VkImageBarrier {
    uint32_t type; /* VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER 45 */
    const void* next;
    uint32_t src_access, dst_access, old_layout, new_layout, src_family, dst_family;
    uint64_t image;
    VkRange range;
} VkImageBarrier;
typedef void (*CmdBarrier)(void* cmd, uint32_t src_stages, uint32_t dst_stages, uint32_t flags, uint32_t memory_count,
                           const void* memory, uint32_t buffer_count, const void* buffers, uint32_t image_count,
                           const VkImageBarrier* images);
typedef void (*CmdClear)(void* cmd, uint64_t image, uint32_t layout, const float colour[4], uint32_t ranges,
                         const VkRange* range);
static CmdBarrier vk_barrier;
static CmdClear vk_clear;

static void clear_vulkan(const LumaDrawNativeVulkan* v) {
    if (vk_barrier == 0) {
        const GetProc instance_proc = (GetProc)v->get_instance_proc_addr;
        const GetProc device_proc = (GetProc)instance_proc(v->instance, "vkGetDeviceProcAddr");
        if (device_proc == 0) return;
        vk_clear = (CmdClear)device_proc(v->device, "vkCmdClearColorImage");
        vk_barrier = (CmdBarrier)device_proc(v->device, "vkCmdPipelineBarrier");
        if (vk_barrier == 0 || vk_clear == 0) return;
    }
    enum { COLOR_ATTACHMENT_OPTIMAL = 2, TRANSFER_DST_OPTIMAL = 7, COLOR_ATTACHMENT_WRITE = 0x100, TRANSFER_WRITE = 0x1000,
           COLOR_ATTACHMENT_OUTPUT = 0x400, TRANSFER = 0x1000, ASPECT_COLOR = 1 };
    const VkRange range = {ASPECT_COLOR, 0, 1, 0, 1};
    VkImageBarrier to = {45, 0, COLOR_ATTACHMENT_WRITE, TRANSFER_WRITE, COLOR_ATTACHMENT_OPTIMAL, TRANSFER_DST_OPTIMAL,
                         ~0u, ~0u, v->target_image, range};
    vk_barrier(v->command_buffer, COLOR_ATTACHMENT_OUTPUT, TRANSFER, 0, 0, 0, 0, 0, 1, &to);
    const float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    vk_clear(v->command_buffer, v->target_image, TRANSFER_DST_OPTIMAL, green, 1, &range);
    VkImageBarrier back = {45, 0, TRANSFER_WRITE, COLOR_ATTACHMENT_WRITE, TRANSFER_DST_OPTIMAL, COLOR_ATTACHMENT_OPTIMAL,
                           ~0u, ~0u, v->target_image, range};
    vk_barrier(v->command_buffer, TRANSFER, COLOR_ATTACHMENT_OUTPUT, 0, 0, 0, 0, 0, 1, &back);
}

/* The render thread: counts go to the game thread through atomics, read at k 2. */
static void paint(void* user, const LumaDrawNativeFrame* frame) {
    (void)user;
    __atomic_add_fetch(&calls, 1, __ATOMIC_RELEASE);
    if (frame->size != sizeof(LumaDrawNativeFrame)) __atomic_add_fetch(&wrong_size, 1, __ATOMIC_RELEASE);
    if (frame->backend != __atomic_load_n(&backend_now, __ATOMIC_ACQUIRE)) __atomic_add_fetch(&wrong_backend, 1, __ATOMIC_RELEASE);
    if (frame->point != LUMA_DRAW_PRESENT) __atomic_add_fetch(&wrong_point, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&width, frame->width, __ATOMIC_RELEASE);
    __atomic_store_n(&height, frame->height, __ATOMIC_RELEASE);
    if (frame->backend == LUMA_DRAW_NATIVE_D3D12) clear_d3d12(&frame->d3d12);
    if (frame->backend == LUMA_DRAW_NATIVE_VULKAN) clear_vulkan(&frame->vulkan);
    if (frame->backend == LUMA_DRAW_NATIVE_D3D11) clear_d3d11(&frame->d3d11);
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 2490;
    if (k < 1 || k > 2) return 0;
    if (k == 1) {
        uint32_t b = 0;
        lt_fact("backend_status", native->backend(lt_mod, &b));
        lt_fact("backend_known", b == LUMA_DRAW_NATIVE_D3D12 || b == LUMA_DRAW_NATIVE_VULKAN || b == LUMA_DRAW_NATIVE_D3D11);
        __atomic_store_n(&backend_now, b, __ATOMIC_RELEASE);
        lt_fact("null_out", native->backend(lt_mod, 0));
        lt_fact("bad_point", native->at(lt_mod, (LumaDrawPoint)99, paint, 0));
        lt_fact("at", native->at(lt_mod, LUMA_DRAW_PRESENT, paint, 0));
    }
    if (k == 2) {
        lt_fact("off", native->at(lt_mod, LUMA_DRAW_PRESENT, 0, 0));
        lt_fact("calls", __atomic_load_n(&calls, __ATOMIC_ACQUIRE));
        lt_fact("same_backend", __atomic_load_n(&wrong_backend, __ATOMIC_ACQUIRE) == 0);
        lt_fact("sized", __atomic_load_n(&wrong_size, __ATOMIC_ACQUIRE) == 0);
        lt_fact("at_present", __atomic_load_n(&wrong_point, __ATOMIC_ACQUIRE) == 0);
        lt_fact("width", __atomic_load_n(&width, __ATOMIC_ACQUIRE));
        lt_fact("height", __atomic_load_n(&height, __ATOMIC_ACQUIRE));
    }
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(native, LUMA_DRAW_NATIVE_NAME, "1.0"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
