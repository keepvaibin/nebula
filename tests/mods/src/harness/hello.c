/* Harness test mod "hello": writes one fact when it starts (harness contract 4: a line "fact <key> <JSON>").
   Tests 1, 2, 7, 10, 13. Needs nothing but luma.h.

   It binds one function it never calls only because LUMA_MOD cannot be given an empty list in C (finding Z-8 of
   audits/0.2-nebula.md: a mod with no imports must be expressible). */
#include <luma.h>

static const LumaFunction* unused;

static int32_t start(const LumaCore* core, const LumaMod* mod) {
    core->log(mod, "fact hello 1");
    return 0;
}

static const LumaImport imports[] = {
    LUMA_FUNCTION(unused, "MR::getPlayerPos", "Vec3f *()"),
};

LUMA_MOD(imports, start, 0);
