/* 2117-library-acts (services-registry: a library acting for the mods that call it). A service judges the table the
   call goes through: the permission is the binding mod's, asked in its own luma.json and granted to it; the LumaMod a
   call names is whose the handle is (it ends with that mod). So a library with net.local serves a caller that has no
   permission of its own, and chooses whose each socket is.
   CASE 1, ka-lib: a library granted net.local. Exports "ka.open_for" (a udp_open naming the given mod) and
   "ka.open_self" (naming itself). Run steps MR::getRandom(0, 4270): "for_not_lib" (close of the caller's socket naming
   the library: LUMA_E_HANDLE, it is not the library's); MR::getRandom(0, 4271) after the caller is taken out:
   "self_after" (close of its own socket naming itself: LUMA_OK, it outlived the caller).
   CASE 2, ka-user: needs ka-lib, asks no permission; at start "for" (ka.open_for with itself) and "self"
   (ka.open_self). */
#include "../loader/lt.h"
#include <luma_net.h>

#ifndef CASE
#error CASE
#endif

#define SIG_FOR "lumi1:00000000000000c1"
#define SIG_SELF "lumi1:00000000000000c2"

#if CASE == 1
static const LumaNetLocalService* net;
static LumaHandle h_for;
static LumaHandle h_self;

static void on_net(void* user, const LumaEvent* event) {
    (void)user;
    (void)event;
}

static int32_t open_for(const LumaMod* caller) {
    return net->udp_open(caller, 0, 0, on_net, 0, &h_for);
}

static int32_t open_self(void) {
    return net->udp_open(lt_mod, 0, 0, on_net, 0, &h_self);
}

static void dependent_gone(const LumaMod* dependent) {
    (void)dependent;
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t);
    if (k == 4270) {
        lt_fact("for_not_lib", net->close(lt_mod, h_for));
    } else if (k == 4271) {
        lt_fact("self_after", net->close(lt_mod, h_self));
    } else {
        return 0;
    }
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaExport exports[] = {
    LUMA_EXPORT_FUNCTION("ka.open_for", SIG_FOR, open_for),
    LUMA_EXPORT_FUNCTION("ka.open_self", SIG_SELF, open_self),
};

static const LumaImport imports[] = {
    LUMA_SERVICE(net, LUMA_NET_LOCAL_NAME, "1.1"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_LIBRARY_MOD(imports, start, 0, exports, dependent_gone);
#else
static int32_t (*open_for)(const LumaMod* caller);
static int32_t (*open_self)(void);

static const LumaImport imports[] = {
    LUMA_MOD_FUNCTION(open_for, "ka-lib/ka.open_for", SIG_FOR),
    LUMA_MOD_FUNCTION(open_self, "ka-lib/ka.open_self", SIG_SELF),
};

LT_START({
    lt_fact("for", open_for(lt_mod));
    lt_fact("self", open_self());
})
LUMA_MOD(imports, start, 0);
#endif
