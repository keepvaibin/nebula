/* 2257a/2257b (M-093) and 2258a/2258b (M-083); services-mods 8, services-fs 11. dot.ids, a mod whose id has a '.'
   (luma.md 4 allows one inside an id). Run steps call MR::getRandom(0, 4295 + k).
   k 1 (2257a): mods.ids.get(message, "dot-title"): "id_status"; the ids record is written with dot.ids/dot-title.
   k 2 (2257b, the start after): the record is read again (that key must not make it unreadable); get again:
   "id_status_2".
   k 3 (2258a): the number written to "dot/number.txt" in its data folder: "write" (the call), then the WRITE event's
   "written" (its status).
   k 4 (2258b, the start after): the file read back: "read" (the call), then the READ event's "read_status" and
   "same_number" (1: the file holds the number get gives now).
   2258c removes dot.ids and forgets it (the Mods page's FORGET); k 5 (2258d, the start after, dot.ids back): the file
   read again: "gone_read" (the call), then the READ event's "gone_status" (LUMA_E_NOT_FOUND: forgetting took its data
   folder). */
#include "../loader/lt.h"
#include <luma_fs.h>
#include <luma_mods.h>

static const LumaModsIdsService* ids;
static const LumaFsService* fs;
static LumaHandle handle;
static uint32_t number;
static char text[16];
static int32_t after_forget;

static int32_t number_text(uint32_t n, char* out) {
    char digits[12];
    int32_t d = 0;
    do { digits[d++] = (char)('0' + n % 10u); n /= 10u; } while (n != 0u);
    int32_t o = 0;
    while (d > 0) out[o++] = digits[--d];
    out[o] = 0;
    return o;
}

static void on_event(void* user, const LumaEvent* event) {
    (void)user;
    if (event->kind == LUMA_FS_EVENT_WRITE) {
        lt_fact("written", event->status);
    } else if (event->kind == LUMA_FS_EVENT_READ && after_forget) {
        lt_fact("gone_status", event->status);
    } else if (event->kind == LUMA_FS_EVENT_READ) {
        lt_fact("read_status", event->status);
        char got[16];
        const int64_t n = event->status == LUMA_OK ? fs->take_read(lt_mod, event->handle, got, sizeof(got) - 1u) : -1;
        char want[16];
        const int32_t w = number_text(number, want);
        int32_t same = n == w;
        for (int32_t i = 0; same && i < w; i++) same = got[i] == want[i];
        lt_fact("same_number", same);
    }
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4295;
    if (LUMA_ARG(call, 1, int32_t) != 0 || k < 1 || k > 5) return 0;
    if (k == 1) {
        lt_fact("id_status", ids->get(lt_mod, LUMA_MODS_SPACE_MESSAGE, "dot-title", &number));
    } else if (k == 2) {
        lt_fact("id_status_2", ids->get(lt_mod, LUMA_MODS_SPACE_MESSAGE, "dot-title", &number));
    } else if (k == 3) {
        (void)ids->get(lt_mod, LUMA_MODS_SPACE_MESSAGE, "dot-title", &number);
        const int32_t n = number_text(number, text);
        lt_fact("write", fs->write(lt_mod, "dot/number.txt", text, (uint64_t)n, on_event, 0, &handle));
    } else if (k == 4) {
        (void)ids->get(lt_mod, LUMA_MODS_SPACE_MESSAGE, "dot-title", &number);
        lt_fact("read", fs->read(lt_mod, "dot/number.txt", on_event, 0, &handle));
    } else {
        after_forget = 1;
        lt_fact("gone_read", fs->read(lt_mod, "dot/number.txt", on_event, 0, &handle));
    }
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(ids, LUMA_MODS_IDS_NAME, "1.0"),
    LUMA_SERVICE(fs, LUMA_FS_NAME, "1.0"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
