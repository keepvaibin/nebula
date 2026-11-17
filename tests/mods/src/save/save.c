/* H's services-save conformance mods (contract services-save 5), against the game module's save services 1.0
   (native/include/luma_save.h). One source; -DSLOT=n picks the mod:
   save-a (1)       the driver: save and save.global calls (k 1-8, 10), the game's own save (k 9: the confirm window,
                    pressed by the test, as G's saveprobe), the game's own copy, erase and load sequences (k 4, 7), the
                    title's answers (before the harness's first load of file 1), its part of the limits (k 11), the
                    read back (k 16).
   save-b..save-e (2-5)  their part of the limits (k 10 + SLOT) and the read back (k 16).
   save-g, save-gd, save-gu (6)  save.game (k 17): export_file of file 1 (a value set first, unsaved) and file 2 (not
                    created), take once in the event, the export form's frame, the 5th open request, cancel, offer's
                    argument checks. Offer (k 18-20, 2271): file 1's export written to its data folder as offer.sav and
                    a broken one as broken.sav (fs); a missing file, the broken one (FORMAT), offer.sav (the screen;
                    the test picks file 2 and REPLACE), a second while it is up (BUSY), file 2 exported after (the
                    offered bytes); again over the created file 2 (k 19: the old one kept); offer then cancel (k 20).
   Run steps call MR::getRandom(0, 4250 + k) and the hook gives 1. Values are 4-byte numbers or patterned blocks
   (byte i of key n of slot s: (i * 31 + n * 7 + s) & 255), so a read back checks every byte. */
#include "../loader/lt.h"
#include <luma_save.h>
#if SLOT == 6
#include <luma_fs.h>
#endif

#ifndef SLOT
#error SLOT
#endif

static const LumaSaveService* save;
__attribute__((unused)) static const LumaSaveGlobalService* global;

#define KEPT 0x2262u
#define KEPT_LATER 0x7777u
#define GLOBAL_VALUE 0x2269u

static unsigned char block[LUMA_SAVE_VALUE_MOST + 1];
static unsigned char back[LUMA_SAVE_VALUE_MOST];

__attribute__((unused)) static void pattern(uint32_t key, uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
        block[i] = (unsigned char)((i * 31u + key * 7u + (uint32_t)SLOT) & 255u);
    }
}

/* A fact key `<head><n>` without a formatter. */
static const char* key_n(char* out, const char* head, uint32_t n) {
    int i = 0;
    while (head[i] != 0) {
        out[i] = head[i];
        i++;
    }
    out[i++] = (char)('0' + n % 10u);
    out[i] = 0;
    return out;
}

__attribute__((unused)) static int32_t set_number(const char* key, uint32_t value) {
    return save->set(lt_mod, key, &value, sizeof(value));
}

/* The value as a number when it is one, else the status. */
__attribute__((unused)) static long long get_number(const char* key) {
    uint32_t value = 0;
    const int64_t length = save->get(lt_mod, key, &value, sizeof(value));
    return length == (int64_t)sizeof(value) ? (long long)value : (long long)length;
}

__attribute__((unused)) static void file_facts(const char* number_fact, const char* character_fact) {
    uint32_t number = 0;
    LumaSaveCharacter character = LUMA_SAVE_MARIO;
    const int32_t status = save->file(lt_mod, &number, &character);
    lt_fact(number_fact, status == LUMA_OK ? (long long)number : (long long)status);
    lt_fact(character_fact, status == LUMA_OK ? (long long)character : (long long)status);
}

/* Block n of `length` bytes as key "k<n>". */
__attribute__((unused)) static int32_t set_block(uint32_t n, uint32_t length) {
    char key[4];
    pattern(n, length);
    return save->set(lt_mod, key_n(key, "k", n), block, length);
}

/* Key "k<n>": its length as a fact len_k<n>, and ok_k<n> 1 when every byte is the pattern's. */
__attribute__((unused)) static void check_block(uint32_t n) {
    char key[4];
    char fact[12];
    key_n(key, "k", n);
    const int64_t length = save->get(lt_mod, key, back, sizeof(back));
    lt_fact(key_n(fact, "len_k", n), length);
    if (length < 0 || length > (int64_t)sizeof(back)) {
        return;
    }
    pattern(n, (uint32_t)length);
    int same = 1;
    for (int64_t i = 0; i < length; i++) {
        same &= back[i] == block[i];
    }
    lt_fact(key_n(fact, "ok_k", n), same);
}

/* The limits (k 10 + SLOT). save-a, save-b, save-c fill their 256 KiB exactly (three 64 KiB values and 65508 bytes:
   each entry 1 + 2 + 4 bytes more); save-d's fourth reaches the chunk's 1 MiB; save-e's 64 KiB does too, its 100
   bytes fit. */
__attribute__((unused)) static void fill(void) {
    if (SLOT <= 3) {
        lt_fact("fill0", set_block(0, LUMA_SAVE_VALUE_MOST));
        lt_fact("fill1", set_block(1, LUMA_SAVE_VALUE_MOST));
        lt_fact("fill2", set_block(2, LUMA_SAVE_VALUE_MOST));
        lt_fact("fill3", set_block(3, LUMA_SAVE_MOD_MOST - 3u * (7u + LUMA_SAVE_VALUE_MOST) - 7u));
        lt_fact("over_mod", set_block(4, 1));
#if SLOT == 1
        lt_fact("over_value", save->set(lt_mod, "big", block, LUMA_SAVE_VALUE_MOST + 1u));
#endif
    } else if (SLOT == 4) {
        lt_fact("fill0", set_block(0, LUMA_SAVE_VALUE_MOST));
        lt_fact("fill1", set_block(1, LUMA_SAVE_VALUE_MOST));
        lt_fact("fill2", set_block(2, LUMA_SAVE_VALUE_MOST));
        lt_fact("over_chunk", set_block(3, LUMA_SAVE_MOD_MOST - 3u * (7u + LUMA_SAVE_VALUE_MOST) - 7u));
    } else {
        lt_fact("over_chunk", set_block(0, LUMA_SAVE_VALUE_MOST));
        lt_fact("small", set_block(1, 100));
    }
}

__attribute__((unused)) static void read_back(void) {
    const uint32_t keys = SLOT <= 3 ? 4u : SLOT == 4 ? 3u : 2u;
    for (uint32_t n = SLOT == 5 ? 1u : 0u; n < keys; n++) {
        check_block(n);
    }
}

#if SLOT == 1
static const LumaFunction* save_start;  /* void GameSequenceFunction::startGameDataSaveSequence(bool, bool) */
static const LumaFunction* save_active; /* bool GameSequenceFunction::isActiveSaveDataHandleSequence() */
static const LumaFunction* save_ok;     /* bool GameSequenceFunction::isSuccessSaveDataHandleSequence() */
static const LumaFunction* load_file;   /* void GameSequenceFunction::startGameDataLoadSequence(int, bool) */
static const LumaFunction* copy_file;   /* void GameSequenceFunction::startCopyUserFileSequence(int, int) */
static const LumaFunction* erase_file;  /* void GameSequenceFunction::startDeleteUserFileSequence(int) */
static const LumaFunction* focus_on;    /* bool NebulaMenuFocus::isActive() */

/* What the frame hook does: nothing, wait for the game's save, or a step of the copy chain (k 4). */
enum { Idle, Saving, Copying, Erasing };
static int32_t phase;
static int32_t title_seen;
static const char* window_id;
static int32_t waited;
static int32_t focus_said;
static const char* const cConfirm = "System_Save00";
static const char* const cDone = "System_Save02";

static int32_t same(const char* a, const char* b) {
    if (a == 0 || b == 0) return 0;
    while (*b != 0 && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static uint8_t call_bool(const LumaFunction* fn) {
    uint8_t result = 0;
    void* args[] = {0};
    fn->invoke(args, &result);
    return result;
}

/* The game's own load of a user file, as the file select picks one (the values of that file and character). */
static void load(int32_t file, uint8_t mario) {
    void* args[] = {0, &file, &mario};
    load_file->invoke(args, 0);
}

/* Before the harness's first load of file 1, at the title: no file is loaded. */
static int32_t before_load(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (title_seen) return 0;
    title_seen = 1;
    uint32_t value = 1;
    lt_fact("title_get", save->get(lt_mod, "kept", &value, sizeof(value)));
    lt_fact("title_set", save->set(lt_mod, "kept", &value, sizeof(value)));
    lt_fact("title_remove", save->remove(lt_mod, "kept"));
    file_facts("title_file", "title_character");
    return 0;
}

/* Before SysInfoWindow::appear(id, ...): which window, while our save runs. */
static int32_t before_appear(LumaCall* call, void* user) {
    (void)user;
    const char* id = LUMA_ARG(call, 1, const char*);
    window_id = (phase == Saving && (same(id, cConfirm) || same(id, cDone))) ? id : 0;
    waited = 0;
    return 0;
}

/* After SysInfoWindow::exeWait: a window takes a press only while it waits (a press edge), so the test presses after
   30 frames of it. */
static int32_t after_wait(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (window_id == 0) return 0;
    if (++waited == 30) {
        lt_fact(same(window_id, cConfirm) ? "confirm_ready" : "done_ready", 1);
    } else if (waited > 30 && !focus_said && same(window_id, cConfirm) && call_bool(focus_on)) {
        lt_fact("focused", 1);
        focus_said = 1;
    }
    return 0;
}

static void copy_chain_loaded_2(void) {
    lt_fact("copy_get", get_number("kept"));
    file_facts("copy_file", "copy_character");
    int32_t file = 2;
    void* args[] = {0, &file};
    erase_file->invoke(args, 0);
    phase = Erasing;
}

static void copy_chain_erased(void) {
    load(2, 1);
    lt_fact("erased_get", get_number("kept"));
    load(1, 1);
    lt_fact("kept_get", get_number("kept"));
    file_facts("kept_file", "kept_character");
    lt_fact("chain_done", 1);
    phase = Idle;
}

static int32_t after_control(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (phase == Idle || call_bool(save_active)) return 0;
    if (phase == Saving) {
        lt_fact("saved", 1);
        lt_fact("save_ok", call_bool(save_ok));
        phase = Idle;
        window_id = 0;
    } else if (phase == Copying) {
        load(2, 1);
        copy_chain_loaded_2();
    } else if (phase == Erasing) {
        copy_chain_erased();
    }
    return 0;
}
#endif

#if SLOT == 6
static const LumaSaveGameService* game;
static unsigned char exported[1u << 20];
static LumaHandle h1, h2;
static int32_t events;

static uint32_t little32(const unsigned char* at) {
    return (uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16 | (uint32_t)at[3] << 24;
}

/* The export form (SaveDataHandler::makeExport): "NSAV", u16 version, u16 parts, then each part's u32 length and
   bytes. Facts: its frame, each part's length, and whether the key "inexport" set before the export is in it. */
static void read_form(int64_t length) {
    lt_fact("magic_ok", length >= 8 && exported[0] == 'N' && exported[1] == 'S' && exported[2] == 'A' && exported[3] == 'V');
    if (length < 8) return;
    lt_fact("version", exported[4] | exported[5] << 8);
    const uint32_t parts = (uint32_t)(exported[6] | exported[7] << 8);
    lt_fact("parts", parts);
    int64_t at = 8;
    char key[8];
    for (uint32_t i = 0; i < parts && i < 3u && at + 4 <= length; i++) {
        const uint32_t size = little32(exported + at);
        lt_fact(key_n(key, "part", i), size);
        at += 4 + (int64_t)size;
    }
    lt_fact("frame_ok", at == length);
    static const char cKey[] = "inexport";
    int found = 0;
    for (int64_t i = 0; !found && i + 8 <= length; i++) {
        int same = 1;
        for (int j = 0; j < 8 && same; j++) same = exported[i + j] == (unsigned char)cKey[j];
        found = same;
    }
    lt_fact("has_value", found);
}

static void on_export(void* user, const LumaEvent* event) {
    (void)user;
    events++;
    lt_fact("events", events);
    if (event->handle == h1) {
        lt_fact("e1_kind", event->kind);
        lt_fact("e1_status", event->status);
        const int64_t length = game->take(lt_mod, event->handle, exported, sizeof(exported));
        lt_fact("e1_take", length == (int64_t)event->value ? 1 : length);
        lt_fact("e1_retake", game->take(lt_mod, event->handle, exported, sizeof(exported)));
        read_form(length);
    } else if (event->handle == h2) {
        lt_fact("e2_status", event->status);
        lt_fact("e2_value", (long long)event->value);
    } else {
        lt_fact("other_event", 1);
    }
}

/* ---- offer (2271) ---- */
static const LumaFsService* fs;
static unsigned char offer_bytes[1u << 20];
static int64_t offer_length;
static LumaHandle ho, hx, hw1, hw2, hc;
static int32_t offer_round, written;

static void on_check(void* user, const LumaEvent* event) {
    (void)user;
    const int64_t length = game->take(lt_mod, event->handle, exported, sizeof(exported));
    int same = event->status == LUMA_OK && length == offer_length;
    for (int64_t i = 0; same && i < length; i++) same = exported[i] == offer_bytes[i];
    lt_fact("o1_same", same);
}

static void on_offer(void* user, const LumaEvent* event) {
    (void)user;
    if (event->kind != LUMA_SAVE_GAME_EVENT_OFFER || event->handle != ho) {
        lt_fact("o_other_event", 1);
        return;
    }
    if (offer_round == 0) {
        lt_fact("o_bad_status", event->status);
        lt_fact("o_bad_value", (long long)event->value);
        offer_round = 1;
        lt_fact("o1", game->offer(lt_mod, "offer.sav", on_offer, 0, &ho));
        lt_fact("o1_busy", game->offer(lt_mod, "offer.sav", on_offer, 0, &hx));
    } else if (offer_round == 1) {
        lt_fact("o1_status", event->status);
        lt_fact("o1_value", (long long)event->value);
        lt_fact("o1_check", game->export_file(lt_mod, 2, on_check, 0, &hc));
    } else if (offer_round == 2) {
        lt_fact("o2_status", event->status);
        lt_fact("o2_value", (long long)event->value);
    } else {
        lt_fact("o_after_cancel", 1);
    }
}

static void on_written(void* user, const LumaEvent* event) {
    lt_fact(user != 0 ? "o_write_good" : "o_write_bad", event->status);
    if (++written < 2) return;
    LumaHandle none = 0;
    lt_fact("o_missing", game->offer(lt_mod, "none.sav", on_offer, 0, &none));
    offer_round = 0;
    lt_fact("o_bad", game->offer(lt_mod, "broken.sav", on_offer, 0, &ho));
}

static void on_first(void* user, const LumaEvent* event) {
    (void)user;
    offer_length = game->take(lt_mod, event->handle, offer_bytes, sizeof(offer_bytes));
    lt_fact("o_export", offer_length > 8 && offer_length <= (int64_t)sizeof(offer_bytes));
    static const unsigned char broken[12] = {'N', 'S', 'A', 'V', 1, 0, 3, 0, 9, 9, 9, 9};
    lt_fact("o_write1", fs->write(lt_mod, "offer.sav", offer_bytes, (uint64_t)offer_length, on_written, (void*)1, &hw1));
    lt_fact("o_write2", fs->write(lt_mod, "broken.sav", broken, sizeof(broken), on_written, 0, &hw2));
}
#endif

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4250;
    if (k < 0 || k > 22) return 0;
#if SLOT == 1
    if (k == 1) {
        /* 2261: in the loaded file. */
        lt_fact("set_kept", set_number("kept", KEPT));
        lt_fact("get_kept", get_number("kept"));
        lt_fact("get_size0", save->get(lt_mod, "kept", 0, 0));
        file_facts("file", "character");
        lt_fact("get_none", get_number("none"));
        lt_fact("remove_none", save->remove(lt_mod, "none"));
        lt_fact("set_gone", set_number("gone", 1));
        lt_fact("remove_gone", save->remove(lt_mod, "gone"));
        lt_fact("get_gone", get_number("gone"));
        lt_fact("set_empty", save->set(lt_mod, "empty", 0, 0));
        lt_fact("get_empty", save->get(lt_mod, "empty", 0, 0));
        uint32_t value = 0;
        lt_fact("bad_key", save->set(lt_mod, "Bad Key", &value, sizeof(value)));
        lt_fact("null_value", save->set(lt_mod, "kept", 0, 4));
        lt_fact("null_number", save->file(lt_mod, 0, 0));
    } else if (k == 2) {
        /* 2262: after the save, changes the next run must not see. */
        lt_fact("later_kept", set_number("kept", KEPT_LATER));
        lt_fact("later_unsaved", set_number("unsaved", 1));
    } else if (k == 3) {
        lt_fact("next_kept", get_number("kept"));
        lt_fact("next_unsaved", get_number("unsaved"));
        file_facts("next_file", "next_character");
    } else if (k == 4) {
        /* 2263, 2264: file 1 (saved) copied to file 2 by the game's own copy, as the file select copies. */
        int32_t dst = 2;
        int32_t src = 1;
        void* args[] = {0, &dst, &src};
        copy_file->invoke(args, 0);
        phase = Copying;
    } else if (k == 5) {
        /* 2265: Luigi's game data of file 1. */
        lt_fact("set_who", set_number("who", 2));
        file_facts("who_file", "who_character");
    } else if (k == 6) {
        lt_fact("mario_who", get_number("who"));
        file_facts("mario_file", "mario_character");
        lt_fact("set_mario_who", set_number("who", 1));
    } else if (k == 7) {
        load(1, 0);
        lt_fact("luigi_who", get_number("who"));
        file_facts("luigi_file", "luigi_character");
        load(1, 1);
        lt_fact("back_who", get_number("who"));
        file_facts("back_file", "back_character");
    } else if (k == 8) {
        /* 2269: save.global, no game save. */
        uint32_t value = GLOBAL_VALUE;
        lt_fact("gset", global->set(lt_mod, "across", &value, sizeof(value)));
        value = 0;
        const int64_t length = global->get(lt_mod, "across", &value, sizeof(value));
        lt_fact("gget", length == 4 ? (long long)value : (long long)length);
        lt_fact("gget_none", global->get(lt_mod, "none", &value, sizeof(value)));
        lt_fact("gremove_none", global->remove(lt_mod, "none"));
        lt_fact("gbad_key", global->set(lt_mod, "Bad Key", &value, sizeof(value)));
        lt_fact("gover_value", global->set(lt_mod, "big", block, LUMA_SAVE_VALUE_MOST + 1u));
        lt_fact("gfile_value", get_number("across"));
    } else if (k == 9) {
        uint8_t remind = 0;
        uint8_t quit_message = 0;
        void* args[] = {0, &remind, &quit_message};
        phase = Saving;
        save_start->invoke(args, 0);
        lt_fact("asked", 1);
    } else if (k == 10) {
        uint32_t value = 0;
        const int64_t length = global->get(lt_mod, "across", &value, sizeof(value));
        lt_fact("next_gget", length == 4 ? (long long)value : (long long)length);
    }
#elif SLOT == 6
    if (k == 17) {
        uint32_t value = 0x1234u;
        lt_fact("set_inexport", save->set(lt_mod, "inexport", &value, sizeof(value)));
        lt_fact("export1", game->export_file(lt_mod, 1, on_export, 0, &h1));
        lt_fact("export2", game->export_file(lt_mod, 2, on_export, 0, &h2));
        LumaHandle h3 = 0, h4 = 0, h5 = 0;
        lt_fact("export3", game->export_file(lt_mod, 3, on_export, 0, &h3));
        lt_fact("export4", game->export_file(lt_mod, 3, on_export, 0, &h4));
        lt_fact("export5", game->export_file(lt_mod, 3, on_export, 0, &h5));
        lt_fact("cancel3", game->cancel(lt_mod, h3));
        lt_fact("cancel4", game->cancel(lt_mod, h4));
        lt_fact("cancel_again", game->cancel(lt_mod, h3));
        lt_fact("take_outside", game->take(lt_mod, h1, exported, sizeof(exported)));
        lt_fact("export_0", game->export_file(lt_mod, 0, on_export, 0, &h5));
        lt_fact("export_7", game->export_file(lt_mod, 7, on_export, 0, &h5));
        lt_fact("export_nofn", game->export_file(lt_mod, 1, 0, 0, &h5));
        lt_fact("offer", game->offer(lt_mod, "files/file1.bin", on_export, 0, &h5));
        lt_fact("offer_up", game->offer(lt_mod, "../file1.bin", on_export, 0, &h5));
    } else if (k == 18) {
        lt_fact("o_start", game->export_file(lt_mod, 1, on_first, 0, &h1));
    } else if (k == 19) {
        offer_round = 2;
        lt_fact("o2", game->offer(lt_mod, "offer.sav", on_offer, 0, &ho));
    } else if (k == 20) {
        offer_round = 3;
        lt_fact("o3", game->offer(lt_mod, "offer.sav", on_offer, 0, &ho));
        lt_fact("o3_cancel", game->cancel(lt_mod, ho));
        lt_fact("o3_again", game->offer(lt_mod, "offer.sav", on_offer, 0, &ho));
        lt_fact("o3_cancel2", game->cancel(lt_mod, ho));
    }
#endif
#if SLOT <= 5
    if (k == 10 + SLOT) {
        fill();
    } else if (k == 16) {
        read_back();
    }
#endif
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(save, LUMA_SAVE_NAME, "1.0"),
#if SLOT == 1
    LUMA_SERVICE(global, LUMA_SAVE_GLOBAL_NAME, "1.0"),
    LUMA_FUNCTION(save_start, "GameSequenceFunction::startGameDataSaveSequence", "void(bool, bool)"),
    LUMA_FUNCTION(save_active, "GameSequenceFunction::isActiveSaveDataHandleSequence", "bool()"),
    LUMA_FUNCTION(save_ok, "GameSequenceFunction::isSuccessSaveDataHandleSequence", "bool()"),
    LUMA_FUNCTION(load_file, "GameSequenceFunction::startGameDataLoadSequence", "void(int, bool)"),
    LUMA_FUNCTION(copy_file, "GameSequenceFunction::startCopyUserFileSequence", "void(int, int)"),
    LUMA_FUNCTION(erase_file, "GameSequenceFunction::startDeleteUserFileSequence", "void(int)"),
    LUMA_FUNCTION(focus_on, "NebulaMenuFocus::isActive", "bool()"),
    LUMA_HOOK_ON("GameSequenceFunction::startGameDataLoadSequence", "void(int, bool)", LUMA_BEFORE, before_load),
    LUMA_HOOK_ON("MarioActor::control", "void()", LUMA_AFTER, after_control),
    LUMA_HOOK_ON("SysInfoWindow::appear", "void(const char *, SysInfoWindow::SysInfoType, SysInfoWindow::SysInfoTextPos, "
                 "SysInfoWindow::SysInfoMessageType)",
                 LUMA_BEFORE, before_appear),
    LUMA_HOOK_ON("SysInfoWindow::exeWait", "void()", LUMA_AFTER, after_wait),
#elif SLOT == 6
    LUMA_SERVICE(game, LUMA_SAVE_GAME_NAME, "1.0"),
    LUMA_SERVICE(fs, LUMA_FS_NAME, "1.0"),
#endif
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

#if SLOT == 1
LT_START({
    phase = Idle;
    title_seen = 0;
    window_id = 0;
    waited = 0;
    focus_said = 0;
})
#elif SLOT == 6
LT_START({
    h1 = 0;
    h2 = 0;
    events = 0;
    ho = hx = hw1 = hw2 = hc = 0;
    offer_round = 0;
    written = 0;
    offer_length = 0;
})
#else
LT_START({})
#endif
LUMA_MOD(imports, start, 0);
