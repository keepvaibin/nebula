/* J's mod-log conformance mods (contract mod-log 8): what every one shares. Facts are log lines "fact <key> <JSON>"
   (harness contract 4); no C library, so numbers are written here. Same shape as F's lt.h. */
#ifndef JL_H
#define JL_H
#include <luma.h>

static const LumaCore* jl_core;
static const LumaMod* jl_mod;

__attribute__((unused)) static char* jl_put(char* at, const char* s) {
    while (*s != 0) *at++ = *s++;
    return at;
}

__attribute__((unused)) static char* jl_num(char* at, long long v) {
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
__attribute__((unused)) static void jl_fact(const char* key, long long v) {
    char line[160];
    char* at = jl_put(line, "fact ");
    at = jl_put(at, key);
    *at++ = ' ';
    at = jl_num(at, v);
    *at = 0;
    jl_core->log(jl_mod, line);
}

/* One report of script code 901 (Script.S0901, "{0} was called off the game thread ...") at `file`:`line`. */
__attribute__((unused)) static void jl_report(const char* file, uint32_t line) {
    const char* args[1] = {"jl.example"};
    jl_core->report(jl_mod, file, line, 901, args, 1);
}

#define JL_START(body)                                                 \
    static int32_t start(const LumaCore* core, const LumaMod* mod) {   \
        jl_core = core;                                                \
        jl_mod = mod;                                                  \
        body;                                                          \
        jl_fact("started", 1);                                         \
        return 0;                                                      \
    }

#endif
