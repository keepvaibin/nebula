/* FB's draw.post conformance mod fp-post (plan 2.4 layer 8, 3.4 layer 4; native/include/luma_draw_post.h), with its
   programs in layers/base/assets/post (post/, cooked at install). Run steps call MR::getRandom(0, 2480 + k) and the
   hook gives 1. Each call's status is the fact mod.fp-post.<name>.
   2481: k 1 program fp_tint ("tint"), in WORLD ("set_world"): the 3-D picture red. k 2 its params (0, 1, 0) ("params"):
   green. k 3 program fp_left ("left"), in FRAME ("set_frame"): the frame's left half blue, HUD included. k 4 both slots
   empty ("world_empty", "frame_empty"), params on the empty slot ("params_empty": E_STATE); program fp_depth
   ("depth"), in WORLD ("set_depth"). k 5 the refusals, each reported: a null name ("null_name"), an empty one
   ("empty_name": E_ARGUMENT), a null out ("null_out"), fp_none ("missing": E_NOT_FOUND), fp_bad ("bad": E_ARGUMENT,
   the compiler's error), fp_motion ("motion": E_ABSENT, no motion vectors with temporal upscaling off), stage 2
   ("stage": E_ARGUMENT), program 999 ("set_bad": E_HANDLE), 17 params ("params_many"), a null values with a count
   ("params_null"), a NaN ("params_nan"), program_free 999 ("free_bad": E_HANDLE); fp_tint five more times
   ("more": how many came LUMA_OK, 5) and a ninth ("ninth": E_LIMIT). k 6 program_free of fp_depth ("free_depth"):
   the WORLD slot running it empties. */
#include "../loader/lt.h"
#include <luma_draw_post.h>

#ifndef CASE
#error CASE
#endif

static const LumaDrawPostService* post;
static LumaDrawPostProgram tint, left, depth;

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 2480;
    if (k < 1 || k > 9) return 0;
    if (k == 1) {
        lt_fact("tint", post->program(lt_mod, "fp_tint", &tint));
        lt_fact("set_world", post->set(lt_mod, LUMA_DRAW_POST_WORLD, tint));
    }
    if (k == 2) {
        const float green[3] = {0.0f, 1.0f, 0.0f};
        lt_fact("params", post->params(lt_mod, LUMA_DRAW_POST_WORLD, green, 3));
    }
    if (k == 3) {
        lt_fact("left", post->program(lt_mod, "fp_left", &left));
        lt_fact("set_frame", post->set(lt_mod, LUMA_DRAW_POST_FRAME, left));
    }
    if (k == 4) {
        lt_fact("world_empty", post->set(lt_mod, LUMA_DRAW_POST_WORLD, 0));
        lt_fact("frame_empty", post->set(lt_mod, LUMA_DRAW_POST_FRAME, 0));
        const float one[1] = {1.0f};
        lt_fact("params_empty", post->params(lt_mod, LUMA_DRAW_POST_WORLD, one, 1));
        lt_fact("depth", post->program(lt_mod, "fp_depth", &depth));
        lt_fact("set_depth", post->set(lt_mod, LUMA_DRAW_POST_WORLD, depth));
    }
    if (k == 5) {
        LumaDrawPostProgram none = 0;
        lt_fact("null_name", post->program(lt_mod, 0, &none));
        lt_fact("empty_name", post->program(lt_mod, "", &none));
        lt_fact("null_out", post->program(lt_mod, "fp_tint", 0));
        lt_fact("missing", post->program(lt_mod, "fp_none", &none));
        lt_fact("bad", post->program(lt_mod, "fp_bad", &none));
        lt_fact("motion", post->program(lt_mod, "fp_motion", &none));
        lt_fact("stage", post->set(lt_mod, (LumaDrawPostStage)2, tint));
        lt_fact("set_bad", post->set(lt_mod, LUMA_DRAW_POST_FRAME, 999));
        const float many[LUMA_DRAW_POST_PARAMS_MOST + 1] = {0};
        lt_fact("params_many", post->params(lt_mod, LUMA_DRAW_POST_WORLD, many, LUMA_DRAW_POST_PARAMS_MOST + 1));
        lt_fact("params_null", post->params(lt_mod, LUMA_DRAW_POST_WORLD, 0, 2));
        const float zero = 0.0f;
        const float nan_[1] = {zero / zero};
        lt_fact("params_nan", post->params(lt_mod, LUMA_DRAW_POST_WORLD, nan_, 1));
        lt_fact("free_bad", post->program_free(lt_mod, 999));
        long long more = 0;
        for (int i = 0; i < 5; i++) {
            LumaDrawPostProgram again = 0;
            more += post->program(lt_mod, "fp_tint", &again) == LUMA_OK && again != 0;
        }
        lt_fact("more", more);
        LumaDrawPostProgram ninth = 0;
        lt_fact("ninth", post->program(lt_mod, "fp_tint", &ninth));
    }
    if (k == 6) lt_fact("free_depth", post->program_free(lt_mod, depth));
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(post, LUMA_DRAW_POST_NAME, "1.0"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
