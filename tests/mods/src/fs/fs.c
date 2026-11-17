/* H's services-fs conformance mods (contract services-fs 11), against the host's file services 1.0
   (native/include/luma_fs.h). One source, one case each (-DCASE=n). Every request is given a tag (its user pointer);
   its event writes the facts e<tag>_kind, e<tag>_status, e<tag>_value, and for a READ or LIST e<tag>_len (the take
   asked with a null buffer), e<tag>_take and e<tag>_same (the bytes are the expected ones).
   fs-data       (1)  2301-2304: at k 1 two writes of one file in two cases, a read, a list, two moves, a read of the
                      old path, a remove of a folder, a list; at k 2 remove "" and list "".
   fs-args       (2)  2305: paths outside the rule and other mistakes, each answered at once.
   fs-limit      (3)  2306: 33 reads at k 1.
   fs-cancel     (4)  2307: a write cancelled, a read cancelled, then a read of the written file, then reads until
                      the 32 are full, at k 1.
   fs-package    (5)  2308: read_package luma.json, list_package "", a write and read in the data folder at k 1.
   fs-exchange   (6)  2309: in the Exchange folder: write, read, list, remove "" (refused), remove the folder, at k 1.
   fs-ex-declined(7)  2310: imports fs.exchange (declared; the test declines it): never starts.
   fs-ex-undecl  (7)  2310b: the same without the permission declared: L506.
   fs-writer     (8)  2311: k 1 writes same.txt in its data folder and pass/note.txt in the Exchange folder.
   fs-reader     (9)  2311: k 2 reads same.txt (its own data folder: not there) and pass/note.txt in the Exchange folder.
   fs-quota      (10) 2312: four files of 64 MiB at k 1 to 4, one byte at k 5 (the 256 MiB full), remove "" at k 6.
                      (One byte over 64 MiB is fs-args': the kernel says fs.write's Service.Limit once a session.)
   fs-last       (11) 2313: four files of 64 MiB at k 1, in the test's last frame (the game ends at once).
                      2314: k 2 reads them, in the start after.
   Run steps call MR::getRandom(0, 4300 + k) and the hook gives 1. */
#include "../loader/lt.h"
#include <luma_fs.h>

#ifndef CASE
#error CASE
#endif

__attribute__((unused)) static const LumaFsService* fs;
__attribute__((unused)) static const LumaFsExchangeService* ex;
__attribute__((unused)) static LumaHandle handle;
__attribute__((unused)) static int32_t events;
/* What each tag's READ or LIST should give (null: not compared). */
__attribute__((unused)) static const char* expect[64];

__attribute__((unused)) static int32_t same_bytes(const char* a, const char* b, int64_t n) {
    for (int64_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

__attribute__((unused)) static int64_t length(const char* a) {
    int64_t n = 0;
    while (a[n] != 0) n++;
    return n;
}

/* fact e<tag>_<what> <v> */
__attribute__((unused)) static void tag_fact(int32_t tag, const char* what, long long v) {
    char key[48];
    char* at = lt_put(key, "e");
    at = lt_num(at, tag);
    *at++ = '_';
    at = lt_put(at, what);
    *at = 0;
    lt_fact(key, v);
}

__attribute__((unused)) static void on_event(void* user, const LumaEvent* event) {
    const int32_t tag = (int32_t)(intptr_t)user;
    events++;
#if CASE == 4
    lt_fact("events_now", events); /* the test waits for the 31st before it asks (the worker's pace, not a frame count) */
#endif
    tag_fact(tag, "kind", event->kind);
    tag_fact(tag, "status", event->status);
    tag_fact(tag, "value", (long long)event->value);
    if (event->status != LUMA_OK) return;
    if (event->kind == LUMA_FS_EVENT_READ || event->kind == LUMA_FS_EVENT_LIST) {
        char got[512];
        const int listing = event->kind == LUMA_FS_EVENT_LIST;
        int64_t n;
#if CASE == 6 || CASE == 9
        n = listing ? ex->take_list(lt_mod, event->handle, 0, 0) : ex->take_read(lt_mod, event->handle, 0, 0);
        tag_fact(tag, "len", n);
        n = listing ? ex->take_list(lt_mod, event->handle, got, sizeof(got)) : ex->take_read(lt_mod, event->handle, got, sizeof(got));
#else
        n = listing ? fs->take_list(lt_mod, event->handle, 0, 0) : fs->take_read(lt_mod, event->handle, 0, 0);
        tag_fact(tag, "len", n);
        n = listing ? fs->take_list(lt_mod, event->handle, got, sizeof(got)) : fs->take_read(lt_mod, event->handle, got, sizeof(got));
#endif
        tag_fact(tag, "take", n);
        if (tag >= 0 && tag < 64 && expect[tag] != 0) {
            const int64_t want = length(expect[tag]);
            tag_fact(tag, "same", n == want && n <= (int64_t)sizeof(got) && same_bytes(got, expect[tag], n));
        }
        /* A second take of the same event: LUMA_E_HANDLE, reported. */
        char again[4];
#if CASE == 6 || CASE == 9
        tag_fact(tag, "again", listing ? ex->take_list(lt_mod, event->handle, again, sizeof(again)) : ex->take_read(lt_mod, event->handle, again, sizeof(again)));
#else
        tag_fact(tag, "again", listing ? fs->take_list(lt_mod, event->handle, again, sizeof(again)) : fs->take_read(lt_mod, event->handle, again, sizeof(again)));
#endif
    }
}

#define T(n) on_event, (void*)(intptr_t)(n), &handle

#if CASE == 10 || CASE == 11
static uint8_t big[64u << 20u];
#endif

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4300;
    if (k < 0 || k > 9) return 0;
#if CASE == 1
    if (k == 1) {
        lt_fact("w1", fs->write(lt_mod, "runs/One.bin", "hello", 5, T(1)));
        lt_fact("w2", fs->write(lt_mod, "RUNS/one.bin", "hello!", 6, T(2)));
        expect[3] = "hello!";
        lt_fact("r3", fs->read(lt_mod, "runs/one.BIN", T(3)));
        expect[4] = "runs/\nruns/One.bin\n";
        lt_fact("l4", fs->list(lt_mod, "", 1, T(4)));
        lt_fact("m5", fs->move(lt_mod, "runs/One.bin", "keep/two.bin", T(5)));
        lt_fact("m6", fs->move(lt_mod, "keep/two.bin", "keep", T(6)));
        lt_fact("r7", fs->read(lt_mod, "runs/One.bin", T(7)));
        lt_fact("d8", fs->remove(lt_mod, "runs", T(8)));
        expect[9] = "keep/\nkeep/two.bin\n";
        lt_fact("l9", fs->list(lt_mod, "", 1, T(9)));
        expect[10] = "two.bin\n";
        lt_fact("l10", fs->list(lt_mod, "keep", 0, T(10)));
        lt_fact("m11", fs->move(lt_mod, "keep/two.bin", "keep/TWO.bin", T(11)));
        expect[12] = "TWO.bin\n";
        lt_fact("l12", fs->list(lt_mod, "keep", 0, T(12)));
        lt_fact("r13", fs->read(lt_mod, "keep", T(13)));
        lt_fact("w14", fs->write(lt_mod, "keep", "x", 1, T(14)));
        lt_fact("w15", fs->write(lt_mod, "empty.bin", 0, 0, T(15)));
        expect[16] = "";
        lt_fact("r16", fs->read(lt_mod, "empty.bin", T(16)));
    }
    if (k == 2) {
        lt_fact("d20", fs->remove(lt_mod, "", T(20)));
        expect[21] = "";
        lt_fact("l21", fs->list(lt_mod, "", 1, T(21)));
        lt_fact("r22", fs->read(lt_mod, "nope.txt", T(22)));
        lt_fact("l23", fs->list(lt_mod, "nope", 0, T(23)));
        lt_fact("d24", fs->remove(lt_mod, "nope", T(24)));
    }
    if (k == 3) lt_fact("events", events);
#elif CASE == 2
    if (k == 1) {
        lt_fact("dotdot", fs->read(lt_mod, "../x", T(1)));
        lt_fact("empty", fs->read(lt_mod, "", T(1)));
        lt_fact("double_slash", fs->read(lt_mod, "a//b", T(1)));
        lt_fact("leading_slash", fs->read(lt_mod, "/a", T(1)));
        lt_fact("backslash", fs->read(lt_mod, "a\\b", T(1)));
        lt_fact("device", fs->read(lt_mod, "con.txt", T(1)));
        lt_fact("device_com", fs->read(lt_mod, "runs/COM3", T(1)));
        lt_fact("half_written", fs->read(lt_mod, "a.bin.tmp.12.3", T(1)));
        lt_fact("trailing_space", fs->read(lt_mod, "a ", T(1)));
        lt_fact("trailing_dot", fs->read(lt_mod, "a.", T(1)));
        lt_fact("hidden", fs->read(lt_mod, ".a", T(1)));
        lt_fact("not_ascii", fs->read(lt_mod, "caf\xC3\xA9", T(1)));
        lt_fact("colon", fs->read(lt_mod, "c:x", T(1)));
        {
            char longpath[300];
            for (int i = 0; i < 256; i++) longpath[i] = (i % 60 == 59) ? '/' : 'a';
            longpath[256] = 0;
            lt_fact("too_long", fs->read(lt_mod, longpath, T(1)));
            char part[70];
            for (int i = 0; i < 65; i++) part[i] = 'b';
            part[65] = 0;
            lt_fact("part_too_long", fs->read(lt_mod, part, T(1)));
            lt_fact("parts_17", fs->read(lt_mod, "a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p/q", T(1)));
            lt_fact("parts_16", fs->read(lt_mod, "a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p", T(1)));
        }
        lt_fact("null_path", fs->read(lt_mod, 0, T(1)));
        lt_fact("null_fn", fs->read(lt_mod, "a", 0, 0, &handle));
        lt_fact("null_out", fs->read(lt_mod, "a", on_event, 0, 0));
        lt_fact("null_data", fs->write(lt_mod, "a", 0, 3, T(1)));
        lt_fact("recursive_2", fs->list(lt_mod, "", 2, T(1)));
        lt_fact("to_inside", fs->move(lt_mod, "a", "A/b", T(1)));
        lt_fact("move_empty", fs->move(lt_mod, "", "b", T(1)));
        lt_fact("write_empty", fs->write(lt_mod, "", "x", 1, T(1)));
        /* One byte over 64 MiB: answered before anything is read from data (a small buffer is enough). */
        lt_fact("over", fs->write(lt_mod, "over.bin", "x", (64ull << 20u) + 1u, T(1)));
        lt_fact("cancel_none", fs->cancel(lt_mod, 123456789u));
        char buf[4];
        lt_fact("take_outside", fs->take_read(lt_mod, handle, buf, sizeof(buf)));
    }
    if (k == 2) lt_fact("events", events);
#elif CASE == 3
    if (k == 1) {
        int32_t made = 0, last = 0;
        for (int i = 0; i < 33; i++) {
            last = fs->read(lt_mod, "none.bin", T(i));
            if (last == LUMA_PENDING) made++;
        }
        lt_fact("made", made);
        lt_fact("last", last);
    }
    if (k == 2) lt_fact("events", events);
#elif CASE == 4
    if (k == 1) {
        lt_fact("w1", fs->write(lt_mod, "c.txt", "x", 1, T(1)));
        lt_fact("cancel_w1", fs->cancel(lt_mod, handle));
        lt_fact("r2", fs->read(lt_mod, "c.txt", T(2)));
        lt_fact("cancel_r2", fs->cancel(lt_mod, handle));
        lt_fact("cancel_r2_again", fs->cancel(lt_mod, handle));
        expect[3] = "x";
        lt_fact("r3", fs->read(lt_mod, "c.txt", T(3)));
        /* The cancelled write holds its place until the worker is done with it (r3 too; the cancelled read not):
           30 more requests fill the 32, the next is LUMA_E_LIMIT. */
        int32_t made = 0, last = 0;
        for (int i = 0; i < 31; i++) {
            last = fs->read(lt_mod, "none.bin", T(40));
            if (last == LUMA_PENDING) made++;
        }
        lt_fact("fill_made", made);
        lt_fact("fill_last", last);
    }
    if (k == 2) lt_fact("events", events);
#elif CASE == 5
    if (k == 1) {
        lt_fact("r1", fs->read_package(lt_mod, "luma.json", T(1)));
        /* The package as installed: the test mods are placed as their folders, the source with the code. */
        expect[2] = "code/\nluma.json\nsource/\n";
        lt_fact("l2", fs->list_package(lt_mod, "", 0, T(2)));
        /* The package holds the library of the target this library was built for (build.sh, one target). */
#if defined(_WIN32) && defined(__aarch64__)
#define FS_CODE "code/windows-arm64/"
#define FS_LIB "mod.dll"
#elif defined(_WIN32)
#define FS_CODE "code/windows-x64/"
#define FS_LIB "mod.dll"
#elif defined(__ANDROID__) && defined(__aarch64__)
#define FS_CODE "code/android-arm64/"
#define FS_LIB "mod.so"
#elif defined(__ANDROID__)
#define FS_CODE "code/android-x64/"
#define FS_LIB "mod.so"
#elif defined(__APPLE__) && defined(__aarch64__)
#define FS_CODE "code/macos-arm64/"
#define FS_LIB "mod.dylib"
#elif defined(__APPLE__)
#define FS_CODE "code/macos-x64/"
#define FS_LIB "mod.dylib"
#elif defined(__aarch64__)
#define FS_CODE "code/linux-arm64/"
#define FS_LIB "mod.so"
#else
#define FS_CODE "code/linux-x64/"
#define FS_LIB "mod.so"
#endif
        expect[3] = "code/\n" FS_CODE "\n" FS_CODE FS_LIB "\nluma.json\nsource/\nsource/fs-package.c\n";
        lt_fact("l3", fs->list_package(lt_mod, "", 1, T(3)));
        lt_fact("r4", fs->read_package(lt_mod, "nope.json", T(4)));
        lt_fact("r5", fs->read(lt_mod, "luma.json", T(5)));
    }
    if (k == 2) lt_fact("events", events);
#elif CASE == 6
    if (k == 1) {
        lt_fact("w1", ex->write(lt_mod, "fs-exchange/a.txt", "shared", 6, T(1)));
        expect[2] = "shared";
        lt_fact("r2", ex->read(lt_mod, "fs-exchange/a.txt", T(2)));
        expect[3] = "a.txt\n";
        lt_fact("l3", ex->list(lt_mod, "fs-exchange", 0, T(3)));
        lt_fact("d_root", ex->remove(lt_mod, "", T(4)));
        lt_fact("d5", ex->remove(lt_mod, "fs-exchange", T(5)));
        lt_fact("l6", ex->list(lt_mod, "fs-exchange", 0, T(6)));
    }
    if (k == 2) lt_fact("events", events);
#elif CASE == 8
    if (k == 1) {
        lt_fact("w1", fs->write(lt_mod, "same.txt", "mine", 4, T(1)));
        lt_fact("w2", ex->write(lt_mod, "pass/note.txt", "for b", 5, T(2)));
    }
#elif CASE == 9
    if (k == 2) {
        lt_fact("r1", fs->read(lt_mod, "same.txt", T(1)));
        expect[2] = "for b";
        lt_fact("r2", ex->read(lt_mod, "pass/note.txt", T(2)));
        lt_fact("d3", ex->remove(lt_mod, "pass", T(3)));
    }
#elif CASE == 10
    if (k >= 1 && k <= 4) {
        char name[] = "bigN.bin";
        name[3] = (char)('0' + k);
        tag_fact(k, "made", fs->write(lt_mod, name, big, 64ull << 20u, T(k)));
    }
    if (k == 5) lt_fact("w5", fs->write(lt_mod, "small.bin", "x", 1, T(5)));
    if (k == 6) lt_fact("d6", fs->remove(lt_mod, "", T(6)));
#elif CASE == 11
    if (k == 1) {
        for (uint32_t i = 0; i < sizeof(big); i++) big[i] = (uint8_t)(i * 7u + (i >> 16));
        for (int32_t n = 1; n <= 4; n++) {
            char name[] = "lastN.bin";
            name[4] = (char)('0' + n);
            tag_fact(n, "made", fs->write(lt_mod, name, big, sizeof(big), T(n)));
        }
    }
    if (k == 2) {
        for (int32_t n = 1; n <= 4; n++) {
            char name[] = "lastN.bin";
            name[4] = (char)('0' + n);
            tag_fact(10 + n, "asked", fs->read(lt_mod, name, T(10 + n)));
        }
    }
#endif
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
#if CASE == 6 || CASE == 7
    LUMA_SERVICE(ex, LUMA_FS_EXCHANGE_NAME, "1.0"),
#elif CASE == 8 || CASE == 9
    LUMA_SERVICE(fs, LUMA_FS_NAME, "1.0"),
    LUMA_SERVICE(ex, LUMA_FS_EXCHANGE_NAME, "1.0"),
#else
    LUMA_SERVICE(fs, LUMA_FS_NAME, "1.0"),
#endif
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
