/* J's movie conformance mods (plan 3.10; native/include/luma_movie.h, contract movies), one case each (-DCASE=n).
   Run steps call MR::getRandom(0, 3100 + k) and the hook gives 1. Each call's status is the fact mod.<id>.<name>.
   Their movie: layers/base/assets/movies/intro.movie.json naming intro.webm (FC's AV1 sample, av1_vorbis.webm: its
   Vorbis track not read, a Note), cooked at install to Luma/<id>/movies/intro.nmov.
   jv-play (1) 3101: k 1 play "movies/intro" with an end function ("play"); its end comes as "end" (its status) and
                  "ends" (how many); position while it plays ("position", "frame" at least 0, "frames" above 0).
   jv-args (2) 3102: k 1 play "movies/none" ("missing": not found), a null name ("null_name"), a null out ("null_out"),
                  stop of 999999 ("stop_unknown"); play "movies/intro" ("play"), stop it ("stop"), stop again
                  ("stop_again"), position after ("position_ended"); its end "end" LUMA_E_CANCELLED.
   jv-subs (1) 3103: case 1's code; its .movie.json: a subtitle over the whole movie, "skip": false.
   jv-skip (1) 3104: case 1's code; its .movie.json leaves "skip" true (the player skips it with +, Yes). */
#include "../loader/lt.h"
#include <luma_movie.h>

#ifndef CASE
#error CASE
#endif

static const LumaMovieService* mv;
static LumaMovie movie;
static int32_t ends;

static void on_end(void* user, const LumaEvent* event) {
    (void)user;
    ends++;
    lt_fact("ends", ends);
    lt_fact("end", event->status);
    lt_fact("end_kind", (int32_t)event->kind);
    lt_fact("end_movie", event->handle == movie);
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 3100;
    if (k < 1 || k > 3) return 0;
#if CASE == 1
    if (k == 1) lt_fact("play", mv->play(lt_mod, "movies/intro", on_end, 0, &movie));
    if (k == 2) {
        int32_t frame = -2, frames = -2;
        lt_fact("position", mv->position(lt_mod, movie, &frame, &frames));
        lt_fact("frame", frame);
        lt_fact("frames", frames);
    }
#elif CASE == 2
    if (k == 1) {
        LumaMovie none = 0;
        int32_t frame = 0;
        lt_fact("missing", mv->play(lt_mod, "movies/none", on_end, 0, &none));
        lt_fact("null_name", mv->play(lt_mod, 0, on_end, 0, &none));
        lt_fact("null_out", mv->play(lt_mod, "movies/intro", on_end, 0, 0));
        lt_fact("stop_unknown", mv->stop(lt_mod, 999999));
        lt_fact("none_written", none == 0);
        lt_fact("play", mv->play(lt_mod, "movies/intro", on_end, 0, &movie));
        lt_fact("stop", mv->stop(lt_mod, movie));
        lt_fact("stop_again", mv->stop(lt_mod, movie));
        lt_fact("position_ended", mv->position(lt_mod, movie, &frame, 0));
    }
#endif
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(mv, LUMA_MOVIE_NAME, "1.0"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
