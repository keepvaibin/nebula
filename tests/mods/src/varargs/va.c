/* F's 1.9 conformance mods (contract varargs 6), one source, CASE:
   1 va-call      calls MR::formatTextV with its own va_list (u"%d", 4247): facts count, first, last
   2 va-before    a before hook on MR::tryCreateCsvParser(const ResourceHolder *, const char *, ...): when the format
                  begins "%s", reads the name from a LUMA_VA_COPY of args[3]: facts seen, read (count of non-empty)
   3 va-after     an after hook on the same: refused at start (Hook.Variadic)
   4 cx-call      calls CubeGravity.cpp's constexpr encodeCorner(-1, 0, 1) by name: fact corner (21 in the source)
   5 cx-hook      a before hook on encodeCorner: refused at start (Hook.Constexpr)
   6 call-tell    imports MR::tryCreateCsvParser(const ResourceHolder *, const char *, ...) as a function: refused
                  (Lib.CallTell) */
#include "../loader/lt.h"

#define FORMAT_TEXT_V "int(char16_t *, size_t, const char16_t *, va_list)"
#define CSV "MR::tryCreateCsvParser"
#define CSV_SIG "Table *(const ResourceHolder *, const char *, ...)"
#define CORNER "Game/Gravity/CubeGravity.cpp#encodeCorner"
#define CORNER_SIG "s32(s32, s32, s32)"

#if CASE == 1
static const LumaFunction* format_text_v;
static const LumaImport imports[] = {LUMA_FUNCTION(format_text_v, "MR::formatTextV", FORMAT_TEXT_V)};

/* The caller's side of a va_list parameter: a pointer to the va_list as the function receives it (luma.h). */
static int format(unsigned short* dst, unsigned long long length, const unsigned short* fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    luma_va_arg received = ap;
    void* args[5] = {0, &dst, &length, &fmt, &received};
    int result = -1;
    format_text_v->invoke(args, &result);
    __builtin_va_end(ap);
    return result;
}

static void begin(void) {
    unsigned short text[32];
    static const unsigned short fmt[] = {'%', 'd', 0};
    const int count = format(text, 32, fmt, 4247);
    lt_fact("count", count);
    lt_fact("first", count > 0 ? text[0] : -1);
    lt_fact("last", count > 0 ? text[count - 1] : -1);
}
LT_START(begin())
#elif CASE == 2
static long long seen, read;

static int32_t before(LumaCall* call, void* user) {
    (void)user;
    seen++;
    const char* format = *(const char* const*)call->args[2];
    if (format != 0 && format[0] == '%' && format[1] == 's') {
        __builtin_va_list mine;
        LUMA_VA_COPY(mine, call->args[3]);
        const char* name = __builtin_va_arg(mine, const char*);
        __builtin_va_end(mine);
        if (name != 0 && name[0] != 0) {
            read++;
            lt_fact("read", read);
        }
    }
    lt_fact("seen", seen);
    return 0;
}

static const LumaImport imports[] = {LUMA_HOOK_ON(CSV, CSV_SIG, LUMA_BEFORE, before)};
LT_START((void)0)
#elif CASE == 3
static int32_t after(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    lt_fact("after_ran", 1);
    return 0;
}
static const LumaImport imports[] = {LUMA_HOOK_ON(CSV, CSV_SIG, LUMA_AFTER, after)};
LT_START((void)0)
#elif CASE == 4
static const LumaFunction* corner;
static const LumaImport imports[] = {LUMA_FUNCTION(corner, CORNER, CORNER_SIG)};
static void begin(void) {
    int x = -1, y = 0, z = 1;
    void* args[4] = {0, &x, &y, &z};
    int result = -100;
    corner->invoke(args, &result);
    lt_fact("corner", result);
}
LT_START(begin())
#elif CASE == 5
static int32_t before(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    lt_fact("hook_ran", 1);
    return 0;
}
static const LumaImport imports[] = {LUMA_HOOK_ON(CORNER, CORNER_SIG, LUMA_BEFORE, before)};
LT_START((void)0)
#elif CASE == 6
static const LumaFunction* csv;
static const LumaImport imports[] = {LUMA_FUNCTION(csv, CSV, CSV_SIG)};
LT_START((void)csv)
#else
#error CASE
#endif

LUMA_MOD(imports, start, 0);
