/* 2712-net-ws-ping-flood (M-055; services-net 8, net 1.4). net-wsflood serves WebSocket itself on a tcp_listen and is
   its own client over ws://127.0.0.1:<port>/flood. Run steps call MR::getRandom(0, 4520 + k).
   k 1: listen, connect (LUMA_PENDING). The server answers the upgrade, then sends PINGS pings of 125 bytes each (a
   peer may ping as often as it likes, RFC 6455 5.5.2) and never reads what the client sends back; after the last
   ping, a text "done".
   The client's service answers each ping with a pong, which the server leaves unread: once the system's buffers are
   full, the pongs wait in the client's send queue. "done": the client got "done" (every ping read).
   k 2: "pings_sent", the pings the server queued (PINGS: its sends fit the mod's send budget); "big_send", the
   client's ws_send of 3 MiB (LUMA_OK: the pongs waiting take at most one control frame of the queue, so the mod's
   own sends keep the budget, RFC 6455 5.5.3's "only the most recent" ping answered); "others", events not
   expected. */
#include "../loader/lt.h"
#include <luma_net.h>

static const LumaNetLocalService* net;

#include "ws_accept.h"

#define PINGS 200000u
#define PING_BYTES 125u
#define FRAME_BYTES (2u + PING_BYTES)
#define BATCH (8256u) /* frames in one send: 1048512 bytes */
#define BIG (3u << 20u)

static LumaHandle listener, server_conn, client;
static uint16_t port;
static char head_in[4096];
static uint32_t head_have;
static int32_t upgraded, done, done_sent, others;
static uint32_t pings_sent;
static uint8_t batch[BATCH * FRAME_BYTES];
static uint8_t big[BIG];

static void fill_batch(void) {
    for (uint32_t i = 0; i < BATCH; i++) {
        uint8_t* f = batch + i * FRAME_BYTES;
        f[0] = 0x89u; /* FIN, ping */
        f[1] = (uint8_t)PING_BYTES;
        for (uint32_t j = 0; j < PING_BYTES; j++) f[2 + j] = (uint8_t)('a' + (i + j) % 26u);
    }
}

/* Up to three batches (3 MiB) a turn, the next turn at the server's SENT (its queue sent): the server's own sends
   stay under the mod's 4 MiB budget while the pongs do. */
static void pump(void) {
    for (int turn = 0; turn < 3 && pings_sent < PINGS; turn++) {
        const uint32_t n = PINGS - pings_sent < BATCH ? PINGS - pings_sent : BATCH;
        if (net->send(lt_mod, server_conn, batch, n * FRAME_BYTES) != LUMA_OK) return;
        pings_sent += n;
    }
    if (pings_sent == PINGS && !done_sent) {
        const uint8_t text[6] = {0x81u, 4u, 'd', 'o', 'n', 'e'};
        done_sent = net->send(lt_mod, server_conn, text, 6u) == LUMA_OK;
    }
}

static void answer_upgrade(uint32_t n) {
    char key[64];
    uint32_t key_n = 0;
    for (uint32_t i = 0; i + 19u < n; i++) {
        if (starts(head_in + i, n - i, "Sec-WebSocket-Key: ")) {
            uint32_t j = i + 19u;
            while (j < n && head_in[j] != '\r' && key_n < 60u) key[key_n++] = head_in[j++];
            break;
        }
    }
    key[key_n] = 0;
    char keyed[128];
    uint32_t k = put(keyed, 0, key);
    k = put(keyed, k, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    uint8_t digest[20];
    sha1((const uint8_t*)keyed, k, digest);
    char accept[32];
    base64(digest, 20u, accept);
    char out[256];
    uint32_t o = put(out, 0, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n");
    o = put(out, o, "Sec-WebSocket-Accept: ");
    o = put(out, o, accept);
    o = put(out, o, "\r\n\r\n");
    (void)net->send(lt_mod, server_conn, out, o);
    upgraded = 1;
    pump();
}

static void on_event(void* user, const LumaEvent* event) {
    (void)user;
    const LumaHandle h = event->handle;
    if (h == listener) {
        if (event->kind == LUMA_NET_EVENT_ACCEPTED && server_conn == 0) server_conn = event->value;
        else others++;
        return;
    }
    if (h == server_conn) {
        if (event->kind == LUMA_NET_EVENT_DATA && !upgraded) {
            const int64_t got = net->take(lt_mod, h, head_in + head_have, sizeof(head_in) - head_have);
            if (got > 0) head_have += (uint32_t)got;
            for (uint32_t i = 0; i + 3u < head_have; i++) {
                if (head_in[i] == '\r' && head_in[i + 1] == '\n' && head_in[i + 2] == '\r' && head_in[i + 3] == '\n') {
                    answer_upgrade(i + 4u);
                    break;
                }
            }
        } else if (event->kind == LUMA_NET_EVENT_SENT && upgraded) {
            pump();
        } else if (event->kind != LUMA_NET_EVENT_DATA && event->kind != LUMA_NET_EVENT_SENT) {
            others++; /* after the upgrade the server reads nothing: the pongs stay unread */
        }
        return;
    }
    if (h == client) {
        if (event->kind == LUMA_NET_EVENT_MESSAGE && event->flags == LUMA_NET_WS_TEXT && event->value == 4u &&
            same(event->data, "done", 4u)) {
            done = 1;
            lt_fact("done", 1);
        } else if (event->kind != LUMA_NET_EVENT_CONNECTED && event->kind != LUMA_NET_EVENT_SENT) {
            others++;
        }
        return;
    }
    others++;
}

static void step(int32_t k) {
    if (k == 1) {
        fill_batch();
        lt_fact("listen", net->tcp_listen(lt_mod, 0, 0, on_event, 0, &listener));
        (void)net->port(lt_mod, listener, &port);
        char url[64];
        uint32_t o = put(url, 0, "ws://127.0.0.1:");
        char digits[6];
        uint32_t d = 0;
        uint32_t p = port;
        do { digits[d++] = (char)('0' + p % 10u); p /= 10u; } while (p != 0u);
        while (d > 0u) url[o++] = digits[--d];
        o = put(url, o, "/flood");
        url[o] = 0;
        lt_fact("connect", net->ws_connect(lt_mod, url, 0, on_event, 0, &client));
    }
    if (k == 2) {
        lt_fact("pings_sent", (long long)pings_sent);
        lt_fact("big_send", net->ws_send(lt_mod, client, big, BIG, 1u));
        lt_fact("others", others);
    }
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4520;
    if (k >= 1 && k <= 2) step(k);
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(net, LUMA_NET_LOCAL_NAME, "1.4"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
