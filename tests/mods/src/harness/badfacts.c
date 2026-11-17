/* Harness test mod "badfacts" (harness revision 6, R6-1 and G-H6-1): logs one good fact and two the harness must
   refuse with harness.bad_fact: `loaded`, which is the game half's own (mod.<id>.loaded), and a key with a dot,
   which would make mod.<id>.<key> read two ways. Test 18. Needs nothing but luma.h. */
#include <luma.h>

static const LumaFunction* unused;

static int32_t start(const LumaCore* core, const LumaMod* mod) {
    core->log(mod, "fact good_key 7");
    core->log(mod, "fact loaded 1");
    core->log(mod, "fact a.b 1");
    return 0;
}

static const LumaImport imports[] = {
    LUMA_FUNCTION(unused, "MR::getPlayerPos", "Vec3f *()"),
};

LUMA_MOD(imports, start, 0);
