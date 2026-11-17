/* to-objects: contract objects section 10 as a mod uses it, on the player (MR::getPlayerDemoActor, a LiveActor * to
   a MarioActor). MR::getRandom(0, 4301) runs every check once, on the game thread, and answers 1:
   - 1301 own_class through the LiveActor pointer is class_of("MarioActor"), and that number is the record rows' index
     (the test names the number the types file gives);
   - 1302 as_class to NameObj is the same address; to Kuribo, a class it is not, null;
   - 1303 the kind 8 import of LiveActor::getBaseMtx called on the player gives what the function row
     LiveActor::getBaseMtx gives (MarioActor does not override it); the kind 8 import of LiveActor::calcAndSetBaseMtx
     runs MarioActor's own function and that function's hooks (a before hook counts it: one more);
   - 1304 overrider names the row MarioActor::calcAndSetBaseMtx for that virtual, and LiveActor::getBaseMtx's row for
     getBaseMtx;
   - 1305 own_class of the player's current nerve (LiveActor.mSpine -> Spine.mCurrNerve, a header's record): a number
     of its own, is_a Nerve, as_class Nerve the same address; and the last Kuribo constructed: its current nerve, an
     NrvKuribo record Kuribo.cpp defines (LumaUnitRows; G-R22-1), the same three;
   - 1306 a pointer one byte past the player gives -3, twice: one Object.NotReadable kept (the second counted);
   - 1307 call_method on the player runs as 1303: the hook counts one more, getBaseMtx's answer the same. */
#include "../loader/lt.h"

static const LumaFunction* player;
static const LumaFunction* f_base;   /* LiveActor::getBaseMtx, the function row */
static const LumaFunction* f_calc;   /* MarioActor::calcAndSetBaseMtx, the function row */
static const LumaFunction* m_base;   /* kind 8: LiveActor::getBaseMtx */
static const LumaFunction* m_calc;   /* kind 8: LiveActor::calcAndSetBaseMtx */
static int32_t c_mario, c_live, c_nameobj, c_kuribo, c_nerve, c_wait;
static int32_t spine_at, nerve_at;
static volatile int32_t calc_runs;
/* 1305's .cpp record (G-R22-1): the last Kuribo the game constructed (an after hook on its constructor), so the
   current stage's, whatever its name, alive or not. Run 22h: no live object was named "Kuribo" at frame 110. */
static const unsigned char* kuribo;
static volatile int32_t kuribos_made;

static int32_t count_calc(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    calc_runs++;
    return 0;
}

static void* the_player(void) {
    void* args[1] = {0};
    void* got = 0;
    player->invoke(args, &got);
    return got;
}

static void* base_by(const LumaFunction* fn, void* object) {
    void* args[1] = {object};
    void* got = 0;
    fn->invoke(args, &got);
    return got;
}

static void checks(void) {
    unsigned char* const p = the_player();
    lt_fact("player", p != 0);
    if (p == 0) {
        return;
    }
    /* 1301 */
    lt_fact("own", lt_core->own_class(lt_mod, p));
    lt_fact("own_is_mario", lt_core->own_class(lt_mod, p) == c_mario);
    lt_fact("mario_number", c_mario);
    lt_fact("live_number", c_live);
    lt_fact("class_of_same", lt_core->class_of("MarioActor") == c_mario);
    /* 1302 */
    lt_fact("as_nameobj_same", lt_core->as_class(lt_mod, p, c_nameobj) == (const void*)p);
    lt_fact("as_live_same", lt_core->as_class(lt_mod, p, c_live) == (const void*)p);
    lt_fact("as_kuribo_null", lt_core->as_class(lt_mod, p, c_kuribo) == 0);
    lt_fact("is_a_live", lt_core->is_a(p, c_live));
    lt_fact("is_a_kuribo", lt_core->is_a(p, c_kuribo));
    /* 1303 */
    void* const by_row = base_by(f_base, p);
    lt_fact("base_row_some", by_row != 0);
    lt_fact("base_method_same", base_by(m_base, p) == by_row);
    lt_fact("method_by_name_same", lt_core->method("LiveActor::getBaseMtx", "MtxPtr() const") == m_base);
    lt_fact("method_unhookable", m_base->hooked == 0);
    int32_t before = calc_runs;
    void* args[1] = {p};
    m_calc->invoke(args, 0);
    lt_fact("calc_method_runs", calc_runs - before);
    lt_fact("hook_on_method_refused", lt_core->hook(lt_mod, m_calc, LUMA_BEFORE, count_calc, 0));
    /* 1304 */
    lt_fact("overrider_calc_mario",
            lt_core->overrider(lt_mod, p, "LiveActor::calcAndSetBaseMtx", "void()") == f_calc);
    lt_fact("overrider_base_live", lt_core->overrider(lt_mod, p, "LiveActor::getBaseMtx", "MtxPtr() const") == f_base);
    lt_fact("overrider_unknown_null", lt_core->overrider(lt_mod, p, "LiveActor::noSuchVirtual", "void()") == 0);
    /* 1305 */
    unsigned char* spine = 0;
    __builtin_memcpy(&spine, p + spine_at, sizeof(spine));
    const unsigned char* nerve = 0;
    if (spine != 0) {
        __builtin_memcpy(&nerve, spine + nerve_at, sizeof(nerve));
    }
    lt_fact("nerve_some", nerve != 0);
    if (nerve != 0) {
        const int32_t own = lt_core->own_class(lt_mod, nerve);
        lt_fact("nerve_class", own);
        lt_fact("nerve_not_base", own != c_nerve);
        lt_fact("nerve_is_a_nerve", lt_core->is_a(nerve, c_nerve));
        lt_fact("nerve_as_nerve_same", lt_core->as_class(lt_mod, nerve, c_nerve) == (const void*)nerve);
        lt_fact("nerve_is_wait", own == c_wait);
    }
    /* 1305: a Kuribo's current nerve, an NrvKuribo record Kuribo.cpp defines (LumaUnitRows). */
    lt_fact("kuribos_made", kuribos_made);
    lt_fact("kuribo_some", kuribo != 0);
    if (kuribo != 0) {
        lt_fact("kuribo_is_kuribo", lt_core->is_a(kuribo, c_kuribo));
        unsigned char* k_spine = 0;
        __builtin_memcpy(&k_spine, kuribo + spine_at, sizeof(k_spine));
        const unsigned char* k_nerve = 0;
        if (k_spine != 0) {
            __builtin_memcpy(&k_nerve, k_spine + nerve_at, sizeof(k_nerve));
        }
        lt_fact("kuribo_nerve_some", k_nerve != 0);
        if (k_nerve != 0) {
            /* The nerve's class is one of Kuribo.cpp's own NrvKuribo records, each found by its name. */
            static const char* const k_kuribo_nerves[] = {
                "NrvKuribo::KuriboNrvNonActive", "NrvKuribo::KuriboNrvAppearFromBox", "NrvKuribo::KuriboNrvBlow",
                "NrvKuribo::KuriboNrvBlowLand", "NrvKuribo::KuriboNrvWander", "NrvKuribo::KuriboNrvFindPlayer",
                "NrvKuribo::KuriboNrvChase", "NrvKuribo::KuriboNrvAttackSuccess", "NrvKuribo::KuriboNrvBindStarPointer",
                "NrvKuribo::KuriboNrvStagger", "NrvKuribo::KuriboNrvFlatDown", "NrvKuribo::KuriboNrvHipDropDown",
                "NrvKuribo::KuriboNrvPressDown", "NrvKuribo::KuriboNrvBlowDown"};
            const int32_t own_nerve = lt_core->own_class(lt_mod, k_nerve);
            int32_t named = 0, is_one = 0;
            for (unsigned i = 0; i < sizeof(k_kuribo_nerves) / sizeof(k_kuribo_nerves[0]); i++) {
                const int32_t c = lt_core->class_of(k_kuribo_nerves[i]);
                named += c >= 0;
                is_one |= c >= 0 && c == own_nerve;
            }
            lt_fact("kuribo_nerve_class", own_nerve);
            lt_fact("kuribo_nerves_named", named);
            lt_fact("kuribo_nerve_is_kuribos", is_one);
            lt_fact("kuribo_nerve_is_a_nerve", lt_core->is_a(k_nerve, c_nerve));
            lt_fact("kuribo_nerve_as_nerve_same", lt_core->as_class(lt_mod, k_nerve, c_nerve) == (const void*)k_nerve);
        }
    }
    /* 1306 */
    lt_fact("misaligned", lt_core->own_class(lt_mod, p + 1));
    lt_fact("misaligned_again", lt_core->own_class(lt_mod, p + 1));
    lt_fact("null_object", lt_core->own_class(lt_mod, 0));
    /* 1307 */
    before = calc_runs;
    void* call_args[1] = {0};
    lt_fact("call_calc", lt_core->call_method(lt_mod, p, "LiveActor::calcAndSetBaseMtx", "void()", call_args, 0));
    lt_fact("call_calc_runs", calc_runs - before);
    before = calc_runs;
    lt_fact("call_null_args", lt_core->call_method(lt_mod, p, "LiveActor::calcAndSetBaseMtx", "void()", 0, 0));
    lt_fact("call_null_args_runs", calc_runs - before);
    lt_fact("call_null_args_params", lt_core->call_method(lt_mod, p, "LiveActor::attackSensor",
                                                          "void(HitSensor *, HitSensor *)", 0, 0));
    void* got = 0;
    void* base_args[1] = {0};
    lt_fact("call_base", lt_core->call_method(lt_mod, p, "LiveActor::getBaseMtx", "MtxPtr() const", base_args, &got));
    lt_fact("call_base_same", got == by_row);
    lt_fact("call_unknown", lt_core->call_method(lt_mod, p, "LiveActor::noSuchVirtual", "void()", call_args, 0));
}

static int32_t marks(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) == 4301) {
        checks();
        LUMA_RESULT(call, int32_t) = 1;
    }
    return 0;
}

static int32_t take_kuribo(LumaCall* call, void* user) {
    (void)user;
    kuribo = LUMA_THIS(call, const unsigned char);
    kuribos_made++;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_FUNCTION(player, "MR::getPlayerDemoActor", "LiveActor *()"),
    LUMA_FUNCTION(f_base, "LiveActor::getBaseMtx", "MtxPtr() const"),
    LUMA_FUNCTION(f_calc, "MarioActor::calcAndSetBaseMtx", "void()"),
    LUMA_METHOD(m_base, "LiveActor::getBaseMtx", "MtxPtr() const"),
    LUMA_METHOD(m_calc, "LiveActor::calcAndSetBaseMtx", "void()"),
    LUMA_CLASS(c_mario, "MarioActor"),
    LUMA_CLASS(c_live, "LiveActor"),
    LUMA_CLASS(c_nameobj, "NameObj"),
    LUMA_CLASS(c_kuribo, "Kuribo"),
    LUMA_CLASS(c_nerve, "Nerve"),
    LUMA_CLASS(c_wait, "NrvMarioActor::MarioActorNrvWait"),
    LUMA_FIELD(spine_at, "LiveActor.mSpine", "Spine *"),
    LUMA_FIELD(nerve_at, "Spine.mCurrNerve", "const Nerve *"),
    LUMA_HOOK_ON("MarioActor::calcAndSetBaseMtx", "void()", LUMA_BEFORE, count_calc),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, marks),
    LUMA_HOOK_ON("Kuribo::Kuribo", "(const char *)", LUMA_AFTER, take_kuribo),
};

LT_START((void)0)
LUMA_MOD(imports, start, 0);
