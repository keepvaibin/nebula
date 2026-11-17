/* lt-mem: memory for mods (loader contract 5). Blocks at their alignment up to 65536, whole and apart; a size nobody
   has gives null (the mod says so), never a crash; free of null does nothing; from a hook as from start. */
#include "lt.h"

static int fill_and_check(unsigned char* p, unsigned long long n, unsigned char v) {
    for (unsigned long long i = 0; i < n; i++) p[i] = v;
    for (unsigned long long i = 0; i < n; i++)
        if (p[i] != v) return 0;
    return 1;
}

static int32_t in_hook(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) != 4246) return 0;
    void* p = lt_core->alloc(lt_mod, 4096, 64);
    const int ok = p != 0 && ((unsigned long long)p & 63u) == 0 && fill_and_check((unsigned char*)p, 4096, 0x5a);
    lt_core->free(lt_mod, p);
    LUMA_RESULT(call, int32_t) = ok;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, in_hook),
};

LT_START({
    unsigned char* a = (unsigned char*)core->alloc(mod, 100, 16);
    unsigned char* b = (unsigned char*)core->alloc(mod, 65536, 65536);
    unsigned char* c = (unsigned char*)core->alloc(mod, 1, 1);
    lt_fact("a16", a != 0 && ((unsigned long long)a & 15u) == 0);
    lt_fact("b64k", b != 0 && ((unsigned long long)b & 65535u) == 0);
    lt_fact("c", c != 0);
    int whole = a != 0 && b != 0 && c != 0 && fill_and_check(a, 100, 1) && fill_and_check(b, 65536, 2) && fill_and_check(c, 1, 3);
    whole = whole && a[0] == 1 && a[99] == 1 && b[0] == 2 && b[65535] == 2 && c[0] == 3; /* none wrote over another */
    lt_fact("apart", whole);
    void* huge = core->alloc(mod, 1ull << 46, 16);
    lt_fact("huge_null", huge == 0);
    core->free(mod, 0);
    core->free(mod, a);
    core->free(mod, c);
    /* b is kept: the game frees what a mod still holds when it is taken out (loader contract 5). Not proven by
       1113: no step takes a mod out; that is 1.1 gate work with 1.11's reload step (run 16, R16-4). */
})
LUMA_MOD(imports, start, 0);
