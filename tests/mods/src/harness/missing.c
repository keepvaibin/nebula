/* Harness test mod "missing": asks for one function that is not a row of the game, and one that is. The loader
   binds a mod whole or not at all, so this mod must not start, and the refusal must name the mod, the import
   (MR::noSuchFunction "void()") and why (no such row). If "fact missing 1" is ever logged, the loader started a mod
   with an unbound import. Run 3d. */
#include <luma.h>

static const LumaFunction* absent;
static const LumaFunction* present;

static int32_t start(const LumaCore* core, const LumaMod* mod) {
    core->log(mod, "fact missing 1");
    return 0;
}

static const LumaImport imports[] = {
    LUMA_FUNCTION(present, "MR::getPlayerPos", "Vec3f *()"),
    LUMA_FUNCTION(absent, "MR::noSuchFunction", "void()"),
};

LUMA_MOD(imports, start, 0);
