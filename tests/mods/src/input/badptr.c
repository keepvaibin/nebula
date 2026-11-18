/* 2514-bad-pointers (M-029; contract services-input): in-badptr (permissions input.inject and input.devices) gives
   input, input.inject and input.dev every pointer they read, write or call wrong (lt_bad.h) at its start: each
   LUMA_E_ARGUMENT, reported; "none_written" 1 when no out given before the wrong one was written; then a good
   device read ("device_good"). */
#include "../loader/lt.h"
#include "../loader/lt_bad.h"
#include <luma_input.h>

static const LumaInputService* input;
static const LumaInputInjectService* inject;
static const LumaInputDevService* dev;

static void on_event(void* user, const LumaEvent* event) {
    (void)user;
    (void)event;
}

static void begin(const LumaMod* mod) {
    LumaHandle none = 7;
    LumaInputAction action = 7;
    uint32_t found = 7;
    LumaInputTextRequest request = {.size = sizeof(LumaInputTextRequest), .kind = LUMA_INPUT_TEXT_ANY, .max_length = 8};
    lt_fact("action_id_unmapped", input->action(mod, (const char*)LT_UNMAPPED, &action));
    lt_fact("action_out_read_only", input->action(mod, "jump", (LumaInputAction*)LT_READ_ONLY));
    lt_fact("read_out_unmapped", input->read(mod, 1, (LumaInputState*)LT_UNMAPPED));
    lt_fact("game_out_read_only", input->game(mod, 0, (LumaInputGame*)LT_READ_ONLY));
    lt_fact("game_raw_out_unmapped", input->game_raw(mod, 0, (LumaInputGame*)LT_UNMAPPED));
    lt_fact("device_out_read_only", input->device(mod, 0, (uint32_t*)LT_READ_ONLY));
    lt_fact("watch_fn_data", input->watch_device(mod, (LumaEventFn)(uintptr_t)LT_DATA_FN, 0));
    lt_fact("text_request_unmapped", input->text_request(mod, (const LumaInputTextRequest*)LT_UNMAPPED, on_event, 0, &none));
    lt_fact("text_fn_game", input->text_request(mod, &request, (LumaEventFn)(uintptr_t)input->rumble, 0, &none));
    lt_fact("text_out_read_only", input->text_request(mod, &request, on_event, 0, (LumaHandle*)LT_READ_ONLY));
    request.initial = (const char*)LT_UNMAPPED;
    LumaHandle text = 7;
    lt_fact("text_initial_unmapped", input->text_request(mod, &request, on_event, 0, &text));
    lt_fact("inject_out_read_only", inject->begin(mod, 0, (LumaInputInject*)LT_READ_ONLY));
    lt_fact("dev_found_read_only", dev->list(mod, 0, 0, (uint32_t*)LT_READ_ONLY));
    lt_fact("dev_out_unmapped", dev->list(mod, (LumaInputDeviceInfo*)LT_UNMAPPED, 1, &found));
    lt_fact("dev_path_unmapped", dev->open(mod, (const char*)LT_UNMAPPED, on_event, 0, &none));
    lt_fact("dev_fn_data", dev->open(mod, "pad", (LumaEventFn)(uintptr_t)LT_DATA_FN, 0, &none));
    lt_fact("none_written", none == 7 && action == 7);
    lt_fact("text_none", text == 0);
    uint32_t kind = 99;
    lt_fact("device_good", input->device(mod, 0, &kind));
}

static const LumaImport imports[] = {
    LUMA_SERVICE(input, LUMA_INPUT_NAME, "1.1"),
    LUMA_SERVICE(inject, LUMA_INPUT_INJECT_NAME, "1.0"),
    LUMA_SERVICE(dev, LUMA_INPUT_DEV_NAME, "1.0"),
};

LT_START(begin(mod))
LUMA_MOD(imports, start, 0);
