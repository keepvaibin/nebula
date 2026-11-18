/* F's 1.1 conformance mods (loader contract 11): what every one of them shares. Facts are log lines
   "fact <key> <JSON>" (harness contract 4); no C library, so numbers are written here. */
#ifndef LT_H
#define LT_H
#include <luma.h>

static const LumaCore* lt_core;
static const LumaMod* lt_mod;

__attribute__((unused)) static char* lt_put(char* at, const char* s) {
    while (*s != 0) *at++ = *s++;
    return at;
}

__attribute__((unused)) static char* lt_num(char* at, long long v) {
    char digits[24];
    int n = 0;
    unsigned long long u = v < 0 ? 0ull - (unsigned long long)v : (unsigned long long)v;
    do {
        digits[n++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u != 0);
    if (v < 0) *at++ = '-';
    while (n > 0) *at++ = digits[--n];
    return at;
}

/* fact <key> <v> */
__attribute__((unused)) static void lt_fact(const char* key, long long v) {
    char line[160];
    char* at = lt_put(line, "fact ");
    at = lt_put(at, key);
    *at++ = ' ';
    at = lt_num(at, v);
    *at = 0;
    lt_core->log(lt_mod, line);
}

__attribute__((unused)) static void lt_say(const char* text) {
    lt_core->log(lt_mod, text);
}

/* The test extension's "harness" service (luma_harness.h): optional, since the game's catalog leaves it out (a test
   run's extension gives it; tools/lumacore/check_services.py). */
#define LT_HARNESS(slot, major_minor) {LUMA_IMPORT_SERVICE, LUMA_OPTIONAL, "harness", major_minor, &slot, 0, 0, 0}

#define LT_START(body)                                                 \
    static int32_t start(const LumaCore* core, const LumaMod* mod) {   \
        lt_core = core;                                                \
        lt_mod = mod;                                                  \
        body;                                                          \
        lt_fact("started", 1);                                         \
        return 0;                                                      \
    }

#endif
