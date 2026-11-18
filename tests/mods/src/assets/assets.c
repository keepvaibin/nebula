/* H's assets conformance mods (services-assets, plan 2.8 layer 1), against native/include/luma_assets.h. One source,
   one case each (-DCASE=n). Run steps call MR::getRandom(0, 4800 + k) and the hook gives 1.
   assets-read     (1) 2801: the Goomba's archive by its disc path, in another case, and in the store's form; its
                        entries walked down to a file; that file's bytes; a folder's bytes, a file's entries, a path
                        with nothing, an index past the end, a name buffer too short, GAME_ONLY, an older struct.
   assets-mistakes (2) 2802: a null path, a path of 1024 bytes, flags 2, a null out, out->size 4, a null data, a null
                        name with a size: ARGUMENT, each reported.
   assets-own      (3) 2803: its own layers/base/assets (hello.txt, sub/deep.txt) as "assets-own:<path>" and as
                        Luma/assets-own/<path>: from_mod 1, the bytes, GAME_ONLY finds nothing, the folder's entries.
   1.1 (layers 2 and 3):
   assets-provide  (4) 2804: a file at a new path (found in any spelling, its bytes, GAME_ONLY nothing, replaced,
                        withdrawn), the Goomba's archive copied from the game's and given back as the mod's; FORMAT
                        inside an archive, at a plain folder, under a file, an archive where a file is.
   assets-provide-mistakes (5) 2805: ARGUMENT for each wrong argument of provide, withdraw and cook; LIMIT.
   assets-first, assets-second (6, 7) 2806: one path, two mods: EXISTS, withdraw, then the second's.
   assets-cook     (8) 2807: a text file cooked as it is and provided; a cut model refused with the cooker's words; a
                        32 MiB text file, whose cooking takes longer than a frame. */
#include "../loader/lt.h"
#include <luma_assets.h>

#ifndef CASE
#error CASE
#endif

static const LumaAssetsService* assets;

__attribute__((unused)) static uint32_t text_length(const char* s) {
    uint32_t n = 0;
    while (s[n] != 0) n++;
    return n;
}

/* out = a + "/" + b (cut at size). */
__attribute__((unused)) static void join(char* out, uint32_t size, const char* a, const char* b) {
    uint32_t at = 0;
    for (uint32_t i = 0; a[i] != 0 && at + 1 < size; i++) out[at++] = a[i];
    if (at + 1 < size) out[at++] = '/';
    for (uint32_t i = 0; b[i] != 0 && at + 1 < size; i++) out[at++] = b[i];
    out[at] = 0;
}

__attribute__((unused)) static int32_t same_bytes(const void* a, const char* b, uint64_t n) {
    const unsigned char* p = (const unsigned char*)a;
    for (uint64_t i = 0; i < n; i++) {
        if (p[i] != (unsigned char)b[i]) return 0;
    }
    return 1;
}

/* A LumaAssetInfo of `size` bytes as the mod sees it, the rest 0. */
static LumaAssetInfo blank(uint32_t size) {
    LumaAssetInfo info;
    unsigned char* p = (unsigned char*)&info;
    for (uint32_t i = 0; i < sizeof(info); i++) p[i] = 0;
    info.size = size;
    return info;
}

__attribute__((unused)) static LumaAssetInfo info_of(const char* path, uint32_t flags, int32_t* status) {
    LumaAssetInfo info = blank(sizeof(LumaAssetInfo));
    *status = assets->find(lt_mod, path, flags, &info);
    return info;
}

#if CASE == 1
static char path[512];
static char name[128];

static void read_case(void) {
    int32_t status = 0;
    const LumaAssetInfo arc = info_of("ObjectData/Kuribo.arc", 0, &status);
    lt_fact("arc_find", status);
    lt_fact("arc_folder", arc.folder);
    lt_fact("arc_archive", arc.archive);
    lt_fact("arc_some", arc.entries > 0);
    lt_fact("arc_from_mod", arc.from_mod);
    const LumaAssetInfo cased = info_of("objectdata/KURIBO.ARC", 0, &status);
    lt_fact("case_same", status == LUMA_OK && cased.entries == arc.entries && cased.folder == 1);
    const LumaAssetInfo stored = info_of("ObjectData/Kuribo", 0, &status);
    lt_fact("store_form_same", status == LUMA_OK && stored.entries == arc.entries);
    const LumaAssetInfo game = info_of("ObjectData/Kuribo.arc", LUMA_ASSETS_GAME_ONLY, &status);
    lt_fact("game_only_same", status == LUMA_OK && game.entries == arc.entries && game.from_mod == 0);

    /* Down the archive to its first file (at most 4 folders deep). */
    join(path, sizeof(path), "ObjectData", "Kuribo.arc");
    LumaAssetInfo at = arc;
    int32_t depth = 0, found_file = 0;
    while (depth < 4 && at.folder == 1 && at.entries > 0) {
        LumaAssetInfo child = blank(sizeof(LumaAssetInfo));
        uint32_t needed = 0;
        int32_t s = LUMA_E_NOT_FOUND;
        uint32_t i = 0;
        /* The first file of this folder, else its first folder. */
        for (i = 0; i < at.entries; i++) {
            s = assets->entry(lt_mod, path, 0, i, name, sizeof(name), &needed, &child);
            if (s == LUMA_OK && child.folder == 0) break;
        }
        if (i == at.entries) {
            s = assets->entry(lt_mod, path, 0, 0, name, sizeof(name), &needed, &child);
        }
        if (s != LUMA_OK) break;
        char next[512];
        join(next, sizeof(next), path, name);
        for (uint32_t k = 0; k < sizeof(path); k++) {
            path[k] = next[k];
            if (next[k] == 0) break;
        }
        at = child;
        depth++;
        if (child.folder == 0) {
            found_file = 1;
            break;
        }
    }
    lt_fact("file_found", found_file);
    if (found_file) {
        int32_t s = 0;
        const LumaAssetInfo file = info_of(path, 0, &s);
        lt_fact("file_find", s);
        const void* data = 0;
        uint64_t bytes = 0;
        lt_fact("file_bytes", assets->bytes(lt_mod, path, 0, &data, &bytes));
        lt_fact("file_bytes_match", bytes == file.bytes && bytes == at.bytes);
        lt_fact("file_bytes_some", bytes > 0 && data != 0);
        uint64_t sum = 0;
        for (uint64_t i = 0; i < bytes; i++) sum += ((const unsigned char*)data)[i];
        lt_fact("file_sum", (long long)(sum & 0xFFFFFFu));  /* every byte read; logged, not compared */
        uint32_t needed = 0;
        lt_fact("file_entries", assets->entry(lt_mod, path, 0, 0, name, sizeof(name), &needed, 0));
    }
    const void* data = 0;
    uint64_t bytes = 0;
    lt_fact("folder_bytes", assets->bytes(lt_mod, "ObjectData/Kuribo.arc", 0, &data, &bytes));
    info_of("ObjectData/NoSuchThingHere.arc", 0, &status);
    lt_fact("missing", status);
    uint32_t needed = 0;
    lt_fact("past_end", assets->entry(lt_mod, "ObjectData/Kuribo.arc", 0, arc.entries, name, sizeof(name), &needed, 0));
    char one[1];
    lt_fact("name_short", assets->entry(lt_mod, "ObjectData/Kuribo.arc", 0, 0, one, sizeof(one), &needed, 0));
    lt_fact("name_short_empty", one[0] == 0);
    assets->entry(lt_mod, "ObjectData/Kuribo.arc", 0, 0, name, sizeof(name), 0, 0);
    lt_fact("needed_ok", needed == text_length(name) + 1);
    lt_fact("query_size", assets->entry(lt_mod, "ObjectData/Kuribo.arc", 0, 0, 0, 0, &needed, 0));
    /* A mod built with an older, shorter LumaAssetInfo: only its 8 bytes are written. */
    LumaAssetInfo old = blank(8u);
    old.folder = old.archive = old.kind = 0xABCDu;
    lt_fact("old_find", assets->find(lt_mod, "ObjectData/Kuribo.arc", 0, &old));
    lt_fact("old_kept", old.archive == 0xABCDu && old.kind == 0xABCDu && old.folder == 1);
}
#elif CASE == 2
static void mistakes_case(void) {
    LumaAssetInfo info = blank(sizeof(LumaAssetInfo));
    static char long_path[1025];
    for (int i = 0; i < 1024; i++) long_path[i] = 'a';
    long_path[1024] = 0;
    lt_fact("null_path", assets->find(lt_mod, 0, 0, &info));
    lt_fact("long_path", assets->find(lt_mod, long_path, 0, &info));
    lt_fact("bad_flags", assets->find(lt_mod, "ObjectData", 2, &info));
    lt_fact("null_out", assets->find(lt_mod, "ObjectData", 0, 0));
    LumaAssetInfo tiny = blank(4u);
    lt_fact("tiny_out", assets->find(lt_mod, "ObjectData", 0, &tiny));
    uint64_t bytes = 0;
    lt_fact("null_data", assets->bytes(lt_mod, "ObjectData/Kuribo.arc", 0, 0, &bytes));
    uint32_t needed = 0;
    lt_fact("null_name", assets->entry(lt_mod, "ObjectData", 0, 0, 0, 5, &needed, 0));
}
#elif CASE == 3
static char name[64];

static void own_case(void) {
    int32_t status = 0;
    const LumaAssetInfo hello = info_of("assets-own:hello.txt", 0, &status);
    lt_fact("own_find", status);
    lt_fact("own_from_mod", hello.from_mod);
    lt_fact("own_size", (long long)hello.bytes);
    const void* data = 0;
    uint64_t bytes = 0;
    lt_fact("own_bytes", assets->bytes(lt_mod, "assets-own:hello.txt", 0, &data, &bytes));
    lt_fact("own_same", bytes == 10 && same_bytes(data, "hello luma", 10));
    const LumaAssetInfo stored = info_of("Luma/assets-own/hello.txt", 0, &status);
    lt_fact("own_store_path", status == LUMA_OK && stored.bytes == hello.bytes);
    info_of("assets-own:hello.txt", LUMA_ASSETS_GAME_ONLY, &status);
    lt_fact("own_game_only", status);
    const LumaAssetInfo folder = info_of("Luma/assets-own", 0, &status);
    lt_fact("own_folder", status == LUMA_OK && folder.folder == 1 && folder.from_mod == 1);
    lt_fact("own_folder_entries", folder.entries);
    uint32_t needed = 0;
    LumaAssetInfo deep = blank(sizeof(LumaAssetInfo));
    lt_fact("deep_entry", assets->entry(lt_mod, "assets-own:sub", 0, 0, name, sizeof(name), &needed, &deep));
    lt_fact("deep_name", text_length(name) == 8 && same_bytes(name, "deep.txt", 8));
    lt_fact("deep_size", (long long)deep.bytes);
}
#elif CASE == 4
/* The game's own Goomba archive copied file by file (GAME_ONLY), then given back as the mod's. */
#define COPY_MOST 64u
static char names[COPY_MOST][64];
static LumaAssetFile copies[COPY_MOST];
static char inner[600];
static char name[64];

static void provide_case(void) {
    static const char hello[] = "provided by a mod";
    static const char again[] = "provided again";
    const LumaAssetFile one = {0, hello, sizeof(hello) - 1u};
    lt_fact("file_provide", assets->provide(lt_mod, "ModData/Hello.txt", &one, 1u));
    int32_t status = 0;
    const LumaAssetInfo file = info_of("moddata/HELLO.txt", 0, &status);
    lt_fact("file_find", status);
    lt_fact("file_from_mod", file.from_mod);
    lt_fact("file_size", (long long)file.bytes);
    const void* data = 0;
    uint64_t bytes = 0;
    lt_fact("file_bytes", assets->bytes(lt_mod, "ModData/./Hello.txt", 0, &data, &bytes));
    lt_fact("file_same", bytes == sizeof(hello) - 1u && same_bytes(data, hello, bytes));
    info_of("ModData/Hello.txt", LUMA_ASSETS_GAME_ONLY, &status);
    lt_fact("file_game_only", status);
    const LumaAssetFile second = {"", again, sizeof(again) - 1u};
    lt_fact("file_again", assets->provide(lt_mod, "ModData/Hello.txt", &second, 1u));
    lt_fact("file_again_bytes", assets->bytes(lt_mod, "ModData/Hello.txt", 0, &data, &bytes));
    lt_fact("file_again_same", bytes == sizeof(again) - 1u && same_bytes(data, again, bytes));
    lt_fact("withdraw", assets->withdraw(lt_mod, "ModData/Hello.txt"));
    info_of("ModData/Hello.txt", 0, &status);
    lt_fact("withdrawn_find", status);
    lt_fact("withdraw_again", assets->withdraw(lt_mod, "ModData/Hello.txt"));

    /* The archive: the game's files (top level), copied. */
    const LumaAssetInfo arc = info_of("ObjectData/Kuribo.arc", LUMA_ASSETS_GAME_ONLY, &status);
    uint32_t count = 0;
    for (uint32_t i = 0; i < arc.entries && count < COPY_MOST; i++) {
        LumaAssetInfo child = blank(sizeof(LumaAssetInfo));
        uint32_t needed = 0;
        if (assets->entry(lt_mod, "ObjectData/Kuribo.arc", LUMA_ASSETS_GAME_ONLY, i, names[count], sizeof(names[count]),
                          &needed, &child) != LUMA_OK ||
            child.folder != 0) {
            continue;
        }
        join(inner, sizeof(inner), "ObjectData/Kuribo.arc", names[count]);
        const void* at = 0;
        uint64_t n = 0;
        if (assets->bytes(lt_mod, inner, LUMA_ASSETS_GAME_ONLY, &at, &n) != LUMA_OK) continue;
        copies[count].path = names[count];
        copies[count].data = at;
        copies[count].bytes = n;
        count++;
    }
    lt_fact("arc_copied", count > 0);
    lt_fact("arc_provide", assets->provide(lt_mod, "ObjectData/Kuribo.arc", copies, count));
    const LumaAssetInfo mine = info_of("ObjectData/Kuribo.arc", 0, &status);
    lt_fact("arc_find", status);
    lt_fact("arc_mine", mine.from_mod == 1 && mine.folder == 1 && mine.archive == 1 && mine.entries == count);
    uint32_t needed = 0;
    LumaAssetInfo first = blank(sizeof(LumaAssetInfo));
    lt_fact("arc_entry", assets->entry(lt_mod, "ObjectData/Kuribo.arc", 0, 0, name, sizeof(name), &needed, &first));
    lt_fact("arc_entry_same", count > 0 && text_length(name) == text_length(names[0]) &&
                                  same_bytes(name, names[0], text_length(name)) && first.bytes == copies[0].bytes &&
                                  first.from_mod == 1);
    join(inner, sizeof(inner), "ObjectData/Kuribo.arc", names[0]);
    lt_fact("arc_file_bytes", assets->bytes(lt_mod, inner, 0, &data, &bytes));
    lt_fact("arc_file_same", count > 0 && bytes == copies[0].bytes && same_bytes(data, (const char*)copies[0].data, bytes));
    const LumaAssetInfo game = info_of("ObjectData/Kuribo.arc", LUMA_ASSETS_GAME_ONLY, &status);
    lt_fact("arc_game_kept", status == LUMA_OK && game.from_mod == 0);

    /* Answers. */
    const LumaAssetFile x = {0, hello, 4u};
    lt_fact("inside_archive", assets->provide(lt_mod, inner, &x, 1u));
    lt_fact("plain_folder", assets->provide(lt_mod, "ObjectData", &x, 1u));
    const LumaAssetFile y = {"a.txt", hello, 4u};
    lt_fact("own_file_to_archive", assets->provide(lt_mod, "ModData/Hello2.txt", &x, 1u) == LUMA_OK &&
                                    assets->provide(lt_mod, "ModData/Hello2.txt", &y, 1u) == LUMA_OK);
    lt_fact("under_file", assets->provide(lt_mod, "ModData/Hello2.txt/b.txt", &x, 1u));
    lt_fact("archive_over_file", assets->provide(lt_mod, "ObjectData/Kuribo.arc/", &x, 1u));
}
#elif CASE == 5
static void provide_mistakes_case(void) {
    static const char bytes4[] = "abcd";
    const LumaAssetFile one = {0, bytes4, 4u};
    lt_fact("null_files", assets->provide(lt_mod, "ModData/M.txt", 0, 1u));
    lt_fact("zero_count", assets->provide(lt_mod, "ModData/M.txt", &one, 0u));
    lt_fact("too_many", assets->provide(lt_mod, "ModData/M.txt", &one, LUMA_ASSETS_PROVIDE_FILES_MOST + 1u));
    const LumaAssetFile no_data = {0, 0, 4u};
    lt_fact("null_data", assets->provide(lt_mod, "ModData/M.txt", &no_data, 1u));
    const LumaAssetFile dots[2] = {{"a/../b.txt", bytes4, 4u}, {"c.txt", bytes4, 4u}};
    lt_fact("dot_dot", assets->provide(lt_mod, "ModData/M", dots, 2u));
    const LumaAssetFile twice[2] = {{"a.txt", bytes4, 4u}, {"A.TXT", bytes4, 4u}};
    lt_fact("twice", assets->provide(lt_mod, "ModData/M", twice, 2u));
    const LumaAssetFile folder_file[2] = {{"a", bytes4, 4u}, {"a/b.txt", bytes4, 4u}};
    lt_fact("file_is_folder", assets->provide(lt_mod, "ModData/M", folder_file, 2u));
    const LumaAssetFile empty_path[2] = {{"", bytes4, 4u}, {"c.txt", bytes4, 4u}};
    lt_fact("empty_path", assets->provide(lt_mod, "ModData/M", empty_path, 2u));
    lt_fact("null_path", assets->provide(lt_mod, 0, &one, 1u));
    lt_fact("root_path", assets->provide(lt_mod, "/", &one, 1u));
    const LumaAssetFile huge = {0, bytes4, LUMA_ASSETS_PROVIDE_MOST + 1ull};
    lt_fact("over_most", assets->provide(lt_mod, "ModData/M.txt", &huge, 1u));
    lt_fact("withdraw_null", assets->withdraw(lt_mod, 0));
    LumaHandle request = 0;
    lt_fact("cook_null_fn", assets->cook(lt_mod, "ModData/M.txt", bytes4, 4u, 0, 0, &request));
}
#elif CASE == 6 || CASE == 7
/* Two mods, one path: the first provides (k 1), the second is answered EXISTS (k 2), the first withdraws (k 3), the
   second provides (k 4). */
static void two_case(int32_t k) {
    static const char first[] = "first mod";
#if CASE == 6
    const LumaAssetFile one = {0, first, sizeof(first) - 1u};
    if (k == 1) lt_fact("first_provide", assets->provide(lt_mod, "ModData/Shared.txt", &one, 1u));
    if (k == 3) lt_fact("first_withdraw", assets->withdraw(lt_mod, "ModData/Shared.txt"));
#else
    static const char second[] = "second mod";
    const LumaAssetFile one = {0, second, sizeof(second) - 1u};
    if (k == 2) {
        lt_fact("second_exists", assets->provide(lt_mod, "ModData/Shared.txt", &one, 1u));
        lt_fact("second_withdraw", assets->withdraw(lt_mod, "ModData/Shared.txt"));
        const void* data = 0;
        uint64_t bytes = 0;
        lt_fact("second_sees_first", assets->bytes(lt_mod, "ModData/Shared.txt", 0, &data, &bytes) == LUMA_OK &&
                                         bytes == sizeof(first) - 1u && same_bytes(data, first, bytes));
    }
    if (k == 4) lt_fact("second_provide", assets->provide(lt_mod, "ModData/Shared.txt", &one, 1u));
#endif
}
#elif CASE == 8
/* The cooker at run time: a text file it leaves as it is, an empty archive name with a broken model inside nothing (a
   model whose blocks are cut: the cooker refuses it, with its words). */
static const char text[] = "cooked as it is";
/* A J3D2 bdl4 header claiming more than it holds. */
static const unsigned char broken[32] = {'J', '3', 'D', '2', 'b', 'd', 'l', '4', 0, 0, 0x10, 0, 0, 0, 0, 8};
static LumaHandle text_request, broken_request, big_request;
#define BIG_BYTES (32ull << 20u)

static void cooked(void* user, const LumaEvent* event) {
    (void)user;
    if (event->kind != LUMA_ASSETS_EVENT_COOKED) return;
    if (event->handle == text_request) {
        lt_fact("text_cooked", event->status);
        lt_fact("text_held", event->value > 0);
        const void* data = 0;
        uint64_t bytes = 0;
        lt_fact("text_bytes", assets->bytes(lt_mod, "ModData/Cooked.txt", 0, &data, &bytes));
        lt_fact("text_same", bytes == sizeof(text) - 1u && same_bytes(data, text, bytes));
    } else if (event->handle == broken_request) {
        lt_fact("broken_cooked", event->status);
        lt_fact("broken_words", event->data != 0 && event->data_size > 0);
        int32_t status = 0;
        info_of("ModData/Broken.bdl", 0, &status);
        lt_fact("broken_absent", status);
    } else if (event->handle == big_request) {
        lt_fact("big_cooked", event->status);
        const void* data = 0;
        uint64_t bytes = 0;
        lt_fact("big_bytes", assets->bytes(lt_mod, "ModData/Big.txt", 0, &data, &bytes));
        const unsigned char* p = (const unsigned char*)data;
        lt_fact("big_same", bytes == BIG_BYTES && p != 0 && p[0] == 'b' && p[BIG_BYTES / 2u] == 'b' && p[BIG_BYTES - 1u] == 'b');
    }
}

static void cook_case(void) {
    lt_fact("text_cook", assets->cook(lt_mod, "ModData/Cooked.txt", text, sizeof(text) - 1u, cooked, 0, &text_request));
    lt_fact("broken_cook",
            assets->cook(lt_mod, "ModData/Broken.bdl", broken, sizeof(broken), cooked, 0, &broken_request));
    lt_fact("requests_differ", text_request != 0 && broken_request != 0 && text_request != broken_request);
    /* The cook copies what it is given before it returns: the buffer goes at once. */
    unsigned char* big = (unsigned char*)lt_core->alloc(lt_mod, BIG_BYTES, 16);
    lt_fact("big_alloc", big != 0);
    if (big == 0) return;
    for (uint64_t i = 0; i < BIG_BYTES; i++) big[i] = 'b';
    lt_fact("big_cook", assets->cook(lt_mod, "ModData/Big.txt", big, BIG_BYTES, cooked, 0, &big_request));
    lt_core->free(lt_mod, big);
}
#endif

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4800;
#if CASE == 6 || CASE == 7
    if (k >= 1 && k <= 4) two_case(k);
    return 0;
#else
    if (k != 1) return 0;
#endif
#if CASE == 1
    read_case();
#elif CASE == 2
    mistakes_case();
#elif CASE == 3
    own_case();
#elif CASE == 4
    provide_case();
#elif CASE == 5
    provide_mistakes_case();
#elif CASE == 8
    cook_case();
#endif
    return 0;
}

static const LumaImport imports[] = {
#if CASE >= 4
    LUMA_SERVICE(assets, LUMA_ASSETS_NAME, "1.1"),
#else
    LUMA_SERVICE(assets, LUMA_ASSETS_NAME, "1.0"),
#endif
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
