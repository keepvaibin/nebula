/* H's WebSocket conformance mod (services-net 8, net 1.4), against native/include/luma_net.h. Run steps call
   MR::getRandom(0, 4500 + k) and the hook gives 1.
   net-ws (2708): the mod serves WebSocket itself on a tcp_listen (its own SHA-1 and base64 for the Accept) and is its
   own client over ws://127.0.0.1:<port>/<path>:
   /echo       answers 101 choosing "chat" of "chat, other": CONNECTED with data "chat"; the client sends "hello" (text)
               and 3 bytes (binary); the server echoes "echo:hello" and the bytes, pings "p1" (the service's pong is
               checked), sends "frag" + "ment" in two frames, then closes 4000 "bye": CLOSED LUMA_OK 4000 with "bye",
               and the server hears the client's echo of 4000.
   /badaccept  101 with a wrong Accept: ERROR LUMA_E_FORMAT, value 101.
   /missing    404: ERROR LUMA_E_FORMAT, value 404.
   /badtext    101, then a text frame not UTF-8: CLOSED LUMA_E_FORMAT 1007, and the server hears a close 1007.
   Mistakes at k 1: an http:// url, a protocol with a space, a null fn: ARGUMENT; ws_send before CONNECTED: CLOSED;
   send on a WebSocket: HANDLE. At CONNECTED: ws_send binary 2 and text not UTF-8: ARGUMENT. */
#include "../loader/lt.h"
#include <luma_net.h>

static const LumaNetLocalService* net;

/* ---- SHA-1 (FIPS 180-4) and base64, for the server's Accept ---- */

static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void sha1(const uint8_t* data, uint32_t n, uint8_t out[20]) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    uint8_t block[64];
    const uint64_t bits = (uint64_t)n * 8u;
    const uint32_t total = ((n + 8u) / 64u + 1u) * 64u;
    for (uint32_t at = 0; at < total; at += 64u) {
        for (uint32_t i = 0; i < 64u; i++) {
            const uint32_t k = at + i;
            uint8_t b = 0;
            if (k < n) b = data[k];
            else if (k == n) b = 0x80u;
            else if (k >= total - 8u) b = (uint8_t)(bits >> (8u * (total - 1u - k)));
            block[i] = b;
        }
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 | (uint32_t)block[i * 4 + 2] << 8 |
                   block[i * 4 + 3];
        }
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (int i = 0; i < 5; i++) {
        out[i * 4] = (uint8_t)(h[i] >> 24); out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(h[i] >> 8); out[i * 4 + 3] = (uint8_t)h[i];
    }
}

static uint32_t base64(const uint8_t* in, uint32_t n, char* out) {
    static const char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t o = 0;
    for (uint32_t i = 0; i < n; i += 3u) {
        const uint32_t v = (uint32_t)in[i] << 16 | (i + 1u < n ? (uint32_t)in[i + 1] << 8 : 0u) |
                           (i + 2u < n ? in[i + 2] : 0u);
        out[o++] = abc[(v >> 18) & 63u];
        out[o++] = abc[(v >> 12) & 63u];
        out[o++] = i + 1u < n ? abc[(v >> 6) & 63u] : '=';
        out[o++] = i + 2u < n ? abc[v & 63u] : '=';
    }
    out[o] = 0;
    return o;
}

static uint32_t length_of(const char* s) { uint32_t n = 0; while (s[n]) n++; return n; }
static int same(const void* a, const void* b, uint32_t n) {
    const uint8_t* x = (const uint8_t*)a; const uint8_t* y = (const uint8_t*)b;
    for (uint32_t i = 0; i < n; i++) if (x[i] != y[i]) return 0;
    return 1;
}
static int starts(const char* text, uint32_t n, const char* with) {
    const uint32_t m = length_of(with);
    return n >= m && same(text, with, m);
}
static uint32_t put(char* to, uint32_t at, const char* s) {
    for (uint32_t i = 0; s[i]; i++) to[at++] = s[i];
    return at;
}

/* ---- The server's connections ---- */

enum { PATH_NONE, PATH_ECHO, PATH_BADACCEPT, PATH_MISSING, PATH_BADTEXT };
typedef struct Conn {
    LumaHandle handle;
    int path;
    int upgraded;
    char in[4096];
    uint32_t have;
    int got_text, got_binary;
} Conn;
static Conn conns[4];
static uint32_t conn_count;
static LumaHandle listener, c_echo, c_badaccept, c_missing, c_badtext;
static uint16_t port;
static int32_t connected_echo, chosen_chat, send_binary_2, send_bad_text, msg_echo, msg_binary, msg_frag, others;
static int32_t pong_ok, server_close_code, server_close_1007, closed_status, closed_value, closed_reason;
static int32_t err_badaccept, err_badaccept_value, err_missing, err_missing_value, closed_badtext, closed_badtext_value;

static Conn* conn_of(LumaHandle h) {
    for (uint32_t i = 0; i < conn_count; i++) if (conns[i].handle == h) return &conns[i];
    return 0;
}

/* A server frame (not masked). */
static void server_frame(LumaHandle h, uint8_t first, const void* data, uint32_t n) {
    uint8_t f[300];
    f[0] = first;
    f[1] = (uint8_t)n;  /* the test's frames are under 126 bytes */
    for (uint32_t i = 0; i < n; i++) f[2 + i] = ((const uint8_t*)data)[i];
    (void)net->send(lt_mod, h, f, 2u + n);
}

static void answer_upgrade(Conn* c, const char* head, uint32_t n) {
    char key[64];
    uint32_t key_n = 0;
    for (uint32_t i = 0; i + 19u < n; i++) {
        if (starts(head + i, n - i, "Sec-WebSocket-Key: ")) {
            uint32_t j = i + 19u;
            while (j < n && head[j] != '\r' && key_n < 60u) key[key_n++] = head[j++];
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
    if (c->path == PATH_BADACCEPT) accept[0] = accept[0] == 'A' ? 'B' : 'A';
    char out[512];
    uint32_t o = 0;
    if (c->path == PATH_MISSING) {
        o = put(out, 0, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
    } else {
        o = put(out, 0, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n");
        o = put(out, o, "Sec-WebSocket-Accept: ");
        o = put(out, o, accept);
        o = put(out, o, "\r\n");
        if (c->path == PATH_ECHO) o = put(out, o, "Sec-WebSocket-Protocol: chat\r\n");
        o = put(out, o, "\r\n");
    }
    (void)net->send(lt_mod, c->handle, out, o);
    c->upgraded = 1;
    if (c->path == PATH_BADTEXT) {
        const uint8_t bad[2] = {0xC3u, 0x28u};  /* not UTF-8 */
        server_frame(c->handle, 0x81u, bad, 2u);
    }
}

/* The client's frames (masked) waiting in c->in. */
static void server_frames(Conn* c) {
    for (;;) {
        if (c->have < 6u) return;
        const uint8_t* b = (const uint8_t*)c->in;
        const uint32_t op = b[0] & 0x0Fu;
        const uint32_t n = b[1] & 0x7Fu;  /* the test's frames are under 126 bytes */
        if (c->have < 6u + n) return;
        uint8_t payload[128];
        for (uint32_t i = 0; i < n; i++) payload[i] = b[6 + i] ^ b[2 + (i & 3u)];
        if (op == 1u && n == 5u && same(payload, "hello", 5u)) {
            server_frame(c->handle, 0x81u, "echo:hello", 10u);
            c->got_text = 1;
        } else if (op == 2u) {
            server_frame(c->handle, 0x82u, payload, n);
            c->got_binary = 1;
        } else if (op == 10u) {
            pong_ok = n == 2u && same(payload, "p1", 2u);
        } else if (op == 8u) {
            const int32_t code = n >= 2u ? (int32_t)(payload[0] << 8 | payload[1]) : 1005;
            if (c->path == PATH_ECHO) server_close_code = code;
            if (c->path == PATH_BADTEXT) server_close_1007 = code == 1007;
        }
        if (c->path == PATH_ECHO && c->got_text && c->got_binary) {
            c->got_text = 0;  /* once */
            server_frame(c->handle, 0x89u, "p1", 2u);
            server_frame(c->handle, 0x01u, "frag", 4u);
            server_frame(c->handle, 0x80u, "ment", 4u);
            const uint8_t bye[5] = {0x0Fu, 0xA0u, 'b', 'y', 'e'};  /* 4000 */
            server_frame(c->handle, 0x88u, bye, 5u);
        }
        const uint32_t used = 6u + n;
        for (uint32_t i = used; i < c->have; i++) c->in[i - used] = c->in[i];
        c->have -= used;
    }
}

static void on_event(void* user, const LumaEvent* event) {
    (void)user;
    const LumaHandle h = event->handle;
    if (event->kind == LUMA_NET_EVENT_ACCEPTED && h == listener) {
        if (conn_count < 4u) {
            conns[conn_count].handle = event->value;
            conns[conn_count].path = PATH_NONE;
            conn_count++;
        }
        return;
    }
    Conn* c = conn_of(h);
    if (c != 0) {
        if (event->kind == LUMA_NET_EVENT_DATA) {
            const int64_t got = net->take(lt_mod, h, c->in + c->have, sizeof(c->in) - c->have);
            if (got > 0) c->have += (uint32_t)got;
            if (!c->upgraded) {
                for (uint32_t i = 0; i + 3u < c->have; i++) {
                    if (c->in[i] == '\r' && c->in[i + 1] == '\n' && c->in[i + 2] == '\r' && c->in[i + 3] == '\n') {
                        if (starts(c->in, c->have, "GET /echo ")) c->path = PATH_ECHO;
                        else if (starts(c->in, c->have, "GET /badaccept ")) c->path = PATH_BADACCEPT;
                        else if (starts(c->in, c->have, "GET /missing ")) c->path = PATH_MISSING;
                        else if (starts(c->in, c->have, "GET /badtext ")) c->path = PATH_BADTEXT;
                        answer_upgrade(c, c->in, i + 4u);
                        const uint32_t used = i + 4u;
                        for (uint32_t j = used; j < c->have; j++) c->in[j - used] = c->in[j];
                        c->have -= used;
                        break;
                    }
                }
            }
            if (c->upgraded) server_frames(c);
        }
        return;  /* SENT, CLOSED of the server's side */
    }
    if (h == c_echo) {
        if (event->kind == LUMA_NET_EVENT_CONNECTED) {
            connected_echo++;
            chosen_chat = event->data_size == 4u && same(event->data, "chat", 4u);
            const uint8_t three[3] = {1, 2, 3};
            send_binary_2 = net->ws_send(lt_mod, h, three, 3u, 2u);
            const uint8_t bad[2] = {0xC3u, 0x28u};
            send_bad_text = net->ws_send(lt_mod, h, bad, 2u, 0u);
            (void)net->ws_send(lt_mod, h, "hello", 5u, 0u);
            (void)net->ws_send(lt_mod, h, three, 3u, 1u);
        } else if (event->kind == LUMA_NET_EVENT_MESSAGE) {
            if (event->flags == LUMA_NET_WS_TEXT && event->value == 10u && same(event->data, "echo:hello", 10u)) {
                msg_echo++;
            } else if (event->flags == LUMA_NET_WS_BINARY && event->value == 3u) {
                msg_binary++;
            } else if (event->flags == LUMA_NET_WS_TEXT && event->value == 8u && same(event->data, "fragment", 8u)) {
                msg_frag++;
            } else {
                others++;
            }
        } else if (event->kind == LUMA_NET_EVENT_CLOSED) {
            closed_status = event->status;
            closed_value = (int32_t)event->value;
            closed_reason = event->data_size == 3u && same(event->data, "bye", 3u);
        } else if (event->kind != LUMA_NET_EVENT_SENT) {
            others++;
        }
    } else if (h == c_badaccept && event->kind == LUMA_NET_EVENT_ERROR) {
        err_badaccept = event->status;
        err_badaccept_value = (int32_t)event->value;
    } else if (h == c_missing && event->kind == LUMA_NET_EVENT_ERROR) {
        err_missing = event->status;
        err_missing_value = (int32_t)event->value;
    } else if (h == c_badtext && event->kind == LUMA_NET_EVENT_CLOSED) {
        closed_badtext = event->status;
        closed_badtext_value = (int32_t)event->value;
    } else if (h == c_badtext && event->kind == LUMA_NET_EVENT_CONNECTED) {
        /* expected before its CLOSED */
    } else {
        others++;
    }
}

static void url_for(char* out, const char* path) {
    uint32_t o = put(out, 0, "ws://127.0.0.1:");
    char digits[6];
    uint32_t d = 0;
    uint32_t p = port;
    do { digits[d++] = (char)('0' + p % 10u); p /= 10u; } while (p != 0u);
    while (d > 0u) out[o++] = digits[--d];
    o = put(out, o, path);
    out[o] = 0;
}

static void step(int32_t k) {
    if (k == 1) {
        lt_fact("listen", net->tcp_listen(lt_mod, 0, 0, on_event, 0, &listener));
        (void)net->port(lt_mod, listener, &port);
        char url[64];
        LumaHandle h = 0;
        lt_fact("http_url", net->ws_connect(lt_mod, "http://127.0.0.1/", 0, on_event, 0, &h));
        url_for(url, "/echo");
        lt_fact("space_protocol", net->ws_connect(lt_mod, url, "a b", on_event, 0, &h));
        lt_fact("null_fn", net->ws_connect(lt_mod, url, 0, 0, 0, &h));
        lt_fact("echo", net->ws_connect(lt_mod, url, "chat, other", on_event, 0, &c_echo));
        lt_fact("send_early", net->ws_send(lt_mod, c_echo, "x", 1u, 0u));
        lt_fact("plain_send", net->send(lt_mod, c_echo, "x", 1u));
        url_for(url, "/badaccept");
        lt_fact("badaccept", net->ws_connect(lt_mod, url, 0, on_event, 0, &c_badaccept));
        url_for(url, "/missing");
        lt_fact("missing", net->ws_connect(lt_mod, url, 0, on_event, 0, &c_missing));
        url_for(url, "/badtext");
        lt_fact("badtext", net->ws_connect(lt_mod, url, 0, on_event, 0, &c_badtext));
    }
    if (k == 2) {
        lt_fact("connected_echo", connected_echo);
        lt_fact("chosen_chat", chosen_chat);
        lt_fact("send_binary_2", send_binary_2);
        lt_fact("send_bad_text", send_bad_text);
        lt_fact("msg_echo", msg_echo);
        lt_fact("msg_binary", msg_binary);
        lt_fact("msg_frag", msg_frag);
        lt_fact("pong_ok", pong_ok);
        lt_fact("closed_status", closed_status);
        lt_fact("closed_value", closed_value);
        lt_fact("closed_reason", closed_reason);
        lt_fact("server_close_code", server_close_code);
        lt_fact("err_badaccept", err_badaccept);
        lt_fact("err_badaccept_value", err_badaccept_value);
        lt_fact("err_missing", err_missing);
        lt_fact("err_missing_value", err_missing_value);
        lt_fact("closed_badtext", closed_badtext);
        lt_fact("closed_badtext_value", closed_badtext_value);
        lt_fact("server_close_1007", server_close_1007);
        lt_fact("send_after_close", net->ws_send(lt_mod, c_echo, "x", 1u, 0u));
        lt_fact("others", others);
        lt_fact("close_listener", net->close(lt_mod, listener));
    }
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4500;
    if (k >= 1 && k <= 2) step(k);
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(net, LUMA_NET_LOCAL_NAME, "1.4"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
