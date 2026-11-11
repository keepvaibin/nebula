/* SPDX-License-Identifier: GPL-3.0-only */
/* The proof harness's service (contract `harness`, revision 1): what the harness's game half (tools/test_driver/luma_test,
   a LumaCore mod) asks of the host half (the test extension) through LumaCore.service("harness", 1, 0). It exists only
   in a harness run; in any other run the service is absent.

   The host half reads the test (the plan the runner wrote), keeps the test clock, drives the controls, keeps pictures,
   writes facts.jsonl and decides when the test ends. The game half does what needs the game: the save, the stage, the
   player, calls and reads by name; each one read back and reported as facts. */
#ifndef LUMA_HARNESS_H
#define LUMA_HARNESS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_HARNESS_NAME "harness"
#define LUMA_HARNESS_MAJOR 1u

typedef struct LumaHarness {
    uint32_t size;
    /* The next thing the game half is to do now, as one line of the plan ("setup save.spin 1", "call player.pos
       MR::getPlayerPos\tVec3f *()\t", ...), or null when there is nothing more this frame. The text stays valid until
       the next call. The game half calls it once a game frame (and again after each line it finished) and answers each
       line with `done` before asking again. */
    const char* (*next)(void);
    /* The answer to the line `next` gave: 1 done, 0 failed (`why` names what, in a few words). A setup line that fails
       ends the test as setup-failed; a step that fails ends it as fail. */
    void (*done)(int32_t ok, const char* why);
    /* A fact: `key` and its value as JSON text, on the test clock's current frame. */
    void (*fact)(const char* key, const char* json);
} LumaHarness;

#ifdef __cplusplus
}
#endif
#endif
