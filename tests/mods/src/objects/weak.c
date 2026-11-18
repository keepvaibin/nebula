/* weak-a and weak-b: contract objects, weak references (LumaCore.weak and weak_get, N-4003), both mods the same code.
   MR::getRandom(k, 4003) runs step k on the game thread and answers 1:
   - 1: a handle for the player: nonzero, the same twice and for both mods (each says it; the test compares), and it
        reads the player as its NameObj; null, an object whose vtable is unmapped and an unmapped object give 0; handle
        0 and handles weak never gave read null; the kernel hooks neither NameObj::~NameObj nor NebulaArena::free for
        it (M-132: lumart's refs did);
   - 2 (weak-a taken out before): the handle still reads the player;
   - 3 (a new scene before): the handle reads null; the new player gets a new handle, which reads it. */
#include "../loader/lt.h"

static const LumaFunction* player;
static int32_t c_nameobj;
static uint64_t handle;

static const void* the_player(void) {
    void* args[1] = {0};
    void* got = 0;
    player->invoke(args, &got);
    return got;
}

static int32_t hooked(const char* name, const char* signature) {
    const LumaFunction* const fn = lt_core->function(name, signature);
    return fn == 0 ? -1 : (fn->hooked != 0 && *fn->hooked != 0);
}

static void made(void) {
    const void* const p = the_player();
    lt_fact("player", p != 0);
    if (p == 0) {
        return;
    }
    const void* const as_nameobj = lt_core->as_class(lt_mod, p, c_nameobj);
    handle = lt_core->weak(lt_mod, p);
    lt_fact("handle", (long long)handle);
    lt_fact("handle_some", handle != 0);
    lt_fact("reads", handle != 0 && lt_core->weak_get(handle) == as_nameobj);
    lt_fact("same_again", lt_core->weak(lt_mod, p) == handle);
    lt_fact("same_as_nameobj", lt_core->weak(lt_mod, as_nameobj) == handle);
    /* Hostile objects: the kernel reads them through the guard's probe (as 1306), and refers to none. */
    static const void* hollow[1];
    hollow[0] = (const void*)(uintptr_t)0x1000;
    lt_fact("null_none", lt_core->weak(lt_mod, 0) == 0);
    lt_fact("hollow_none", lt_core->weak(lt_mod, hollow) == 0);
    lt_fact("nowhere_none", lt_core->weak(lt_mod, (const void*)(uintptr_t)0x2000) == 0);
    /* Handles weak never gave. */
    int32_t read = 0;
    read += lt_core->weak_get(0) != 0;
    read += lt_core->weak_get(0xffffffffull) != 0;
    read += lt_core->weak_get(~0ull) != 0;
    read += lt_core->weak_get(handle ^ (1ull << 32)) != 0;
    read += lt_core->weak_get(handle + 1) != 0;
    read += lt_core->weak_get(handle & 0xffffffff00000000ull) != 0;
    lt_fact("bogus_read", read);
    lt_fact("dtor_hooked", hooked("NameObj::~NameObj", 0));
    lt_fact("free_hooked", hooked("NebulaArena::free", "void(void *)"));
}

static void still(void) {
    const void* const p = the_player();
    lt_fact("still_reads", p != 0 && lt_core->weak_get(handle) == lt_core->as_class(lt_mod, p, c_nameobj));
}

static void after_scene(void) {
    lt_fact("old_null", lt_core->weak_get(handle) == 0);
    const void* const p = the_player();
    const uint64_t again = p != 0 ? lt_core->weak(lt_mod, p) : 0;
    lt_fact("new_some", again != 0);
    lt_fact("new_differs", again != handle);
    lt_fact("new_reads", again != 0 && lt_core->weak_get(again) == lt_core->as_class(lt_mod, p, c_nameobj));
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) != 4003) {
        return 0;
    }
    switch (LUMA_ARG(call, 1, int32_t)) {
    case 1: made(); break;
    case 2: still(); break;
    case 3: after_scene(); break;
    default: break;
    }
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_FUNCTION(player, "MR::getPlayerDemoActor", "LiveActor *()"),
    LUMA_CLASS(c_nameobj, "NameObj"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START((void)0)
LUMA_MOD(imports, start, 0);
