/* H's 2.1 conformance mods (services-registry 14), against the harness's test services test.svc 1.3 and test.secret
   1.0 (tools/test_driver/extension/luma_test_services.h). One source, one case each (-DCASE=n):
   reg-svc13      (1)  2101: import test.svc "1.3": bound, the table's size, major 1, minor 3, echo 41 -> 42.
   reg-svc11      (2)  2102: import "1.1": bound to the same table core->service gives for 1.3.
   reg-svc14      (3)  2103: import "1.4": refused (L505 Name.Service, test.svc, 1.4).
   reg-svc20      (4)  2104: import "2.0": refused (L505, 2.0).
   reg-secret     (5)  2105, 2106: import test.secret "1.0", declared; refused when not granted, bound when granted.
   reg-opt        (6)  2107: LUMA_OPTIONAL import of "1.4": starts, slot null.
   reg-ask        (7)  2108: core->service(mod, "test.secret", 1, 0) not granted: null; Hook.ServiceNotGranted said.
   reg-thread     (8)  2109: echo off the game thread twice (harness other_thread): -2 both; Service.Thread said once.
   reg-events     (9)  2110: 300 events posted from the service's thread inside one frame: 256 the next frame, 44 the
                           one after, in post order.
   reg-twice      (10) 2111: a node posted twice before the frame: one event.
   reg-again      (11) 2112: a node posted again from its delivery: one event a frame, two frames.
   reg-order-a/b  (12) 2113: two mods, one event each in the same frame, the lower one's posted first: the higher
                           mod's given first (list order, not post order). -DACT_K: the step that posts for this mod.
   reg-struct     (13) 2116: take_struct with size 4: -1; Service.StructSize with 4 and 16 said.
   reg-takeout    (14) 2114: 10 events posted at k 0; the run takes the mod out before the next frame's deliveries.
   reg-watch      (15) 2114: at k 7 test.svc's stats of reg-takeout (posted, delivered, dropped, the moments' order).
   Run steps call MR::getRandom(0, 4260 + k), and the hook gives 1: k 0 acts, k 1, 3 and 4 read the events given so
   far (facts "given_1", "given_2", "given_3": one key per reading), k 2 reads 2113's order, k 5 and 6 post for
   2113's two mods (each acts on its own ACT_K only). */
#include "../loader/lt.h"
#include <luma_harness.h>
#include "../../../../tools/test_driver/extension/luma_test_services.h"

#ifndef CASE
#error CASE
#endif
#if CASE == 12 && !defined(ACT_K)
#error ACT_K
#endif

/* Each case uses some of these; the rest are unused in it. */
__attribute__((unused)) static const LumaTestSvcService* svc;
__attribute__((unused)) static const LumaTestSecretService* secret;
__attribute__((unused)) static const void* harness_table;

/* Events: how many this mod was given, whether they came in post order, and the counts the steps read. */
static int32_t given;
static int32_t in_order = 1;
static int64_t next_value;

__attribute__((unused)) static void on_event(void* user, const LumaEvent* event) {
    (void)user;
    if (event->kind != LUMA_TEST_SVC_EVENT_POSTED || event->status != LUMA_OK || (int64_t)event->value != next_value) {
        in_order = 0;
    }
    next_value = (int64_t)event->value + 1;
    given++;
}

#if CASE == 8
static int32_t off_first = 1, off_second = 1;
static void off_thread(void* user) {
    (void)user;
    uint64_t out = 0;
    off_first = svc->echo(lt_mod, 1, &out);
    off_second = svc->echo(lt_mod, 1, &out);
}
#endif

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4260;
    if (k < 0 || k > 9) return 0;
#if CASE == 8
    if (k == 0) {
        const LumaHarness* harness = (const LumaHarness*)harness_table;
        lt_fact("made", harness != 0 && harness->size >= sizeof(LumaHarness) ? harness->other_thread(off_thread, 0) : 0);
        lt_fact("off_first", off_first);
        lt_fact("off_second", off_second);
    }
#elif CASE == 9
    if (k == 0) lt_fact("post", svc->post_events(lt_mod, 300, on_event, 0));
#elif CASE == 10
    if (k == 0) lt_fact("post", svc->post_twice(lt_mod, on_event, 0));
#elif CASE == 11
    if (k == 0) lt_fact("post", svc->post_again(lt_mod, on_event, 0));
#elif CASE == 12
    if (k == ACT_K) lt_fact("post", svc->post_events(lt_mod, 1, on_event, 0));
    if (k == 2) {
        /* Each asks for both: the order of the two mods' latest deliveries. */
        LumaTestSvcStats a = {.size = sizeof(LumaTestSvcStats)};
        LumaTestSvcStats b = {.size = sizeof(LumaTestSvcStats)};
        svc->stats(lt_mod, "reg-order-a", &a);
        svc->stats(lt_mod, "reg-order-b", &b);
        lt_fact("a_first", a.delivered_seq != 0 && b.delivered_seq != 0 && a.delivered_seq < b.delivered_seq);
    }
#elif CASE == 14
    if (k == 0) lt_fact("post", svc->post_events(lt_mod, 10, on_event, 0));
#elif CASE == 15
    if (k == 7) {
        LumaTestSvcStats t = {.size = sizeof(LumaTestSvcStats)};
        lt_fact("stats", svc->stats(lt_mod, "reg-takeout", &t));
        lt_fact("known", t.known);
        lt_fact("posted", (long long)t.posted);
        lt_fact("delivered", (long long)t.delivered);
        lt_fact("dropped", (long long)t.dropped);
        lt_fact("after_gone", (long long)t.after_gone);
        lt_fact("gone_before_end", t.gone_seq != 0 && t.end_seq != 0 && t.begin_seq < t.gone_seq && t.gone_seq < t.end_seq);
    }
#endif
    /* One key per reading, at consecutive frames: the same three readings whether a step runs before or after the
       frame's deliveries (audits/2.1-review.md J21-2), so a frame that stopped at the limit and the rest given in the
       very next frame are both proven. */
    if (k == 1) lt_fact("given_1", given);
    if (k == 3) lt_fact("given_2", given);
    if (k == 4) {
        lt_fact("given_3", given);
        lt_fact("in_order", in_order);
    }
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

#if CASE == 1 || (CASE >= 8 && CASE <= 15)
#define SVC_VERSION "1.3"
#elif CASE == 2
#define SVC_VERSION "1.1"
#elif CASE == 3 || CASE == 6
#define SVC_VERSION "1.4"
#elif CASE == 4
#define SVC_VERSION "2.0"
#endif

static const LumaImport imports[] = {
#if CASE == 5
    LUMA_SERVICE(secret, "test.secret", "1.0"),
#elif CASE == 6
    {LUMA_IMPORT_SERVICE, LUMA_OPTIONAL, "test.svc", SVC_VERSION, &svc, 0, 0, 0},
#elif CASE != 7
    LUMA_SERVICE(svc, "test.svc", SVC_VERSION),
#endif
#if CASE == 8
    LUMA_SERVICE(harness_table, LUMA_HARNESS_NAME, "1.1"),
#endif
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({
#if CASE == 1
    uint64_t out = 0;
    lt_fact("slot", svc != 0);
    lt_fact("size", svc->base.size == sizeof(LumaTestSvcService));
    lt_fact("major", svc->base.major);
    lt_fact("minor", svc->base.minor);
    lt_fact("echo", svc->echo(mod, 41, &out));
    lt_fact("out", (long long)out);
#elif CASE == 2
    lt_fact("slot", svc != 0);
    lt_fact("same", svc == core->service(mod, "test.svc", 1, 3));
#elif CASE == 5
    lt_fact("ping", secret->ping(mod));
#elif CASE == 6
    lt_fact("slot", svc != 0);
#elif CASE == 7
    lt_fact("asked", core->service(mod, "test.secret", 1, 0) != 0);
#elif CASE == 13
    /* No commas inside LT_START's argument: one designated field each, the rest zero. */
    LumaTestSvcStruct small = {.size = 4};
    lt_fact("small", svc->take_struct(mod, &small));
    LumaTestSvcStruct whole = {.size = sizeof(LumaTestSvcStruct)};
    lt_fact("whole", svc->take_struct(mod, &whole));
#endif
})
/* A stop, so a take-out proves it runs (reload.md 3: takeout.<id> "stopped" is true once its stop ran). */
static void stop(void) {}

LUMA_MOD(imports, start, stop);
