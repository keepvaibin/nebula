/* save-boot (2272): save.game asked from the mod's start, before the game has read its user files (M-076, M-077). Run
   on a save whose file 1 holds game data (2262a-save). Facts: "export" (export_file(1) in start: LUMA_PENDING, 1),
   "offer" (offer in start: LUMA_E_BUSY, -14, the files not read yet), "events" (EXPORT events: 1), "e_status" (0:
   the file as saved, not empty), "e_frame_ok" (1: the event came at a frame's start after the start), "magic_ok"
   (NSAV), "mario" (the length of part 1, Mario's game data: above 0). Before the fix the event's delivery ended the
   game (no save handler yet), or gave LUMA_E_NOT_FOUND for the saved file. */
#include "../loader/lt.h"
#include <luma_save.h>
#include <luma_time.h>

static const LumaSaveGameService* game;
static const LumaTimeService* tm;
static unsigned char exported[1u << 20];
static int32_t told;

static uint64_t frame_now(void) {
    LumaTimeNow now;
    now.size = sizeof(now);
    return tm->now(lt_mod, &now) == LUMA_OK ? now.frame : 0;
}

static uint32_t u32_at(int64_t at) {
    return (uint32_t)exported[at] | (uint32_t)exported[at + 1] << 8 | (uint32_t)exported[at + 2] << 16 |
           (uint32_t)exported[at + 3] << 24;
}

static void on_export(void* user, const LumaEvent* event) {
    (void)user;
    lt_fact("events", ++told);
    lt_fact("e_status", event->status);
    lt_fact("e_frame_ok", frame_now() >= 1);
    const int64_t length = game->take(lt_mod, event->handle, exported, sizeof(exported));
    lt_fact("magic_ok", length >= 12 && exported[0] == 'N' && exported[1] == 'S' && exported[2] == 'A' && exported[3] == 'V');
    if (length >= 12) {
        lt_fact("mario", u32_at(8));
    }
}

static void on_offer(void* user, const LumaEvent* event) {
    (void)user;
    (void)event;
    lt_fact("offer_event", 1);
}

static const LumaImport imports[] = {
    LUMA_SERVICE(game, LUMA_SAVE_GAME_NAME, "1.0"),
    LUMA_SERVICE(tm, LUMA_TIME_NAME, "1.0"),
};

LT_START({
    LumaHandle handle = 0;
    LumaHandle offered = 0;
    told = 0;
    lt_fact("export", game->export_file(lt_mod, 1, on_export, 0, &handle));
    lt_fact("offer", game->offer(lt_mod, "none.sav", on_offer, 0, &offered));
})
LUMA_MOD(imports, start, 0);
