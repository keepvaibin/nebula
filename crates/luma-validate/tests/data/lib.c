/* The libraries tests/libraries.rs reads: one source, one variant per -D, built by build.sh for each format the
   validator reads. Built freestanding, as a mod's library is. */
#include <luma.h>

/* Not every variant uses both. */
__attribute__((unused)) static const LumaFunction* get_pos;

__attribute__((unused)) static int32_t on_update(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    return 0;
}

#if defined(EXTRA)
/* An extra library (code.libraries): a library of the mod's own the entry library loads. */
LUMA_EXPORT int extra_value(void) {
    return 7;
}
#elif defined(HELPER)
/* An extra library that itself needs another (L510 lists what it needs). */
int extra_value(void);
LUMA_EXPORT int helper_value(void) {
    return extra_value() + 1;
}
#elif defined(PROGRAM)
/* A program, not a library. */
int entry(void) {
    return 0;
}
#else

#if defined(USES_EXTRA)
int extra_value(void);
#if defined(EXTRA_BY_POINTER)
/* Called through a pointer the loader binds: a Mach-O call to another library otherwise goes through a lazy stub,
   which needs the system's dyld_stub_binder. */
static int (*volatile extra_at)(void) = extra_value;
#define extra_value() extra_at()
#endif
#endif

__attribute__((unused)) static int32_t start(const LumaCore* core, const LumaMod* mod) {
    (void)core;
    (void)mod;
#if defined(USES_EXTRA)
    return extra_value() == 7 ? 0 : 1;
#else
    return 0;
#endif
}

#if defined(GOOD) || defined(USES_EXTRA)
static LumaImport imports[] = {
    LUMA_FUNCTION(get_pos, "MR::getPlayerPos", "Vec3f *()"),
    LUMA_HOOK_ON("GameSystem::update", "void()", LUMA_AFTER, on_update),
};
LUMA_MOD(imports, start, 0);
#elif defined(MISSING)
static LumaImport imports[] = {
    LUMA_FUNCTION(get_pos, "MR::noSuchFunction", "void()"),
};
LUMA_MOD(imports, start, 0);
#elif defined(WRONG_SIGNATURE)
static LumaImport imports[] = {
    LUMA_FUNCTION(get_pos, "MR::getPlayerPos", "int()"),
};
LUMA_MOD(imports, start, 0);
#elif defined(NO_SIGNATURE)
/* A function named without its signature: it binds only while the game has one function of that name. */
static LumaImport imports[] = {
    LUMA_FUNCTION(get_pos, "MR::getPlayerPos", 0),
};
LUMA_MOD(imports, start, 0);
#elif defined(OPTIONAL_MISSING)
static LumaImport imports[] = {
    {LUMA_IMPORT_FUNCTION, LUMA_OPTIONAL, "MR::noSuchFunction", "void()", &get_pos, 0, 0, 0},
};
LUMA_MOD(imports, start, 0);
#elif defined(CONSTRUCTOR_WRAP)
static LumaImport imports[] = {
    LUMA_HOOK_ON("LiveActor::LiveActor", "(const char *)", LUMA_WRAP, on_update),
};
LUMA_MOD(imports, start, 0);
#elif defined(CONSTRUCTOR_AFTER)
static LumaImport imports[] = {
    LUMA_HOOK_ON("LiveActor::LiveActor", "(const char *)", LUMA_AFTER, on_update),
};
LUMA_MOD(imports, start, 0);
#elif defined(REPLACE)
static LumaImport imports[] = {
    LUMA_HOOK_ON("GameSystem::update", "void()", LUMA_REPLACE, on_update),
};
LUMA_MOD(imports, start, 0);
#elif defined(BAD_ROW)
static LumaImport imports[] = {
    {9, 0, "MR::getPlayerPos", "Vec3f *()", &get_pos, 0, 0, 0},
};
LUMA_MOD(imports, start, 0);
#elif defined(NEWER)
static LumaImport imports[] = {
    LUMA_FUNCTION(get_pos, "MR::getPlayerPos", "Vec3f *()"),
};
LUMA_FLOATS
LUMA_EXPORT const LumaModInfo luma_mod = {sizeof(LumaModInfo), LUMA_API + 1, imports, 1, start, 0};
#elif defined(SHORT_INFO)
/* A luma_mod that was not made by luma.h: shorter than LumaModInfo. */
LUMA_EXPORT const uint32_t luma_mod[2] = {8, LUMA_API};
#elif defined(SERVICE)
/* Loader D3-2: a service by name and "major.minor". */
static LumaImport imports[] = {
    {5, 0, "net", "1.0", &get_pos, 0, 0, 0},
};
LUMA_MOD(imports, start, 0);
#elif defined(SERVICE_BAD)
static LumaImport imports[] = {
    {5, 0, "net", "one", &get_pos, 0, 0, 0},
};
LUMA_MOD(imports, start, 0);
#elif defined(SERVICE_MORE)
/* Registry 2's grammar (L505 whatever the flags), an optional import the game lacks (nothing), an optional import of
   a permissioned service the mod does not declare (L506, and not a required service). */
static LumaImport imports[] = {
    {5, 0, "Net", "1.0", &get_pos, 0, 0, 0},
    {5, 0, "net..x", "1.0", &get_pos, 0, 0, 0},
    {5, 0, "net", "01.0", &get_pos, 0, 0, 0},
    {5, 0, "net", "1.70000", &get_pos, 0, 0, 0},
    {5, LUMA_OPTIONAL, "fs", "1.0", &get_pos, 0, 0, 0},
    {5, LUMA_OPTIONAL, "net.local", "1.0", &get_pos, 0, 0, 0},
};
LUMA_MOD(imports, start, 0);
#elif defined(MOD_FUNCTION)
/* Loader D3-3: another mod's exported function, "<mod id>/<export name>". */
static LumaImport imports[] = {
    {6, 0, "lumakit/hud.meter", "lumi1:0123456789abcdef", &get_pos, 0, 0, 0},
};
LUMA_MOD(imports, start, 0);
#elif defined(MOD_FUNCTION_BAD)
static LumaImport imports[] = {
    {6, 0, "hud.meter", "lumi1:0123456789abcdef", &get_pos, 0, 0, 0},
};
LUMA_MOD(imports, start, 0);
#elif defined(METHOD)
/* Objects 10: a virtual call (kind 8) by the declaring class's name, and a class's number (kind 3) of any record. */
static LumaImport imports[] = {
    {8, 0, "LiveActor::calcAndSetBaseMtx", "void()", &get_pos, 0, 0, 0},
    {3, 0, "LiveActor", 0, &get_pos, 0, 0, 0},
};
LUMA_MOD(imports, start, 0);
#elif defined(METHOD_BAD)
/* Not virtuals: a plain function, a virtual with another signature, one with no row; and no such record. */
static LumaImport imports[] = {
    {8, 0, "MR::getPlayerPos", "Vec3f *()", &get_pos, 0, 0, 0},
    {8, 0, "LiveActor::calcAndSetBaseMtx", "int()", &get_pos, 0, 0, 0},
    {8, 0, "LiveActor::noRow", "void()", &get_pos, 0, 0, 0},
    {8, LUMA_OPTIONAL, "LiveActor::notThere", "void()", &get_pos, 0, 0, 0},
    {3, 0, "NoSuchClass", 0, &get_pos, 0, 0, 0},
};
LUMA_MOD(imports, start, 0);
#elif defined(NO_IMPORTS)
LUMA_MOD_NO_IMPORTS(start, 0);
#elif defined(NO_EXPORT)
LUMA_EXPORT int not_luma_mod = 1;
#else
#error one variant
#endif

#endif

#if defined(RUNS_AT_LOAD)
/* Code the system's loader runs when it loads the library, before LumaCore's `begin` (loader contract D2-6): a
   constructor (ELF .init_array, Mach-O __mod_init_func) and, on Windows, an entry point. */
static volatile int loaded;
__attribute__((constructor)) static void at_load(void) {
    loaded = 1;
}
#if defined(_WIN32)
int __stdcall dll_entry(void* module, unsigned long reason, void* reserved) {
    (void)module;
    (void)reason;
    (void)reserved;
    loaded = 2;
    return 1;
}
#endif
#endif

#if defined(RUNS_AT_CLOSE) || defined(FINI_FUNCTION)
/* Code the system runs when it closes the library, after LumaCore has freed the mod's memory (F's audit of R3-A,
   A-1): a destructor the compiler does not register with atexit (ELF .fini_array, Mach-O __mod_term_func), or the
   function the linker's -fini names (ELF DT_FINI). */
static volatile int closed;
#if defined(RUNS_AT_CLOSE) && defined(__APPLE__)
/* Apple's clang always registers a destructor attribute's function with __cxa_atexit (luma.h's exit list), so the
   term section is written as an assembler or another compiler would. */
static void at_close(void) {
    closed = 1;
}
__attribute__((used, section("__DATA,__mod_term_func,mod_term_funcs"))) static void (*const at_close_entry)(void) = at_close;
#elif defined(RUNS_AT_CLOSE)
__attribute__((destructor)) static void at_close(void) {
    closed = 1;
}
#else
void fini_function(void);
void fini_function(void) {
    closed = 2;
}
#endif
#endif

#if defined(TLS_DATA) || defined(TLS_CALLBACK)
/* A TLS directory as the C library's tlssup gives one (these are built without it). Thread-local data alone runs
   nothing and loads; a callback runs at load and close and is refused (A-3). */
typedef void (*TlsCallback)(void* module, unsigned long reason, void* reserved);
static unsigned long tls_index;
static char tls_block[8];
#if defined(TLS_CALLBACK)
static void at_tls(void* module, unsigned long reason, void* reserved) {
    (void)module;
    (void)reason;
    (void)reserved;
    tls_block[0] = 1;
}
static TlsCallback tls_callbacks[] = {at_tls, 0};
#else
static TlsCallback tls_callbacks[] = {0};
#endif
typedef struct {
    void* start;
    void* end;
    void* index;
    TlsCallback* callbacks;
    unsigned zero_fill;
    unsigned characteristics;
} TlsDirectory;
__attribute__((used)) const TlsDirectory _tls_used = {tls_block, tls_block + 8, &tls_index, tls_callbacks, 0, 0};
#endif
