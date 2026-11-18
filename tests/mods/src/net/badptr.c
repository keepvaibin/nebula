/* 2711-bad-pointers (M-029; services-net): net-badptr (permission net.local) gives net.local every pointer it reads,
   writes or calls wrong (lt_bad.h) at k 1: each LUMA_E_ARGUMENT, reported; "none_written" 1 when no out was written;
   a good listener after ("listen_good"), its port refused into read-only memory, then taken ("port_good"), closed.
   Run steps call MR::getRandom(0, 4500 + k) and the hook gives 1. */
#include "../loader/lt.h"
#include "../loader/lt_bad.h"
#include <luma_net.h>

static const LumaNetLocalService* net;

static void on_event(void* user, const LumaEvent* event) {
    (void)user;
    (void)event;
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4500;
    if (k != 1) return 0;
    LumaHandle none = 7;
    LumaNetHttpRequest request = {.size = sizeof(LumaNetHttpRequest), .url = "http://127.0.0.1:9/"};
    lt_fact("connect_host_unmapped", net->tcp_connect(lt_mod, (const char*)LT_UNMAPPED, 9, on_event, 0, &none));
    lt_fact("connect_fn_data", net->tcp_connect(lt_mod, "127.0.0.1", 9, (LumaEventFn)(uintptr_t)LT_DATA_FN, 0, &none));
    lt_fact("connect_out_read_only", net->tcp_connect(lt_mod, "127.0.0.1", 9, on_event, 0, (LumaHandle*)LT_READ_ONLY));
    lt_fact("listen_fn_game", net->tcp_listen(lt_mod, 0, 0, (LumaEventFn)(uintptr_t)net->close, 0, &none));
    lt_fact("listen_out_unmapped", net->tcp_listen(lt_mod, 0, 0, on_event, 0, (LumaHandle*)LT_UNMAPPED));
    lt_fact("send_data_unmapped", net->send(lt_mod, 12345u, LT_UNMAPPED, 16));
    lt_fact("udp_out_read_only", net->udp_open(lt_mod, 0, 0, on_event, 0, (LumaHandle*)LT_READ_ONLY));
    lt_fact("send_to_host_unmapped", net->send_to(lt_mod, 12345u, (const char*)LT_UNMAPPED, 9, "x", 1));
    lt_fact("send_to_data_unmapped", net->send_to(lt_mod, 12345u, "127.0.0.1", 9, LT_UNMAPPED, 16));
    lt_fact("ipc_name_unmapped", net->ipc_connect(lt_mod, (const char*)LT_UNMAPPED, on_event, 0, &none));
    lt_fact("tls_certificate_unmapped",
            net->tls_listen(lt_mod, 0, 0, (const char*)LT_UNMAPPED, "certs/key.pem", on_event, 0, &none));
    lt_fact("http_request_unmapped", net->http(lt_mod, (const LumaNetHttpRequest*)LT_UNMAPPED, on_event, 0, &none));
    request.url = (const char*)LT_UNMAPPED;
    lt_fact("http_url_unmapped", net->http(lt_mod, &request, on_event, 0, &none));
    request.url = "http://127.0.0.1:9/";
    request.headers = (const LumaNetHeader*)LT_UNMAPPED;
    request.header_count = 1;
    lt_fact("http_headers_unmapped", net->http(lt_mod, &request, on_event, 0, &none));
    request.headers = 0;
    request.header_count = 0;
    request.body = LT_UNMAPPED;
    request.body_bytes = 16;
    lt_fact("http_body_unmapped", net->http(lt_mod, &request, on_event, 0, &none));
    lt_fact("ws_url_unmapped", net->ws_connect(lt_mod, (const char*)LT_UNMAPPED, 0, on_event, 0, &none));
    lt_fact("none_written", none == 7);
    LumaHandle listener = 0;
    uint16_t port = 0;
    lt_fact("listen_good", net->tcp_listen(lt_mod, 0, 0, on_event, 0, &listener));
    lt_fact("port_read_only", net->port(lt_mod, listener, (uint16_t*)LT_READ_ONLY));
    lt_fact("port_good", net->port(lt_mod, listener, &port));
    lt_fact("port_given", port != 0);
    lt_fact("close_good", net->close(lt_mod, listener));
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(net, LUMA_NET_LOCAL_NAME, "1.4"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
