/* 2808-bad-pointers (M-029; services-assets): assets-badptr gives assets every pointer it reads, writes or calls wrong
   (lt_bad.h) at k 1: each LUMA_E_ARGUMENT, reported; "none_written" 1 when no out was written; a good find after
   ("find_good"). Run steps call MR::getRandom(0, 4800 + k) and the hook gives 1. */
#include "../loader/lt.h"
#include "../loader/lt_bad.h"
#include <luma_assets.h>

static const LumaAssetsService* assets;

static void on_event(void* user, const LumaEvent* event) {
    (void)user;
    (void)event;
}

#define ARC "ObjectData/Kuribo.arc"

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4800;
    if (k != 1) return 0;
    LumaAssetInfo info = {.size = sizeof(LumaAssetInfo)};
    const void* data = (const void*)7;
    uint64_t bytes = 7;
    uint32_t needed = 7;
    LumaHandle none = 7;
    static const char kBytes[16] = "0123456789abcde";
    lt_fact("find_path_unmapped", assets->find(lt_mod, (const char*)LT_UNMAPPED, 0, &info));
    lt_fact("find_out_unmapped", assets->find(lt_mod, ARC, 0, (LumaAssetInfo*)LT_UNMAPPED));
    lt_fact("find_out_read_only", assets->find(lt_mod, ARC, 0, (LumaAssetInfo*)LT_READ_ONLY));
    lt_fact("bytes_data_read_only", assets->bytes(lt_mod, ARC, 0, (const void**)LT_READ_ONLY, &bytes));
    lt_fact("bytes_bytes_unmapped", assets->bytes(lt_mod, ARC, 0, &data, (uint64_t*)LT_UNMAPPED));
    lt_fact("entry_name_unmapped", assets->entry(lt_mod, ARC, 0, 0, (char*)LT_UNMAPPED, 64, 0, 0));
    lt_fact("entry_needed_read_only", assets->entry(lt_mod, ARC, 0, 0, 0, 0, (uint32_t*)LT_READ_ONLY, 0));
    lt_fact("entry_out_read_only", assets->entry(lt_mod, ARC, 0, 0, 0, 0, &needed, (LumaAssetInfo*)LT_READ_ONLY));
    lt_fact("provide_files_unmapped", assets->provide(lt_mod, "badptr/a.bin", (const LumaAssetFile*)LT_UNMAPPED, 1));
    const LumaAssetFile data_wrong = {0, LT_UNMAPPED, 16};
    lt_fact("provide_data_unmapped", assets->provide(lt_mod, "badptr/a.bin", &data_wrong, 1));
    const LumaAssetFile path_wrong[2] = {{(const char*)LT_UNMAPPED, kBytes, 16}, {"b.bin", kBytes, 16}};
    lt_fact("provide_path_unmapped", assets->provide(lt_mod, "badptr/b.arc", path_wrong, 2));
    lt_fact("withdraw_path_unmapped", assets->withdraw(lt_mod, (const char*)LT_UNMAPPED));
    lt_fact("cook_fn_data", assets->cook(lt_mod, "badptr/c.bti", kBytes, 16, (LumaEventFn)(uintptr_t)LT_DATA_FN, 0, &none));
    lt_fact("cook_request_read_only", assets->cook(lt_mod, "badptr/c.bti", kBytes, 16, on_event, 0, (LumaHandle*)LT_READ_ONLY));
    lt_fact("cook_data_unmapped", assets->cook(lt_mod, "badptr/c.bti", LT_UNMAPPED, 16, on_event, 0, &none));
    lt_fact("none_written", data == (const void*)7 && bytes == 7 && none == 7);
    lt_fact("find_good", assets->find(lt_mod, ARC, 0, &info));
    lt_fact("withdraw_none", assets->withdraw(lt_mod, "badptr/a.bin"));
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(assets, LUMA_ASSETS_NAME, "1.1"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
