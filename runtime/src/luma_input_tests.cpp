// SPDX-License-Identifier: GPL-3.0-only
// input 1.4's look (luma_input.cpp) with a stand-in kernel: every read's turn reaches each reading mod once, however
// many reads of the controller a frame has (a world rate above real time reads again for each extra step) and however
// often the mod reads (M-060). A rumble lasts its seconds of steady time, whatever the number of reads (N-4001).
#include "galaxy/luma_input_service.h"

#include <luma_input.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

int failed = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failed;
    }
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-6f; }

// Two mods the stand-in kernel knows; any other pointer is unknown.
int mod_a = 0;
int mod_b = 0;
const LumaMod* const A = reinterpret_cast<const LumaMod*>(&mod_a);
const LumaMod* const B = reinterpret_cast<const LumaMod*>(&mod_b);

const char* k_mod_id(const LumaMod* mod) { return mod == A ? "a" : mod == B ? "b" : nullptr; }
int32_t k_mod_order(const LumaMod* mod) { return mod == A ? 0 : mod == B ? 1 : -1; }
int32_t k_granted(const LumaMod*, const char*) { return 0; }
int32_t k_mod_live(const LumaMod* mod) { return k_mod_id(mod) != nullptr ? 1 : 0; }
int32_t k_game_thread() { return 1; }
int reports = 0;
void k_report(const LumaMod*, const char*, uint32_t, const char* const*, uint32_t) { ++reports; }
int32_t k_post(LumaEventNode*) { return LUMA_OK; }
int32_t k_call(const LumaMod*, const char*, LumaEventFn, void*, const LumaEvent*) { return LUMA_OK; }
void* k_alloc(const LumaMod*, uint64_t, uint64_t) { return nullptr; }
void k_free(const LumaMod*, void*) {}
void k_unknown(const char*) {}

const LumaServiceKernel kKernel = {sizeof(LumaServiceKernel), k_mod_id, k_mod_order, k_granted, k_mod_live, k_game_thread,
                                   k_report, k_post, k_call, k_alloc, k_free, k_unknown};

void log_line(const char*) {}

std::uint64_t fake_now = 0;
std::uint64_t fake_clock() { return fake_now; }

// One read of the main player's controller after the live controls gathered `yaw` (radians) with the look holding.
void read_after(float yaw) {
    galaxy::luma_input::look_sample(yaw, 0.0f, LUMA_INPUT_DEVICE_KEYBOARD_MOUSE, true);
    NebulaInputSample sample{};
    galaxy::luma_input::game_read(0u, &sample, LUMA_INPUT_DEVICE_KEYBOARD_MOUSE, false);
}

LumaInputLook look_of(const LumaInputService* input, const LumaMod* mod) {
    LumaInputLook look{};
    look.size = sizeof(look);
    check(input->look(mod, LUMA_INPUT_PAD_MAIN, &look) == LUMA_OK, "look answers LUMA_OK");
    return look;
}

}  // namespace

int main() {
    const LumaServiceProvider* provider = nullptr;
    for (const LumaServiceProvider* p : galaxy::luma_input::providers(log_line, nullptr)) {
        if (std::strcmp(p->name, LUMA_INPUT_NAME) == 0) {
            provider = p;
        }
    }
    check(provider != nullptr, "the input provider is there");
    if (provider == nullptr) {
        return 1;
    }
    const auto* input = reinterpret_cast<const LumaInputService*>(provider->table);
    provider->registered(provider->user, &kKernel);
    provider->mod_begin(provider->user, A);
    provider->mod_begin(provider->user, B);

    check(input->look_mode(A, LUMA_INPUT_PAD_MAIN, 1, 2.0f) == LUMA_OK, "look_mode on");
    // Each mod's first look starts it: no turn yet.
    check(near(look_of(input, A).yaw, 0.0f), "a's first look has no turn");
    check(near(look_of(input, B).yaw, 0.0f), "b's first look has no turn");

    // One frame with three reads (a world rate of 3): a reads once at the frame's end, and gets all three turns.
    read_after(0.01f);
    read_after(0.02f);
    read_after(0.03f);
    LumaInputLook a = look_of(input, A);
    check(near(a.yaw, 0.12f), "a: the three reads' turn, times the scale 2");
    check(a.on == 1u && a.device == LUMA_INPUT_DEVICE_KEYBOARD_MOUSE, "a: the look holds, by the mouse");
    check(near(look_of(input, A).yaw, 0.0f), "a: read again with no read between, no turn");

    // b reads after every read: each turn once, the same sum.
    read_after(0.05f);
    check(near(look_of(input, B).yaw, 0.12f + 0.10f), "b: every turn since its last look");
    read_after(0.04f);
    check(near(look_of(input, B).yaw, 0.08f), "b: the one read since");
    check(near(look_of(input, A).yaw, 0.10f + 0.08f), "a: both reads since its last look");

    // Off: what was gathered is dropped; on again starts from still.
    read_after(0.5f);
    check(input->look_mode(A, LUMA_INPUT_PAD_MAIN, 0, 1.0f) == LUMA_OK, "look_mode off");
    check(input->look_mode(A, LUMA_INPUT_PAD_MAIN, 1, 1.0f) == LUMA_OK, "look_mode on again");
    check(near(look_of(input, A).yaw, 0.0f), "a: the turn before off is gone");
    read_after(0.25f);
    check(near(look_of(input, A).yaw, 0.25f), "a: a read after on again");

    // Rumble: half a second of steady time. Three hundred reads in no time (a world rate far above real time, or a
    // frame rate far above 60) do not end it; the time does, whether reads come or not.
    galaxy::luma_input::set_clock_for_tests(fake_clock);
    fake_now = 1000000000;
    NebulaInputSample sample{};
    galaxy::luma_input::game_read(0u, &sample, LUMA_INPUT_DEVICE_KEYBOARD_MOUSE, true);
    check(input->rumble(A, 0u, 0.5f, 0.5f) == LUMA_OK, "rumble answers LUMA_OK");
    for (int i = 0; i < 300; i++) {
        galaxy::luma_input::game_read(0u, &sample, LUMA_INPUT_DEVICE_KEYBOARD_MOUSE, true);
    }
    check(galaxy::luma_input::mod_rumble(0u) == 128u, "rumble: 300 reads in no time do not end it");
    fake_now += 499000000;
    check(galaxy::luma_input::mod_rumble(0u) == 128u, "rumble: on at 0.499 s");
    fake_now += 2000000;
    check(galaxy::luma_input::mod_rumble(0u) == 0u, "rumble: off at 0.501 s, with no read between");

    // The mods end as the kernel ends them: what the service kept for each (its look and rumble) goes with it, so the
    // sanitizers' leak check finds nothing left (it found two records of 112 bytes while the mods were left running).
    check(input->rumble(B, 0u, 1.0f, 1.0f) == LUMA_OK, "b's rumble answers LUMA_OK");
    check(galaxy::luma_input::mod_rumble(0u) == 255u, "b's rumble on");
    provider->mod_end(provider->user, A);
    provider->mod_end(provider->user, B);
    check(galaxy::luma_input::mod_rumble(0u) == 0u, "no rumble once b has ended");
    galaxy::luma_input::set_clock_for_tests(nullptr);

    check(reports == 0, "no mistake reported");
    if (failed == 0) {
        std::printf("luma_input tests passed\n");
    }
    return failed == 0 ? 0 : 1;
}
