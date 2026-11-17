/* lt-frame: a mod with no imports whose start has a 64 KiB frame (loader contract 8: luma.h's own stack probe, no
   import table). The sum proves the whole buffer was used. */
#include "lt.h"

static int32_t start(const LumaCore* core, const LumaMod* mod) {
    lt_core = core;
    lt_mod = mod;
    volatile unsigned char big[65536];
    for (unsigned i = 0; i < sizeof(big); i++) big[i] = (unsigned char)(i % 251u);
    unsigned long long sum = 0;
    for (unsigned i = 0; i < sizeof(big); i++) sum += big[i];
    lt_fact("sum", (long long)sum);
    lt_fact("started", 1);
    return 0;
}

LUMA_MOD_NO_IMPORTS(start, 0);
