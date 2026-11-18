/* 3903-flat-shadow-one-joint (P-178): sh-flat1 gives every Kuribo, after its own init, a flat-model shadow
   (MR::initShadowVolumeFlatModel, the game's own call) whose model is the Coin's: one joint, no child to drop to.
   "flat_made" counts the Kuribos given one; "started" is the mod's start. js-stage places two Kuribos. */
#include "../loader/lt.h"

static int32_t c_live;
static const LumaFunction* f_flat; /* MR::initShadowVolumeFlatModel */
static int32_t made;

static int32_t after_init(LumaCall* call, void* user) {
    (void)user;
    void* const live = (void*)lt_core->as_class(lt_mod, call->args[0], c_live);
    if (live == 0) return 0;
    const char* model = "Coin";
    void* args[3] = {0, (void*)&live, (void*)&model};
    f_flat->invoke(args, 0);
    made++;
    lt_fact("flat_made", made);
    return 0;
}

static const LumaImport imports[] = {
    LUMA_CLASS(c_live, "LiveActor"),
    LUMA_FUNCTION(f_flat, "MR::initShadowVolumeFlatModel", "void(LiveActor *, const char *)"),
    LUMA_HOOK_ON("Kuribo::init", "void(const TableIter &)", LUMA_AFTER, after_init),
};

LT_START({})
LUMA_MOD(imports, start, 0);
