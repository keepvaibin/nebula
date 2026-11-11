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
#elif defined(NO_IMPORTS)
LUMA_MOD_NO_IMPORTS(start, 0);
#elif defined(NO_EXPORT)
LUMA_EXPORT int not_luma_mod = 1;
#else
#error one variant
#endif

#endif
