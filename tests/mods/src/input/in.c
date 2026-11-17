/* F's 2.5 conformance mods (contract services-input 1.0), one source, CASE:
   1 in-follow  key options "jump2" (default "a") and "spin2" (default "b"). At start: action("jump2") gives a handle,
                action("nope") answers LUMA_E_NOT_FOUND, a read with size 8 is refused (StructSize), a read of handle 0
                is refused (Handle). Each frame (after MarioActor::control) reads jump2: facts pressed, released (counts),
                longest (the most frames down), down_now; and spin2 bound (0: it follows its control).
   2 in-other   key option "x" (default "z"). Reads handles 1 to 8: only its own reads LUMA_OK (fact own_reads 1), the
                others are refused LUMA_E_HANDLE (fact others_refused 7 when 8 actions exist; at least 1).
   Layers 2 to 6 (input 1.1, input.inject 1.0, input.dev 1.0):
   3 in-game    game, game_raw, device, watch_device and rumble at the first frame in the stage, and the refusals;
                a_frames counts the frames the game read A; raw_differs counts frames game_raw differed (none).
   4 in-inject  permission input.inject: begins on player 0 at the first frame in the stage, sets the stick up for
                120 frames (frame 1 with a stick x not a number, frame 2 a button past the game's), then ends;
                moved_sq is how far Mario went (squared), after_end_injected the read after the end.
   4 in-inject-undecl  the same code with no permission in its luma.json (the import is refused).
   6 in-text    text_request's refusals at the start; in the stage a NAME request with an initial cut to 8, its peek,
                a second request (the first ends CANCELLED), the game's A held at rest while open, cancel at 300;
                at 310 a NUMBER request of 6 the harness types into; a_after counts A after it ends.
   7 in-dev     permission input.devices: list, the refusals, an unknown path; no device is opened.
   8 in-rumble  rumble on the harness's controller (test device pad): the refusals, the co-star's ABSENT, 0.5 for a
                second, 1.0, then strength 0 (the harness's fact input.motor). */
#include "../loader/lt.h"
#include <luma_input.h>

#ifndef CASE
#error CASE
#endif

static const LumaInputService* input;

#if CASE == 1
static LumaInputAction jump2, spin2;
static long long pressed, released, longest;

static int32_t after(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    LumaInputState s = {.size = sizeof(LumaInputState)};
    if (input->read(lt_mod, jump2, &s) != LUMA_OK) {
        lt_fact("read_failed", 1);
        return 0;
    }
    if (s.pressed) {
        pressed++;
        lt_fact("pressed", pressed);
    }
    if (s.released) {
        released++;
        lt_fact("released", released);
    }
    if ((long long)s.frames > longest) {
        longest = (long long)s.frames;
        lt_fact("longest", longest);
    }
    LumaInputState t = {.size = sizeof(LumaInputState)};
    if (input->read(lt_mod, spin2, &t) == LUMA_OK) {
        lt_fact("spin2_bound", t.bound);
    }
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(input, LUMA_INPUT_NAME, "1.0"),
    LUMA_HOOK_ON("MarioActor::control", "void()", LUMA_AFTER, after),
};

static void begin(const LumaMod* mod) {
    lt_fact("jump2_ok", input->action(mod, "jump2", &jump2) == LUMA_OK && jump2 != 0);
    lt_fact("spin2_ok", input->action(mod, "spin2", &spin2) == LUMA_OK && spin2 != 0 && spin2 != jump2);
    LumaInputAction none = 7;
    lt_fact("nope_not_found", input->action(mod, "nope", &none) == LUMA_E_NOT_FOUND && none == 0);
    LumaInputState small = {.size = 8};
    lt_fact("small_refused", input->read(mod, jump2, &small) == LUMA_E_ARGUMENT);
    LumaInputState zero = {.size = sizeof(LumaInputState)};
    lt_fact("zero_refused", input->read(mod, 0, &zero) == LUMA_E_HANDLE);
    LumaInputState first = {.size = sizeof(LumaInputState)};
    lt_fact("first_up", input->read(mod, jump2, &first) == LUMA_OK && first.down == 0 && first.size == sizeof(LumaInputState));
}
LT_START(begin(mod))
#elif CASE == 2
static const LumaImport imports[] = {LUMA_SERVICE(input, LUMA_INPUT_NAME, "1.0")};

static void begin(const LumaMod* mod) {
    LumaInputAction mine = 0;
    lt_fact("x_ok", input->action(mod, "x", &mine) == LUMA_OK);
    long long own = 0, refused = 0;
    for (LumaInputAction h = 1; h <= 8; h++) {
        LumaInputState s = {.size = sizeof(LumaInputState)};
        const int32_t status = input->read(mod, h, &s);
        if (status == LUMA_OK) own += h == mine;
        if (status == LUMA_E_HANDLE) refused++;
    }
    lt_fact("own_reads", own);
    lt_fact("others_refused", refused);
}
LT_START(begin(mod))
#elif CASE == 3
/* in-game (input 1.1): game, game_raw, device, watch_device, rumble. */
static uint32_t kind_at_start;
static long long frames_seen, a_frames, raw_differs, device_events;

static void on_device(void* user, const LumaEvent* event) {
    (void)user;
    if (event->kind == LUMA_INPUT_EVENT_DEVICE) {
        device_events++;
        lt_fact("device_events", device_events);
        lt_fact("device_now", (long long)event->value);
    }
}

static int same_game(const LumaInputGame* a, const LumaInputGame* b) {
    return a->buttons == b->buttons && a->stick_x == b->stick_x && a->stick_y == b->stick_y &&
           a->pointer_on == b->pointer_on && a->shake == b->shake && a->device == b->device &&
           a->injected == b->injected;
}

static int32_t after(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    frames_seen++;
    LumaInputGame g = {.size = sizeof(LumaInputGame)};
    const int32_t status = input->game(lt_mod, 0, &g);
    if (frames_seen == 1) {
        lt_fact("game_status", status);
        lt_fact("game_device", g.device);
        lt_fact("game_injected", g.injected);
        LumaInputGame costar = {.size = sizeof(LumaInputGame)};
        lt_fact("costar_status", input->game(lt_mod, 1, &costar));
        LumaInputGame past = {.size = sizeof(LumaInputGame)};
        lt_fact("player2_refused", input->game(lt_mod, 2, &past) == LUMA_E_ARGUMENT);
        LumaInputGame small = {.size = 24};
        lt_fact("small_refused", input->game(lt_mod, 0, &small) == LUMA_E_ARGUMENT);
        uint32_t kind = 99;
        lt_fact("device_status", input->device(lt_mod, 0, &kind));
        lt_fact("device_kind", kind);
        lt_fact("device_same", kind == g.device);
        lt_fact("device_null_refused", input->device(lt_mod, 0, 0) == LUMA_E_ARGUMENT);
        /* A device with no motor answers ABSENT; one with a motor OK (and the host shakes it a second). */
        lt_fact("rumble_status", input->rumble(lt_mod, 0, 0.5f, 1.0f));
        lt_fact("rumble_nan_refused", input->rumble(lt_mod, 0, __builtin_nanf(""), 1.0f) == LUMA_E_ARGUMENT);
        lt_fact("rumble_long_refused", input->rumble(lt_mod, 0, 0.5f, 11.0f) == LUMA_E_ARGUMENT);
        lt_fact("rumble_player_refused", input->rumble(lt_mod, 2, 0.5f, 1.0f) == LUMA_E_ARGUMENT);
        (void)input->rumble(lt_mod, 0, 0.0f, 0.0f);
    }
    if (status == LUMA_OK && (g.buttons & LUMA_INPUT_BUTTON_A) != 0) {
        a_frames++;
        lt_fact("a_frames", a_frames);
    }
    LumaInputGame raw = {.size = sizeof(LumaInputGame)};
    if (input->game_raw(lt_mod, 0, &raw) == LUMA_OK && !same_game(&g, &raw)) {
        raw_differs++;
        lt_fact("raw_differs", raw_differs);
    }
    if (frames_seen == 1) lt_fact("running", 1);
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(input, LUMA_INPUT_NAME, "1.1"),
    LUMA_HOOK_ON("MarioActor::control", "void()", LUMA_AFTER, after),
};

static void begin(const LumaMod* mod) {
    lt_fact("watch_ok", input->watch_device(mod, on_device, 0) == LUMA_OK);
    kind_at_start = 99;
    lt_fact("start_device_status", input->device(mod, 0, &kind_at_start));
}
LT_START(begin(mod))
#elif CASE == 4
/* in-inject (input.inject 1.0, permission input.inject): stick up for 120 frames, then the player's own again. */
static const LumaInputInjectService* inject;
static LumaHandle held;
static long long frames_seen, injected_frames, up_frames, x_zero_frames, buttons_zero_frames, raw_free_frames;
static float start_x, start_y, start_z;
static const LumaFunction* player_pos; /* MR::getPlayerPos */

static const float* pos(void) {
    void* args[1] = {0};
    const float* got = 0;
    player_pos->invoke(args, &got);
    return got;
}

static int in_stage;

/* Mario's first control: the stage runs (the title's controller stays the player's). */
static int32_t after(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    in_stage = 1;
    return 0;
}

/* Once a frame (GameSystem::update), as the game reads its input once a frame: a world step's hook (MarioActor::control)
   runs 0 to 16 times a frame, and a frame with none would leave that frame's read at rest (inject's contract: a read
   is at rest unless the mod set it again). */
static int32_t every_frame(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (!in_stage) {
        return 0;
    }
    frames_seen++;
    if (frames_seen == 1) {
        /* Begun in the stage, not at the mod's start: the title's controller stays the player's. */
        lt_fact("begin_ok", inject->begin(lt_mod, 0, &held) == LUMA_OK && held != 0);
        LumaHandle again = 5;
        lt_fact("again_busy", inject->begin(lt_mod, 0, &again) == LUMA_E_BUSY && again == 0);
        LumaHandle past = 0;
        lt_fact("player2_refused", inject->begin(lt_mod, 2, &past) == LUMA_E_ARGUMENT);
        LumaInputGame small = {.size = 24};
        lt_fact("small_refused", inject->set(lt_mod, held, &small) == LUMA_E_ARGUMENT);
        lt_fact("zero_refused", inject->set(lt_mod, 0, &small) == LUMA_E_HANDLE);
        const float* p = pos();
        lt_fact("has_pos", p != 0);
        if (p != 0) {
            start_x = p[0];
            start_y = p[1];
            start_z = p[2];
        }
    }
    if (frames_seen <= 120) {
        LumaInputGame s = {.size = sizeof(LumaInputGame), .stick_y = 1.0f};
        if (frames_seen == 1) s.stick_x = __builtin_nanf(""); /* reads 0, noted, still OK */
        if (frames_seen == 2) s.buttons = 1u << 20;             /* past the game's buttons: dropped */
        const int32_t status = inject->set(lt_mod, held, &s);
        if (frames_seen == 1) lt_fact("set_nan_status", status);
        if (status != LUMA_OK) lt_fact("set_failed", frames_seen);
    }
    /* What the game read this frame (frame 1's read came before the first set). */
    if (frames_seen >= 3 && frames_seen <= 120) {
        LumaInputGame g = {.size = sizeof(LumaInputGame)};
        if (input->game(lt_mod, 0, &g) == LUMA_OK) {
            if (g.injected) injected_frames++;
            if (g.stick_y > 0.99f) up_frames++;
            if (g.stick_x == 0.0f) x_zero_frames++;
            if (g.buttons == 0u) buttons_zero_frames++;
        }
        LumaInputGame raw = {.size = sizeof(LumaInputGame)};
        if (input->game_raw(lt_mod, 0, &raw) == LUMA_OK && raw.injected == 0) raw_free_frames++;
    }
    if (frames_seen == 121) {
        lt_fact("injected_frames", injected_frames);
        lt_fact("up_frames", up_frames);
        lt_fact("x_zero_frames", x_zero_frames);
        lt_fact("buttons_zero_frames", buttons_zero_frames);
        lt_fact("raw_free_frames", raw_free_frames);
        const float* p = pos();
        if (p != 0) {
            const float dx = p[0] - start_x, dy = p[1] - start_y, dz = p[2] - start_z;
            lt_fact("moved_sq", (long long)(dx * dx + dy * dy + dz * dz));
        }
        lt_fact("end_ok", inject->end(lt_mod, held) == LUMA_OK);
        LumaInputGame s = {.size = sizeof(LumaInputGame)};
        lt_fact("stale_refused", inject->set(lt_mod, held, &s) == LUMA_E_HANDLE);
    }
    if (frames_seen == 124) {
        LumaInputGame g = {.size = sizeof(LumaInputGame)};
        lt_fact("after_end_status", input->game(lt_mod, 0, &g));
        lt_fact("after_end_injected", g.injected);
    }
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(input, LUMA_INPUT_NAME, "1.1"),
    LUMA_SERVICE(inject, LUMA_INPUT_INJECT_NAME, "1.0"),
    LUMA_FUNCTION(player_pos, "MR::getPlayerPos", "Vec3f *()"),
    LUMA_HOOK_ON("MarioActor::control", "void()", LUMA_AFTER, after),
    LUMA_HOOK_ON("GameSystem::update", "void()", LUMA_AFTER, every_frame),
};

static void begin(const LumaMod* mod) {
    (void)mod;
}
LT_START(begin(mod))
#elif CASE == 6
/* in-text (input 1.1 text_request, text_peek, text_cancel). */
static long long frames_seen, a_open, a_after;
static LumaHandle first, second, third;
static int third_done;
static long long events;

static void on_text(void* user, const LumaEvent* event) {
    (void)user;
    if (event->kind != LUMA_INPUT_EVENT_TEXT) return;
    events++;
    lt_fact("events", events);
    if (event->handle == first) lt_fact("first_status", event->status);
    if (event->handle == second) {
        lt_fact("second_status", event->status);
        lt_fact("second_data_null", event->data == 0 && event->data_size == 0);
    }
    if (event->handle == third) {
        /* The harness typed "-12.5a3x", Backspace, "9", Enter into a NUMBER request of 6: "-12.59". */
        static const char want[] = "-12.59";
        int same = event->data != 0 && event->data_size == 6;
        for (int i = 0; same && i < 6; i++) same = ((const char*)event->data)[i] == want[i];
        lt_fact("third_status", event->status);
        lt_fact("third_bytes", (long long)event->data_size);
        lt_fact("third_points", (long long)event->value);
        lt_fact("third_text_ok", same);
        third_done = 1;
    }
}

static long long length_of(const char* s) {
    long long n = 0;
    while (s[n] != 0) n++;
    return n;
}

static int32_t after(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    frames_seen++;
    if (frames_seen == 1) {
        const LumaInputTextRequest r = {.size = sizeof(LumaInputTextRequest), .kind = LUMA_INPUT_TEXT_NAME,
                                        .max_length = 8, .initial = "Gold Mario!"};
        lt_fact("first_request", input->text_request(lt_mod, &r, on_text, 0, &first));
        char buffer[64];
        uint32_t length = 0, caret = 0;
        lt_fact("peek_status", input->text_peek(lt_mod, first, buffer, sizeof(buffer), &length, &caret));
        lt_fact("peek_length", length);
        lt_fact("peek_caret", caret);
        lt_fact("peek_text_length", length_of(buffer));
        char small[4];
        uint32_t whole = 0;
        (void)input->text_peek(lt_mod, first, small, sizeof(small), &whole, 0);
        lt_fact("small_peek_length", length_of(small));
        lt_fact("small_peek_whole", whole);
    }
    if (frames_seen == 2) {
        /* A second request ends the first CANCELLED. */
        const LumaInputTextRequest r = {.size = sizeof(LumaInputTextRequest), .kind = LUMA_INPUT_TEXT_ANY,
                                        .max_length = 32};
        lt_fact("second_request", input->text_request(lt_mod, &r, on_text, 0, &second));
        lt_fact("first_peek_refused", input->text_peek(lt_mod, first, 0, 0, 0, 0) == LUMA_E_HANDLE);
    }
    LumaInputGame g = {.size = sizeof(LumaInputGame)};
    const int pressed = input->game(lt_mod, 0, &g) == LUMA_OK && (g.buttons & LUMA_INPUT_BUTTON_A) != 0;
    if (frames_seen > 2 && frames_seen < 300 && pressed) a_open++;
    if (frames_seen == 300) {
        lt_fact("a_while_open", a_open);
        lt_fact("cancel_ok", input->text_cancel(lt_mod, second) == LUMA_OK);
        lt_fact("cancel_again_refused", input->text_cancel(lt_mod, second) == LUMA_E_HANDLE);
    }
    if (frames_seen == 310) {
        const LumaInputTextRequest r = {.size = sizeof(LumaInputTextRequest), .kind = LUMA_INPUT_TEXT_NUMBER,
                                        .max_length = 6};
        lt_fact("third_request", input->text_request(lt_mod, &r, on_text, 0, &third));
    }
    if (third_done && pressed) {
        a_after++;
        lt_fact("a_after", a_after);
    }
    if (frames_seen == 1) lt_fact("running", 1);
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(input, LUMA_INPUT_NAME, "1.1"),
    LUMA_HOOK_ON("MarioActor::control", "void()", LUMA_AFTER, after),
};

static void begin(const LumaMod* mod) {
    LumaHandle h = 0;
    LumaInputTextRequest r = {.size = sizeof(LumaInputTextRequest), .kind = LUMA_INPUT_TEXT_ANY, .max_length = 0};
    lt_fact("zero_length_refused", input->text_request(mod, &r, on_text, 0, &h) == LUMA_E_ARGUMENT);
    r.max_length = LUMA_INPUT_TEXT_MOST + 1u;
    lt_fact("long_refused", input->text_request(mod, &r, on_text, 0, &h) == LUMA_E_ARGUMENT);
    r.max_length = 8;
    r.kind = 9;
    lt_fact("kind_refused", input->text_request(mod, &r, on_text, 0, &h) == LUMA_E_ARGUMENT);
    r.kind = LUMA_INPUT_TEXT_ANY;
    r.initial = "\xff";
    lt_fact("not_utf8_refused", input->text_request(mod, &r, on_text, 0, &h) == LUMA_E_ARGUMENT);
    r.initial = 0;
    lt_fact("null_fn_refused", input->text_request(mod, &r, 0, 0, &h) == LUMA_E_ARGUMENT);
    r.size = 8;
    lt_fact("small_refused", input->text_request(mod, &r, on_text, 0, &h) == LUMA_E_ARGUMENT);
}
LT_START(begin(mod))
#elif CASE == 7
/* in-dev (input.dev 1.0, permission input.devices): list and the refusals; no device is opened. */
static const LumaInputDevService* dev;
static const LumaImport imports[] = {LUMA_SERVICE(input, LUMA_INPUT_NAME, "1.1"), LUMA_SERVICE(dev, LUMA_INPUT_DEV_NAME, "1.0")};

static void on_report(void* user, const LumaEvent* event) {
    (void)user;
    (void)event;
}

static void begin(const LumaMod* mod) {
    uint32_t found = 999;
    lt_fact("list_status", dev->list(mod, 0, 0, &found));
    lt_fact("found_at_most", found <= LUMA_INPUT_DEVICES_MOST);
    if (found > 0) {
        LumaInputDeviceInfo one = {.size = sizeof(LumaInputDeviceInfo)};
        uint32_t again = 0;
        lt_fact("list_one_status", dev->list(mod, &one, 1, &again));
        lt_fact("first_path", one.path[0] != 0);
    }
    LumaInputDeviceInfo small = {.size = 8};
    lt_fact("small_refused", dev->list(mod, &small, 1, &found) == LUMA_E_ARGUMENT);
    lt_fact("null_found_refused", dev->list(mod, 0, 0, 0) == LUMA_E_ARGUMENT);
    LumaHandle h = 7;
    lt_fact("empty_path_refused", dev->open(mod, "", on_report, 0, &h) == LUMA_E_ARGUMENT);
    lt_fact("unknown_path", dev->open(mod, "\\\\?\\hid#vid_0000&pid_0000#nope", on_report, 0, &h));
    const unsigned char bytes[2] = {0, 1};
    lt_fact("write_handle_refused", dev->write(mod, 12345, bytes, 2) == LUMA_E_HANDLE);
    lt_fact("close_handle_refused", dev->close(mod, 12345) == LUMA_E_HANDLE);
}
LT_START(begin(mod))
#elif CASE == 8
/* in-rumble (input 1.1 rumble, on the harness's controller, test device pad): the steps call MR::getRandom(0, 2520 + k).
   k 1: the device is a PAD; the refusals (strength past 1, seconds below 0 and past 10, player 2: ARGUMENT); player 1
   (the co-star, no motor the host drives): ABSENT; then player 0 at 0.5 for 1 second. k 2: player 0 at 1.0 for 5
   seconds. k 3: strength 0 stops it. The harness's fact input.motor shows the motor: 128, 0 a second on, 255, 0. */
static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 2520;
    if (k < 1 || k > 3) return 0;
    if (k == 1) {
        uint32_t kind = 99;
        lt_fact("device_status", input->device(lt_mod, 0, &kind));
        lt_fact("device_kind", kind);
        lt_fact("strong_refused", input->rumble(lt_mod, 0, 1.5f, 1.0f) == LUMA_E_ARGUMENT);
        lt_fact("negative_refused", input->rumble(lt_mod, 0, 0.5f, -1.0f) == LUMA_E_ARGUMENT);
        lt_fact("long_refused", input->rumble(lt_mod, 0, 0.5f, 10.5f) == LUMA_E_ARGUMENT);
        lt_fact("player2_refused", input->rumble(lt_mod, 2, 0.5f, 1.0f) == LUMA_E_ARGUMENT);
        lt_fact("costar_status", input->rumble(lt_mod, 1, 0.5f, 1.0f));
        lt_fact("half_status", input->rumble(lt_mod, 0, 0.5f, 1.0f));
    }
    if (k == 2) lt_fact("full_status", input->rumble(lt_mod, 0, 1.0f, 5.0f));
    if (k == 3) lt_fact("stop_status", input->rumble(lt_mod, 0, 0.0f, 0.0f));
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(input, LUMA_INPUT_NAME, "1.1"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};
LT_START((void)mod)
#endif
LUMA_MOD(imports, start, 0);
