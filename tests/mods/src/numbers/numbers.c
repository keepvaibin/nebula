/* F's 1.5 conformance mods (contract numbers; plan 1.5: every number and table in the game's code changeable at load,
   by name, the source untouched).
   nm-numbers (-DCASE=1): at start, by name: MR::getMaxElementIndex's number 1 (the `return 2;` of a z the largest):
                          its kind and value, the game's function returning 2, then set_number 7 and the same call
                          returning 7; the same number by a null signature (one function of that name); a float
                          (CameraContext::getRenderFovy's number 3, 1e-4f) read and set through its address; a file's constant
                          (SnowFloor.cpp's sTextureSize, 128) by its name and place 0; and each refusal of set_number
                          and number, by its number (facts r_*).
   nm-watch   (-DCASE=2): with nm-numbers, for its take-out: MR::getRandom(0, 1501) calls MR::getMaxElementIndex again
                          (fact after_call: 2, the game's number again) and reads the float (after_float_game: 1).
   nm-tables  (-DCASE=3): at start, by name: EventUtil.cpp's cCometNames (the names MR::getCometNameFromId gives): its
                          type, count, size and first name; the game's function giving "Ghost" for 1, then set_table
                          writing the mod's own name there and the game's two functions giving and finding it; a table
                          of whole numbers (StarPieceAlmsStorage.cpp's sMaxValueArray) and a record (BossBegoman.cpp's
                          hWaitParam) read; and each refusal of table and set_table (facts r_*).
   nm-tables-watch (-DCASE=4): with nm-tables, for its take-out: MR::getRandom(0, 1502) stands in for the game writing
                          comet 0 itself (fact game_wrote); MR::getRandom(0, 1503) asks the game's function for comet 1
                          again (fact after_ghost: 1, the game's name again) and comet 0 (after_game_kept: 1, the
                          game's own change kept). */
#include "../loader/lt.h"

#ifndef CASE
#error CASE
#endif

typedef struct Vec3 {
    float x, y, z;
} Vec3;

#if CASE == 1 || CASE == 2
static const LumaFunction* f_max; /* MR::getMaxElementIndex */

static int32_t max_index_of_z(void) {
    Vec3 v = {1.0f, 2.0f, 5.0f};
    void* args[2] = {0, &v};
    uint32_t got = 99;
    f_max->invoke(args, &got);
    return (int32_t)got;
}

static int32_t float_is(const void* at, float want) {
    float f;
    __builtin_memcpy(&f, at, 4);
    return f == want;
}

#endif

#if CASE == 1
static const LumaImport imports[] = {
    LUMA_FUNCTION(f_max, "MR::getMaxElementIndex", "u32(const Vec3f &)"),
};

static const char k_max[] = "MR::getMaxElementIndex";
static const char k_max_sig[] = "u32(const Vec3f &)";

LT_START({
    uint32_t kind = 0;
    uint32_t* const max_at = (uint32_t*)core->number(mod, k_max, k_max_sig, 1, &kind);
    lt_fact("found", max_at != 0);
    lt_fact("kind", kind);
    lt_fact("value", max_at != 0 ? (long long)*max_at : -1);
    lt_fact("call_before", max_index_of_z());
    lt_fact("set", core->set_number(mod, k_max, k_max_sig, 1, 7.0));
    lt_fact("value_after", max_at != 0 ? (long long)*max_at : -1);
    lt_fact("call_after", max_index_of_z());
    uint32_t kind2 = 0;
    lt_fact("null_signature_same", core->number(mod, k_max, 0, 1, &kind2) == (void*)max_at && kind2 == 8);

    uint32_t fkind = 0;
    void* const fovy = core->number(mod, "CameraContext::getRenderFovy", "f32() const", 3, &fkind);
    lt_fact("float_kind", fkind);
    lt_fact("float_game", fovy != 0 && float_is(fovy, 1e-4f));
    lt_fact("float_set", core->set_number(mod, "CameraContext::getRenderFovy", "f32() const", 3, 2.5));
    lt_fact("float_after", fovy != 0 && float_is(fovy, 2.5f));

    uint32_t ckind = 0;
    const uint16_t* const size = (const uint16_t*)core->number(mod, "Game/MapObj/SnowFloor.cpp#sTextureSize", 0, 0, &ckind);
    lt_fact("constant_kind", ckind);
    lt_fact("constant_value", size != 0 ? (long long)*size : -1);
    lt_fact("constant_by_empty_signature", core->number(mod, "Game/MapObj/SnowFloor.cpp#sTextureSize", "", 0, 0) == (const void*)size);

    lt_fact("r_no_name", core->set_number(mod, 0, 0, 1, 1.0));
    lt_fact("r_no_such", core->set_number(mod, "MR::noSuchFunction", 0, 1, 1.0));
    lt_fact("r_no_such_place", core->set_number(mod, k_max, k_max_sig, 99, 1.0));
    lt_fact("r_wrong_signature", core->set_number(mod, k_max, "u32(Vec3f)", 1, 1.0));
    lt_fact("r_fraction", core->set_number(mod, k_max, k_max_sig, 1, 2.5));
    lt_fact("r_negative_unsigned", core->set_number(mod, k_max, k_max_sig, 1, -3.0));
    lt_fact("r_u16_range", core->set_number(mod, "Game/MapObj/SnowFloor.cpp#sTextureSize", 0, 0, 70000.0));
    lt_fact("r_float_range", core->set_number(mod, "CameraContext::getRenderFovy", "f32() const", 3, 1e300));
    lt_fact("r_value_kept", max_at != 0 ? (long long)*max_at : -1);
    uint32_t nkind = 5;
    lt_fact("r_number_no_such", core->number(mod, "MR::noSuchFunction", 0, 1, &nkind) == 0 && nkind == 0);
})
LUMA_MOD(imports, start, 0);

#elif CASE == 2
static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 1, int32_t) != 0 || LUMA_ARG(call, 2, int32_t) != 1501) {
        return 0;
    }
    lt_fact("after_call", max_index_of_z());
    const void* const fovy = lt_core->number(lt_mod, "CameraContext::getRenderFovy", "f32() const", 3, 0);
    lt_fact("after_float_game", fovy != 0 && float_is(fovy, 1e-4f));
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
    LUMA_FUNCTION(f_max, "MR::getMaxElementIndex", "u32(const Vec3f &)"),
};

LT_START({})
LUMA_MOD(imports, start, 0);

#elif CASE == 3 || CASE == 4
static const LumaFunction* f_comet;    /* MR::getCometNameFromId */

static int32_t equal(const char* a, const char* b) {
    if (a == 0 || b == 0) {
        return 0;
    }
    while (*a != 0 && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static const char* comet_name(int32_t id) {
    void* args[2] = {0, &id};
    const char* got = 0;
    f_comet->invoke(args, &got);
    return got;
}

#if CASE == 3
static const LumaFunction* f_comet_id; /* MR::getCometNameIdFromString */
static const char k_comets[] = "Game/Util/EventUtil.cpp#cCometNames";
static const char k_own[] = "LumaComet";

static int32_t comet_id(const char* name) {
    void* args[2] = {0, &name};
    int32_t got = -9;
    f_comet_id->invoke(args, &got);
    return got;
}

static const LumaImport imports[] = {
    LUMA_FUNCTION(f_comet, "MR::getCometNameFromId", "const char *(int)"),
    LUMA_FUNCTION(f_comet_id, "MR::getCometNameIdFromString", "int(const char *)"),
};

LT_START({
    LumaTable t = {0};
    lt_fact("found", core->table(mod, k_comets, &t) == 0);
    lt_fact("type_char_pointer", equal(t.type, "const char *"));
    lt_fact("count", t.count);
    lt_fact("size", t.size);
    const char* const* const names = (const char* const*)t.data;
    lt_fact("first_red", names != 0 && equal(names[0], "Red"));
    lt_fact("call_before", equal(comet_name(1), "Ghost"));
    const char* own = k_own;
    lt_fact("set", core->set_table(mod, k_comets, sizeof(const char*), &own, sizeof(own)));
    lt_fact("data_after", names != 0 && names[1] == k_own);
    lt_fact("call_after", comet_name(1) == k_own);
    lt_fact("id_after", comet_id("LumaComet"));
    lt_fact("ghost_gone", comet_id("Ghost"));

    LumaTable w = {0};
    lt_fact("whole_found", core->table(mod, "Game/System/StarPieceAlmsStorage.cpp#sMaxValueArray", &w) == 0);
    lt_fact("whole_type", equal(w.type, "unsigned int"));
    lt_fact("whole_count", w.count);
    lt_fact("whole_9", w.data != 0 ? (long long)((const uint32_t*)w.data)[9] : -1);

    LumaTable r = {0};
    lt_fact("record_found", core->table(mod, "Game/Boss/BossBegoman.cpp#hWaitParam", &r) == 0);
    lt_fact("record_type", equal(r.type, "MR::ActorMoveParam"));
    lt_fact("record_count", r.count);
    lt_fact("record_size", r.size);
    lt_fact("record_second", r.data != 0 && ((const float*)r.data)[1] == 3.0f);

    LumaTable n;
    n.data = (const void*)1;
    n.type = "x";
    n.size = 7;
    n.count = 7;
    lt_fact("r_table_no_name", core->table(mod, 0, &n));
    lt_fact("r_table_no_such", core->table(mod, "Game/Util/EventUtil.cpp#noSuchTable", &n));
    lt_fact("r_table_cleared", n.data == 0 && n.type == 0 && n.size == 0 && n.count == 0);
    lt_fact("r_set_no_such", core->set_table(mod, "noSuchTable", 0, &own, sizeof(own)));
    lt_fact("r_set_past_end", core->set_table(mod, k_comets, 40, &own, sizeof(own)));
    lt_fact("r_set_straddle", core->set_table(mod, k_comets, 36, &own, sizeof(own)));
    lt_fact("r_set_none", core->set_table(mod, k_comets, 0, &own, 0));
    lt_fact("r_set_null", core->set_table(mod, k_comets, 0, 0, 8));
    lt_fact("r_kept", comet_name(1) == k_own && equal(comet_name(0), "Red"));
})
LUMA_MOD(imports, start, 0);

#else
static const char* g_game_wrote; /* what the stand-in for the game put in comet 0 */

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 1, int32_t) != 0) {
        return 0;
    }
    if (LUMA_ARG(call, 2, int32_t) == 1502) {
        /* The game changing its own table after nm-tables wrote comet 1 (H's 1.5 review 1): a write through `data`
           stands in for it here only (contract N-10 keeps mods from it). The value is the game's own string, comet 2. */
        LumaTable t = {0};
        if (lt_core->table(lt_mod, "Game/Util/EventUtil.cpp#cCometNames", &t) == 0 && t.data != 0) {
            const char** names = (const char**)t.data;
            g_game_wrote = names[2];
            names[0] = g_game_wrote;
            lt_fact("game_wrote", 1);
        }
        return 0;
    }
    if (LUMA_ARG(call, 2, int32_t) != 1503) {
        return 0;
    }
    lt_fact("after_ghost", equal(comet_name(1), "Ghost"));
    lt_fact("after_game_kept", g_game_wrote != 0 && comet_name(0) == g_game_wrote);
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
    LUMA_FUNCTION(f_comet, "MR::getCometNameFromId", "const char *(int)"),
};

LT_START({})
LUMA_MOD(imports, start, 0);
#endif
#endif
