/* H's services-system conformance mods (contract services-system 11), against the host's system services 1.0
   (native/include/luma_system.h). One source, one case each (-DCASE=n):
   sys-facts          (1)  2221, 2222: facts and display at k 1.
   sys-struct         (2)  2223: facts and display with structs of 8 bytes at k 1.
   sys-clip-text      (3)  2225, 2228: set_text "Luma ✓ 星" at k 1; get_text at k 2 (two takes in the event's call);
                            at k 3 a take after the call and a set_text that is not UTF-8.
   sys-clip-image     (4)  2226: set_image 3x2 with 23 bytes, then with 24, at k 1; get_image at k 2.
   sys-clip-empty     (5)  2227: get_text and get_image on the empty clipboard at k 1.
   sys-clip-limit     (6)  2229: five get_text at k 1.
   sys-clip-cancel    (7)  2230: get_text and cancel at k 1, cancel again; the events counted at k 3.
   sys-clip-twice     (8)  2233: set_text at k 1, get_text at k 2 and at k 3.
   sys-links          (9)  2231: open example.org at k 1 and again at once; example.com at k 2; example.org at k 3.
   sys-links-bad      (10) 2231b: three links the service does not open at k 1.
   sys-clip-declined  (11) 2224: imports system.clipboard (declared; the test declines it): never starts.
   sys-clip-undeclared(11) 2224b: the same code without the permission declared: L506.
   sys-links-declined (12) 2232: imports system.links (declared; declined): never starts.
   sys-links-undeclared(12) 2232b: the same without the permission: L506.
   sys-takeout        (13) 2234: at k 1 set_text, get_text and a link (all answered at once in deterministic mode,
                            their events queued); the run takes the mod out before the next frame's deliveries.
   Run steps call MR::getRandom(0, 4240 + k) and the hook gives 1. Facts are numbers (lt.h); texts are compared here. */
#include "../loader/lt.h"
#include <luma_system.h>

#ifndef CASE
#error CASE
#endif

__attribute__((unused)) static const LumaSystemService* sys;
__attribute__((unused)) static const LumaSystemClipboardService* clip;
__attribute__((unused)) static const LumaSystemLinksService* links;

__attribute__((unused)) static int32_t same(const char* a, const char* b) {
    while (*a != 0 && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

__attribute__((unused)) static int32_t length(const char* a) {
    int32_t n = 0;
    while (a[n] != 0) n++;
    return n;
}

/* "Luma ✓ 星": 5 + 3 + 1 + 3 bytes. */
__attribute__((unused)) static const char kText[] = "Luma \xE2\x9C\x93 \xE6\x98\x9F";
__attribute__((unused)) static const uint8_t kPixels[24] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255,
                                                           255, 255, 255, 128, 0, 0, 0, 0, 10, 20, 30, 40};
__attribute__((unused)) static LumaHandle handle, last_handle;
__attribute__((unused)) static int32_t events;

__attribute__((unused)) static void on_event(void* user, const LumaEvent* event) {
    (void)user;
    events++;
    lt_fact("ev_kind", event->kind);
    lt_fact("ev_status", event->status);
    last_handle = event->handle;
#if CASE == 3 || CASE == 8
    if (event->kind == LUMA_SYSTEM_CLIPBOARD_EVENT_TEXT && event->status == LUMA_OK) {
        lt_fact("ev_value", (long long)event->value);
        char small[5];
        lt_fact("take", clip->take_text(lt_mod, event->handle, small, sizeof(small)));
        lt_fact("take_head", small[0] == 'L' && small[1] == 'u' && small[2] == 'm' && small[3] == 'a' && small[4] == 0);
        char again[32];
        lt_fact("take_again", clip->take_text(lt_mod, event->handle, again, sizeof(again)));
    }
#elif CASE == 4
    if (event->kind == LUMA_SYSTEM_CLIPBOARD_EVENT_IMAGE && event->status == LUMA_OK) {
        lt_fact("ev_width", (long long)(event->value >> 32));
        lt_fact("ev_height", (long long)(event->value & 0xFFFFFFFFull));
        uint8_t got[24];
        lt_fact("take_small", clip->take_image(lt_mod, event->handle, got, 23));
        lt_fact("take", clip->take_image(lt_mod, event->handle, got, sizeof(got)));
        int32_t equal = 1;
        for (int i = 0; i < 24; i++) equal = equal && got[i] == kPixels[i];
        lt_fact("take_same", equal);
    }
#endif
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4240;
    if (k < 0 || k > 9) return 0;
#if CASE == 1
    if (k == 1) {
        LumaSystemFacts f = {.size = sizeof(LumaSystemFacts)};
        lt_fact("facts", sys->facts(lt_mod, &f));
        lt_fact("platform", f.platform);
        lt_fact("arch", f.arch);
        lt_fact("renderer", f.renderer);
        lt_fact("region", f.region);
        lt_fact("lumacore_major", f.lumacore_major);
        lt_fact("lumacore_minor", f.lumacore_minor);
        lt_fact("clipboard_images", f.clipboard_images);
        lt_fact("game_id_length", length(f.game_id));
        lt_fact("game_id_region", f.game_id[3]);
        /* The language's folder starts with its release's: Us, Eu or Jp (a pack's language may be another's). */
        lt_fact("language_length", length(f.language));
        lt_fact("version_length", length(f.game_version));
        lt_fact("locale_en_us", same(f.locale, "en-US"));
        lt_fact("os_empty", f.os[0] == 0);
        lt_fact("gpu_empty", f.gpu[0] == 0);
        LumaSystemDisplay d = {.size = sizeof(LumaSystemDisplay)};
        lt_fact("display", sys->display(lt_mod, &d));
        lt_fact("width_some", d.width > 0);
        lt_fact("height_some", d.height > 0);
        lt_fact("safe_inside", d.safe_left + d.safe_right < d.width && d.safe_top + d.safe_bottom < d.height);
        lt_fact("changes_some", d.changes >= 1);
        lt_fact("focused", d.focused);
        lt_fact("scale_some", d.scale > 0);
    }
#elif CASE == 2
    if (k == 1) {
        LumaSystemFacts f = {.size = 8};
        lt_fact("facts_8", sys->facts(lt_mod, &f));
        LumaSystemDisplay d = {.size = 8};
        lt_fact("display_8", sys->display(lt_mod, &d));
    }
#elif CASE == 3
    if (k == 1) lt_fact("set", clip->set_text(lt_mod, kText, sizeof(kText) - 1, on_event, 0, &handle));
    if (k == 2) lt_fact("get", clip->get_text(lt_mod, on_event, 0, &handle));
    if (k == 3) {
        char after[8];
        lt_fact("take_after", clip->take_text(lt_mod, last_handle, after, sizeof(after)));
        static const char bad[] = {(char)0xC3, (char)0x28};
        lt_fact("set_bad", clip->set_text(lt_mod, bad, sizeof(bad), on_event, 0, &handle));
        lt_fact("events", events);
    }
#elif CASE == 4
    if (k == 1) {
        lt_fact("set_bad_size", clip->set_image(lt_mod, kPixels, 23, 3, 2, on_event, 0, &handle));
        lt_fact("set", clip->set_image(lt_mod, kPixels, sizeof(kPixels), 3, 2, on_event, 0, &handle));
    }
    if (k == 2) lt_fact("get", clip->get_image(lt_mod, on_event, 0, &handle));
#elif CASE == 5
    if (k == 1) {
        lt_fact("get_text", clip->get_text(lt_mod, on_event, 0, &handle));
        lt_fact("get_image", clip->get_image(lt_mod, on_event, 0, &handle));
    }
    if (k == 2) lt_fact("events", events);
#elif CASE == 6
    if (k == 1) {
        int32_t made = 0;
        int32_t last = 0;
        for (int i = 0; i < 5; i++) {
            LumaHandle h = 0;
            last = clip->get_text(lt_mod, on_event, 0, &h);
            if (last == LUMA_PENDING && h != 0) made++;
        }
        lt_fact("made", made);
        lt_fact("last", last);
    }
#elif CASE == 7
    if (k == 1) {
        lt_fact("get", clip->get_text(lt_mod, on_event, 0, &handle));
        lt_fact("cancel_1", clip->cancel(lt_mod, handle));
        lt_fact("cancel_2", clip->cancel(lt_mod, handle));
    }
    if (k == 3) lt_fact("events", events);
#elif CASE == 8
    if (k == 1) lt_fact("set", clip->set_text(lt_mod, "a", 1, on_event, 0, &handle));
    if (k == 2) lt_fact("get_1", clip->get_text(lt_mod, on_event, 0, &handle));
    if (k == 3) lt_fact("get_2", clip->get_text(lt_mod, on_event, 0, &handle));
#elif CASE == 9
    if (k == 1) {
        lt_fact("open_1", links->open(lt_mod, "https://example.org/luma", on_event, 0, &handle));
        LumaHandle again = 0;
        lt_fact("open_again", links->open(lt_mod, "https://example.org/luma", on_event, 0, &again));
    }
    if (k == 2) lt_fact("open_2", links->open(lt_mod, "https://example.com/", on_event, 0, &handle));
    if (k == 3) lt_fact("open_3", links->open(lt_mod, "https://Example.org/other", on_event, 0, &handle));
    if (k == 4) lt_fact("events", events);
#elif CASE == 10
    if (k == 1) {
        lt_fact("ftp", links->open(lt_mod, "ftp://x", on_event, 0, &handle));
        lt_fact("userinfo", links->open(lt_mod, "https://a@example.org/", on_event, 0, &handle));
        lt_fact("unicode_host", links->open(lt_mod, "https://b\xC3\xBC" "cher.example/", on_event, 0, &handle));
    }
    if (k == 2) lt_fact("events", events);
#elif CASE == 13
    if (k == 1) {
        lt_fact("set", clip->set_text(lt_mod, "a", 1, on_event, 0, &handle));
        lt_fact("get", clip->get_text(lt_mod, on_event, 0, &handle));
        lt_fact("open", links->open(lt_mod, "https://example.org/luma", on_event, 0, &handle));
    }
#endif
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
#if CASE == 1 || CASE == 2
    LUMA_SERVICE(sys, LUMA_SYSTEM_NAME, "1.0"),
#elif CASE >= 3 && CASE <= 8 || CASE == 11
    LUMA_SERVICE(clip, LUMA_SYSTEM_CLIPBOARD_NAME, "1.0"),
#elif CASE == 9 || CASE == 10 || CASE == 12
    LUMA_SERVICE(links, LUMA_SYSTEM_LINKS_NAME, "1.0"),
#elif CASE == 13
    LUMA_SERVICE(clip, LUMA_SYSTEM_CLIPBOARD_NAME, "1.0"),
    LUMA_SERVICE(links, LUMA_SYSTEM_LINKS_NAME, "1.0"),
#endif
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
/* A stop, so a take-out proves it runs (reload.md 3: takeout.<id> "stopped" is true once its stop ran). */
static void stop(void) {}

LUMA_MOD(imports, start, stop);
