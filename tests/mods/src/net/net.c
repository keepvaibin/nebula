/* H's net.local conformance mods (services-net, plan 2.7 layer 1), against native/include/luma_net.h. One source, one
   case each (-DCASE=n). Run steps call MR::getRandom(0, 4500 + k) and the hook gives 1.
   net-echo      (1) 2701: at k 1 listen on a port the system picks (this machine only), connect to 127.0.0.1 there;
                      CONNECTED: the client sends "ping"; ACCEPTED: the server socket; the server's DATA: it answers
                      "pong:" + what it took; the client's DATA: compared (same=1); then the client closes and the
                      server gets CLOSED (status 0). Facts count each event.
   net-mistakes  (2) 2702: at k 1 a host outside the local network, port 0, a null fn, local_network 2, send on a
                      listener, take outside a DATA call, close twice, 16 listeners then a 17th (LIMIT); a connect to a
                      port nobody listens on: ERROR LUMA_E_UNREACHABLE.
   net-internet  (3) 2703: net.internet's rules without a public host (a test reaches none). luma.json names
                      localhost, 127.0.0.1, nothing.invalid and *.named.invalid. At k 1: a host it does not name, the
                      parent of a "*." entry: DENIED; a host with a wrong letter, port 0, null host: ARGUMENT; 16
                      net.local listeners then an internet connect: LIMIT (one count for both services); a net.local
                      handle given to net.internet's send, close and port: HANDLE; net.internet's tcp_listen: DENIED.
                      Then four connects, each PENDING: LOCALHOST (a name whose addresses are all this machine's) and
                      127.0.0.1: ERROR DENIED; nothing.invalid and a.named.invalid (no such name, RFC 6761): ERROR
                      NOT_FOUND. k 2: counts.
   net-udp       (4) 2704: UDP (1.1). At k 1 socket A by udp_open (port the system picks, this machine), B by
                      udp_connect to 127.0.0.1:A; the mistakes: send on A, send_to on B, take and take_datagram outside
                      a call: HANDLE; send_to a far host or a v6 one: ARGUMENT; 65508 bytes: TOO_LARGE; net.internet's
                      udp_open and send_to: DENIED; its udp_connect to LOCALHOST (named): PENDING, then ERROR DENIED.
                      B's CONNECTED: it sends "ping" and an empty datagram; A's DATAGRAM: it takes both (the sender
                      127.0.0.1), take inside it is HANDLE, and answers "pong:ping" with send_to to the sender; B's
                      DATAGRAM: "pong:ping" from A's port; B closes. k 2: counts, A closed.
   net-ipc       (5) 2705: endpoints on this device (1.2). At k 1 ipc_listen "echo" (endpoint nebula.net-ipc.echo),
                      the same again: EXISTS; port on it: HANDLE; a name with '/' and one of 65 bytes: ARGUMENT;
                      net.internet's ipc_connect and ipc_listen: DENIED; ipc_connect to nebula.net-ipc.echo, then the
                      same exchange as net-echo (ping, pong:ping, the client's close is the server's CLOSED 0); an
                      ipc_connect to nebula.net-ipc.nobody: ERROR NOT_FOUND. k 2: counts, the listener closed.
   net-tls       (6) 2706: TLS (1.3). The package holds certs/ (a test root, a leaf for localhost and 127.0.0.1, one
                      for wrong.example, a self-signed stranger); luma.json's net.local names certs/root.pem in `roots`.
                      At k 1 tls_listen with each leaf (ports the system picks); a missing file: NOT_FOUND; a key not
                      the certificate's and a key given as the certificate: FORMAT; "..", a rooted path, a null key:
                      ARGUMENT; net.internet's tls_listen: DENIED. tls_connect to localhost (send before CONNECTED:
                      CLOSED), to 127.0.0.1 (the address in the certificate), to the wrong.example and stranger
                      listeners as localhost: ERROR DENIED (Net.CertificateRefused), whose server sockets end CLOSED
                      DENIED; net.internet's tls_connect to LOCALHOST: ERROR DENIED; a far host: ARGUMENT. The
                      localhost stream: ping, pong:ping, the client's close is the server's CLOSED 0 (close_notify).
                      k 2: counts.
   net-http      (7) 2707: HTTP (1.3). The mod serves HTTP itself: a tcp_listen and a tls_listen (the leaf above)
                      answer by path. k 1: the mistakes (ARGUMENT each: null request, wrong size, ftp url, a far host,
                      TRACE, a Host header, a CR in a value, a timeout past the most, an unknown flag), net.internet:
                      a host not named DENIED, LOCALHOST ERROR DENIED; 8 requests to a closed port (PENDING, then
                      ERROR UNREACHABLE) and a 9th: LIMIT; send on a request: HANDLE. k 2, 3, 4: batches of requests
                      (plain with a header, chunked with an extension and a trailer, gzip, gzip RAW, a 302, an
                      answer to the connection's end, HEAD, 204, a POST echoed back whole, a 303 after a POST, https;
                      then 6 redirects in a row: LIMIT, a redirect to a far host and one from https to http: DENIED
                      (Net.RedirectRefused), a body past 64 MiB: TOO_LARGE, no answer within 400 ms: TIMEOUT, not
                      HTTP: FORMAT). k 5: each request's status, body, DONE and ERROR as facts. */
#include "../loader/lt.h"
#include <luma_net.h>

#ifndef CASE
#error CASE
#endif

static const LumaNetLocalService* net;
#if CASE == 3 || CASE == 4 || CASE == 5 || CASE == 6 || CASE == 7
static const LumaNetInternetService* inet;
#endif

#if CASE == 6 || CASE == 7
static int32_t same(const char* x, const char* y, int64_t n) {
    for (int64_t i = 0; i < n; i++) {
        if (x[i] != y[i]) return 0;
    }
    return 1;
}

static int64_t text_length(const char* s) {
    int64_t n = 0;
    while (s[n] != 0) n++;
    return n;
}

/* Copies s to at (no end byte); returns the end. */
__attribute__((unused)) static char* put(char* at, const char* s) {
    while (*s != 0) *at++ = *s++;
    return at;
}

__attribute__((unused)) static char* put_number(char* at, uint64_t v) {
    char digits[24];
    int n = 0;
    do {
        digits[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v != 0u);
    while (n > 0) *at++ = digits[--n];
    return at;
}

/* Where `needle` starts in `hay` (n bytes), or -1. */
__attribute__((unused)) static int64_t find(const char* hay, int64_t n, const char* needle) {
    const int64_t m = text_length(needle);
    for (int64_t i = 0; i + m <= n; i++) {
        if (same(hay + i, needle, m)) return i;
    }
    return -1;
}
#endif

#if CASE == 1
static LumaHandle listener, client, server;
static int32_t connected, accepted, server_data, client_data, sent, closed, errors;

static int32_t same4(const char* a, const char* b, int64_t n) {
    for (int64_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static void on_event(void* user, const LumaEvent* event) {
    (void)user;
    char got[64];
    switch (event->kind) {
    case LUMA_NET_EVENT_CONNECTED:
        connected++;
        lt_fact("send_ping", net->send(lt_mod, event->handle, "ping", 4));
        break;
    case LUMA_NET_EVENT_ACCEPTED:
        accepted++;
        server = (LumaHandle)event->value;
        lt_fact("accepted_from_listener", event->handle == listener);
        break;
    case LUMA_NET_EVENT_DATA: {
        const int64_t waiting = net->take(lt_mod, event->handle, 0, 0);
        const int64_t n = net->take(lt_mod, event->handle, got, sizeof(got));
        if (event->handle == server) {
            server_data++;
            lt_fact("server_waiting", waiting);
            lt_fact("server_got_ping", n == 4 && same4(got, "ping", 4));
            char answer[16] = {'p', 'o', 'n', 'g', ':'};
            for (int i = 0; i < 4; i++) answer[5 + i] = got[i];
            lt_fact("send_pong", net->send(lt_mod, server, answer, 9));
        } else if (event->handle == client) {
            client_data++;
            lt_fact("client_got_pong", n == 9 && same4(got, "pong:ping", 9));
            lt_fact("close_client", net->close(lt_mod, client));
        }
        break;
    }
    case LUMA_NET_EVENT_SENT:
        sent++;
        break;
    case LUMA_NET_EVENT_CLOSED:
        closed++;
        lt_fact("closed_is_server", event->handle == server);
        lt_fact("closed_status", event->status);
        break;
    case LUMA_NET_EVENT_ERROR:
        errors++;
        lt_fact("error_status", event->status);
        break;
    default:
        break;
    }
}
#elif CASE == 2
static int32_t refused_events;
static uint16_t refused_port;
static void on_event(void* user, const LumaEvent* event) {
    (void)user;
    if (event->kind == LUMA_NET_EVENT_ERROR) {
        refused_events++;
        lt_fact("refused_status", event->status);
    }
}
#elif CASE == 3
static LumaHandle h_loopback_name, h_loopback_address, h_none, h_wild;
static int32_t errors, others;
static void on_event(void* user, const LumaEvent* event) {
    (void)user;
    if (event->kind != LUMA_NET_EVENT_ERROR) {
        others++;
        return;
    }
    errors++;
    if (event->handle == h_loopback_name) lt_fact("loopback_name_status", event->status);
    if (event->handle == h_loopback_address) lt_fact("loopback_address_status", event->status);
    if (event->handle == h_none) lt_fact("none_status", event->status);
    if (event->handle == h_wild) lt_fact("wild_status", event->status);
}
#elif CASE == 5
static LumaHandle listener, client, server, nobody;
static int32_t connected, accepted, server_data, client_data, sent, closed, errors_client, nobody_errors, others;

static void on_event(void* user, const LumaEvent* event) {
    (void)user;
    char got[64];
    if (event->handle == nobody) {
        if (event->kind == LUMA_NET_EVENT_ERROR) {
            nobody_errors++;
            lt_fact("nobody_status", event->status);
        } else {
            others++;
        }
        return;
    }
    switch (event->kind) {
    case LUMA_NET_EVENT_CONNECTED:
        connected++;
        lt_fact("send_ping", net->send(lt_mod, event->handle, "ping", 4));
        break;
    case LUMA_NET_EVENT_ACCEPTED:
        accepted++;
        server = (LumaHandle)event->value;
        lt_fact("accepted_from_listener", event->handle == listener);
        break;
    case LUMA_NET_EVENT_DATA: {
        const int64_t n = net->take(lt_mod, event->handle, got, sizeof(got));
        if (event->handle == server) {
            server_data++;
            lt_fact("server_got_ping", n == 4 && got[0] == 'p' && got[1] == 'i' && got[2] == 'n' && got[3] == 'g');
            lt_fact("send_pong", net->send(lt_mod, server, "pong:ping", 9));
        } else if (event->handle == client) {
            client_data++;
            lt_fact("client_got_pong", n == 9 && got[0] == 'p' && got[4] == ':' && got[8] == 'g');
            lt_fact("close_client", net->close(lt_mod, client));
        }
        break;
    }
    case LUMA_NET_EVENT_SENT:
        sent++;
        break;
    case LUMA_NET_EVENT_CLOSED:
        closed++;
        lt_fact("closed_is_server", event->handle == server);
        lt_fact("closed_status", event->status);
        break;
    case LUMA_NET_EVENT_ERROR:
        errors_client++;
        lt_fact("client_error", event->status);
        break;
    default:
        others++;
        break;
    }
}
#elif CASE == 6
static LumaHandle good, wrong, stranger, client, address_client, wrong_client, stranger_client, inet_client, server;
static uint16_t good_port, wrong_port, stranger_port;
static int32_t connected, address_connected, accepted, server_data, client_data, closed_server, closed_status = 99,
    wrong_status, stranger_status, inet_status, refused_servers, other_closed_ok, others;

static void on_event(void* user, const LumaEvent* event) {
    (void)user;
    char got[64];
    const LumaHandle h = event->handle;
    switch (event->kind) {
    case LUMA_NET_EVENT_CONNECTED:
        if (h == client) {
            connected++;
            lt_fact("send_ping", net->send(lt_mod, client, "ping", 4));
        } else if (h == address_client) {
            address_connected++;
            lt_fact("close_address_client", net->close(lt_mod, address_client));
        } else {
            others++;
        }
        break;
    case LUMA_NET_EVENT_ACCEPTED:
        accepted++;
        break;
    case LUMA_NET_EVENT_DATA: {
        const int64_t n = net->take(lt_mod, h, got, sizeof(got));
        if (h == client) {
            client_data++;
            lt_fact("client_got_pong", n == 9 && same(got, "pong:ping", 9));
            lt_fact("close_client", net->close(lt_mod, client));
        } else if (n == 4 && same(got, "ping", 4)) {
            server = h;  /* the accepted socket the localhost client reached */
            server_data++;
            lt_fact("send_pong", net->send(lt_mod, server, "pong:ping", 9));
        } else {
            others++;
        }
        break;
    }
    case LUMA_NET_EVENT_SENT:
        break;
    case LUMA_NET_EVENT_CLOSED:
        if (h == server) {
            closed_server++;
            closed_status = event->status;
        } else if (event->status == LUMA_E_DENIED) {
            refused_servers++;  /* the wrong and stranger listeners' sockets: the client refused their handshake */
        } else if (event->status == LUMA_OK) {
            other_closed_ok++;  /* the 127.0.0.1 client's server socket */
        } else {
            others++;
        }
        break;
    case LUMA_NET_EVENT_ERROR:
        if (h == wrong_client) {
            wrong_status = event->status;
        } else if (h == stranger_client) {
            stranger_status = event->status;
        } else if (h == inet_client) {
            inet_status = event->status;
        } else {
            others++;
            lt_fact("unexpected_error", event->status);
        }
        break;
    default:
        others++;
        break;
    }
    /* Every event the test waits for has come (the handshakes' pace, not a frame count): said once. */
    static int32_t settled;
    if (!settled && connected && address_connected && client_data && closed_server && wrong_status && stranger_status &&
        inet_status && refused_servers >= 2 && other_closed_ok) {
        settled = 1;
        lt_fact("settled", 1);
    }
}
#elif CASE == 7
static const unsigned char kGzip[32] = {0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0a, 0xcb,
                                        0x48, 0xcd, 0xc9, 0xc9, 0xd7, 0x51, 0x48, 0xaf, 0xca, 0x2c, 0x50,
                                        0x04, 0x00, 0xc6, 0xe6, 0x3e, 0x08, 0x0c, 0x00, 0x00, 0x00};
static LumaHandle plain_listener, tls_listener;
static uint16_t plain_port, tls_port;
static int32_t server_others;

/* The mod's own HTTP server: each accepted socket's request, answered by its path. */
typedef struct Conn {
    LumaHandle h;
    int32_t used;
    int32_t answered;
    int64_t n;
    char buf[1024];
} Conn;
static Conn conns[16];
static char answer[2048];

static Conn* conn_of(LumaHandle h) {
    for (int i = 0; i < 16; i++) {
        if (conns[i].used && conns[i].h == h) return &conns[i];
    }
    return 0;
}

static void answer_with(Conn* c, const char* text, int64_t n) {
    net->send(lt_mod, c->h, text, (uint64_t)n);
    net->close(lt_mod, c->h);  /* the bytes still go (header): Connection: close */
    c->used = 0;
}

static void serve(Conn* c) {
    const int64_t head = find(c->buf, c->n, "\r\n\r\n");
    if (head < 0) return;
    int64_t body = 0;
    const int64_t length_at = find(c->buf, head, "Content-Length: ");
    if (length_at >= 0) {
        for (int64_t i = length_at + 16; i < head && c->buf[i] >= '0' && c->buf[i] <= '9'; i++) {
            body = body * 10 + (c->buf[i] - '0');
        }
    }
    if (c->n < head + 4 + body) return;  /* the body still comes */
    c->answered = 1;
    const int64_t space = find(c->buf, head, " ");
    const char* path = c->buf + space + 1;
#define IS(p) same(path, p " ", text_length(p " "))
#define SEND(t) answer_with(c, t, text_length(t))
    if (IS("/plain")) {
        SEND("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nX-Test: yes\r\n\r\nhello");
    } else if (IS("/chunked")) {
        SEND("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n7;x=1\r\n, world\r\n0\r\nX-Trailer: 1\r\n\r\n");
    } else if (IS("/gzip")) {
        char* at = put(answer, "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 32\r\n\r\n");
        for (int i = 0; i < 32; i++) *at++ = (char)kGzip[i];
        answer_with(c, answer, at - answer);
    } else if (IS("/redirect")) {
        SEND("HTTP/1.1 302 Found\r\nLocation: /plain\r\nContent-Length: 0\r\n\r\n");
    } else if (IS("/see-other")) {
        SEND("HTTP/1.1 303 See Other\r\nLocation: /echo\r\nContent-Length: 0\r\n\r\n");
    } else if (IS("/loop")) {
        SEND("HTTP/1.1 302 Found\r\nLocation: /loop\r\n\r\n");
    } else if (IS("/far")) {
        SEND("HTTP/1.1 302 Found\r\nLocation: http://8.8.8.8/\r\nContent-Length: 0\r\n\r\n");
    } else if (IS("/tohttp")) {
        char* at = put(answer, "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:");
        at = put_number(at, plain_port);
        at = put(at, "/plain\r\nContent-Length: 0\r\n\r\n");
        answer_with(c, answer, at - answer);
    } else if (IS("/big")) {
        SEND("HTTP/1.1 200 OK\r\nContent-Length: 99999999999\r\n\r\n");
    } else if (IS("/slow")) {
        /* no answer: the request's 400 ms pass */
    } else if (IS("/toend")) {
        SEND("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nto the end");
    } else if (IS("/head")) {
        SEND("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n");
    } else if (IS("/nocontent")) {
        SEND("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 204 No Content\r\n\r\n");
    } else if (IS("/garbage")) {
        SEND("NOT HTTP AT ALL\r\n\r\n");
    } else if (IS("/echo")) {
        char* at = put(answer, "HTTP/1.1 200 OK\r\nContent-Length: ");
        at = put_number(at, (uint64_t)c->n);
        at = put(at, "\r\n\r\n");
        for (int64_t i = 0; i < c->n; i++) *at++ = c->buf[i];
        answer_with(c, answer, at - answer);
    } else {
        SEND("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
    }
#undef IS
#undef SEND
}

static void on_server(void* user, const LumaEvent* event) {
    (void)user;
    switch (event->kind) {
    case LUMA_NET_EVENT_ACCEPTED:
        for (int i = 0; i < 16; i++) {
            if (!conns[i].used) {
                conns[i].used = 1;
                conns[i].answered = 0;
                conns[i].n = 0;
                conns[i].h = (LumaHandle)event->value;
                return;
            }
        }
        server_others++;
        net->close(lt_mod, (LumaHandle)event->value);
        break;
    case LUMA_NET_EVENT_DATA: {
        Conn* c = conn_of(event->handle);
        if (c == 0) {
            server_others++;
            break;
        }
        const int64_t room = (int64_t)sizeof(c->buf) - c->n;
        const int64_t n = net->take(lt_mod, c->h, c->buf + c->n, (uint64_t)room);
        if (n > 0) c->n += n;
        if (!c->answered) serve(c);
        break;
    }
    case LUMA_NET_EVENT_CLOSED: {
        Conn* c = conn_of(event->handle);
        if (c != 0) c->used = 0;  /* the client ended it first (a refused redirect, TOO_LARGE, a timeout) */
        break;
    }
    case LUMA_NET_EVENT_SENT:
        break;
    default:
        server_others++;
        break;
    }
}

/* The requests, by index (the event's user). */
typedef struct Spec {
    const char* name;
    const char* path;
    const char* method;
    int32_t https;
    uint32_t flags;
    uint32_t timeout_ms;
    const char* body;
} Spec;
typedef struct Req {
    LumaHandle h;
    int32_t made;
    int32_t headers;
    int32_t status;
    int32_t x_test;
    int64_t body_n;
    char body_got[512];
    int32_t done;
    int64_t done_value;
    int32_t error;
    int32_t others;
} Req;
static const Spec specs[] = {
    {"plain", "/plain", 0, 0, 0, 0, 0},
    {"chunked", "/chunked", 0, 0, 0, 0, 0},
    {"gzip", "/gzip", 0, 0, 0, 0, 0},
    {"gzip_raw", "/gzip", 0, 0, LUMA_NET_HTTP_RAW, 0, 0},
    {"redirect", "/redirect", 0, 0, 0, 0, 0},
    {"toend", "/toend", 0, 0, 0, 0, 0},
    {"head", "/head", "HEAD", 0, 0, 0, 0},
    {"nocontent", "/nocontent", 0, 0, 0, 0, 0},
    {"echo", "/echo", "POST", 0, 0, 0, "abc"},
    {"see_other", "/see-other", "POST", 0, 0, 0, "abc"},
    {"https", "/plain", 0, 1, 0, 0, 0},
    {"loop", "/loop", 0, 0, 0, 0, 0},
    {"far", "/far", 0, 0, 0, 0, 0},
    {"tohttp", "/tohttp", 0, 1, 0, 0, 0},
    {"big", "/big", 0, 0, 0, 0, 0},
    {"slow", "/slow", 0, 0, 0, 400, 0},
    {"garbage", "/garbage", 0, 0, 0, 0, 0},
    {"no_redirect", "/redirect", 0, 0, LUMA_NET_HTTP_NO_REDIRECT, 0, 0},
};
#define REQS ((int)(sizeof(specs) / sizeof(specs[0])))
static Req reqs[REQS];
static LumaHandle dead[9];
static uint16_t gone_port;
static int32_t dead_errors, dead_unreachable, inet_status, inet_others;
static LumaHandle inet_request;

static void on_request(void* user, const LumaEvent* event) {
    Req* r = &reqs[(uintptr_t)user];
    switch (event->kind) {
    case LUMA_NET_EVENT_HEADERS: {
        char lines[256];
        r->headers++;
        r->status = (int32_t)event->value;
        const int64_t n = net->take(lt_mod, r->h, lines, sizeof(lines));
        r->x_test = n > 0 && find(lines, n, "X-Test: yes\r\n") >= 0;
        break;
    }
    case LUMA_NET_EVENT_DATA: {
        const int64_t room = (int64_t)sizeof(r->body_got) - r->body_n;
        const int64_t n = net->take(lt_mod, r->h, r->body_got + r->body_n, (uint64_t)room);
        if (n > 0) r->body_n += n;
        break;
    }
    case LUMA_NET_EVENT_DONE:
        r->done++;
        r->done_value = (int64_t)event->value;
        break;
    case LUMA_NET_EVENT_ERROR:
        r->error = event->status;
        break;
    default:
        r->others++;
        break;
    }
}

static void on_other(void* user, const LumaEvent* event) {
    (void)user;
    if (event->kind == LUMA_NET_EVENT_ERROR && event->handle == inet_request) {
        inet_status = event->status;
    } else if (event->kind == LUMA_NET_EVENT_ERROR) {
        dead_errors++;
        if (event->status == LUMA_E_UNREACHABLE) dead_unreachable++;
        lt_fact("dead_status", event->status); /* the last dead request's status, for a failing run */
    } else {
        inet_others++;
    }
}

static char url[128];

static const char* url_of(int32_t https, uint16_t port, const char* path) {
    char* at = put(url, https ? "https://localhost:" : "http://127.0.0.1:");
    at = put_number(at, port);
    at = put(at, path);
    *at = 0;
    return url;
}

static void request(int i) {
    Req* r = &reqs[i];
    const Spec* p = &specs[i];
    LumaNetHeader header = {"X-Mod", "1"};
    LumaNetHttpRequest q;
    q.size = sizeof(q);
    q.timeout_ms = p->timeout_ms;
    q.method = p->method;
    q.url = url_of(p->https, p->https ? tls_port : plain_port, p->path);
    q.headers = &header;
    q.header_count = 1;
    q.flags = p->flags;
    q.body = p->body;
    q.body_bytes = p->body != 0 ? (uint64_t)text_length(p->body) : 0u;
    r->made = net->http(lt_mod, &q, on_request, (void*)(uintptr_t)i, &r->h);
}

static void fact_of(int i, const char* what, long long v) {
    char key[64];
    char* at = put(key, specs[i].name);
    at = put(at, "_");
    at = put(at, what);
    *at = 0;
    lt_fact(key, v);
}

static int32_t body_is(const Req* r, const char* text) {
    const int64_t n = text_length(text);
    return r->body_n == n && same(r->body_got, text, n);
}

static void mistake(const char* key, const char* method, const char* the_url, const char* name, const char* value,
                    uint32_t timeout_ms, uint32_t flags) {
    LumaNetHeader header = {name, value};
    LumaNetHttpRequest q;
    LumaHandle h = 0;
    q.size = sizeof(q);
    q.timeout_ms = timeout_ms;
    q.method = method;
    q.url = the_url;
    q.headers = name != 0 ? &header : 0;
    q.header_count = name != 0 ? 1u : 0u;
    q.flags = flags;
    q.body = 0;
    q.body_bytes = 0;
    lt_fact(key, net->http(lt_mod, &q, on_other, 0, &h));
}
#elif CASE == 4
static LumaHandle a, b, far_away;
static uint16_t a_port;
static int32_t b_connected, a_events, a_taken, a_ping, a_empty, a_from_loopback, b_events, b_pong, b_from_a, sent_a,
    sent_b, errors_far, others;
static char big[LUMA_NET_DATAGRAM_MOST + 1];

static int32_t same(const char* x, const char* y, int64_t n) {
    for (int64_t i = 0; i < n; i++) {
        if (x[i] != y[i]) return 0;
    }
    return 1;
}

static void on_event(void* user, const LumaEvent* event) {
    (void)user;
    char got[64];
    LumaNetPeer from;
    if (event->handle == far_away) {
        if (event->kind == LUMA_NET_EVENT_ERROR) {
            errors_far++;
            lt_fact("far_status", event->status);
        } else {
            others++;
        }
        return;
    }
    switch (event->kind) {
    case LUMA_NET_EVENT_CONNECTED:
        if (event->handle != b) {
            others++;
            break;
        }
        b_connected++;
        lt_fact("send_ping", net->send(lt_mod, b, "ping", 4));
        lt_fact("send_empty", net->send(lt_mod, b, "", 0));
        break;
    case LUMA_NET_EVENT_DATAGRAM:
        if (event->handle == a) {
            a_events++;
            char none[4];
            lt_fact("take_on_udp", net->take(lt_mod, a, none, sizeof(none)));
            for (;;) {
                const int64_t n = net->take_datagram(lt_mod, a, got, sizeof(got), &from);
                if (n < 0) {
                    lt_fact("a_last_take", n);
                    break;
                }
                a_taken++;
                if (n == 4 && same(got, "ping", 4)) a_ping++;
                if (n == 0) a_empty++;
                if (same(from.host, "127.0.0.1", 10)) a_from_loopback++;
                if (n == 4) lt_fact("answer", net->send_to(lt_mod, a, from.host, from.port, "pong:ping", 9));
            }
        } else if (event->handle == b) {
            b_events++;
            const int64_t n = net->take_datagram(lt_mod, b, got, sizeof(got), &from);
            if (n == 9 && same(got, "pong:ping", 9)) b_pong++;
            if (from.port == a_port && same(from.host, "127.0.0.1", 10)) b_from_a++;
            lt_fact("close_b", net->close(lt_mod, b));
        }
        break;
    case LUMA_NET_EVENT_SENT:
        if (event->handle == a) sent_a++;
        if (event->handle == b) sent_b++;
        break;
    default:
        others++;
        break;
    }
}
#endif

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4500;
    if (k < 0 || k > 9) return 0;
#if CASE == 1
    if (k == 1) {
        lt_fact("listen", net->tcp_listen(lt_mod, 0, 0, on_event, 0, &listener));
        uint16_t port = 0;
        lt_fact("port_status", net->port(lt_mod, listener, &port));
        lt_fact("port_given", port != 0);
        lt_fact("connect", net->tcp_connect(lt_mod, "127.0.0.1", port, on_event, 0, &client));
    }
    if (k == 2) {
        lt_fact("connected", connected);
        lt_fact("accepted", accepted);
        lt_fact("server_data", server_data);
        lt_fact("client_data", client_data);
        lt_fact("sent", sent);
        lt_fact("closed", closed);
        lt_fact("errors", errors);
        lt_fact("close_listener", net->close(lt_mod, listener));
    }
    if (k == 3) {
        /* address (1.4): the length asked first is the length given; the text is a dotted IPv4 address, not
           loopback. A machine with no local network answers NOT_FOUND to each, which the facts accept as well. */
        char text[64] = {0};
        const int64_t asked = net->address(lt_mod, 0, 0);
        const int64_t given = net->address(lt_mod, text, sizeof text);
        int dots = 0, digits = 1;
        for (int64_t i = 0; i < given && i < (int64_t)sizeof text; ++i) {
            if (text[i] == '.') ++dots;
            else if (text[i] < '0' || text[i] > '9') digits = 0;
        }
        const int none = asked == LUMA_E_NOT_FOUND && given == LUMA_E_NOT_FOUND;
        const int found = asked > 0 && asked == given && text[given] == 0 && dots == 3 && digits &&
                          !(text[0] == '1' && text[1] == '2' && text[2] == '7' && text[3] == '.');
        lt_fact("address_ok", none || found);
        char small[4];
        const int64_t short_status = net->address(lt_mod, small, sizeof small);
        lt_fact("address_short", none ? short_status == LUMA_E_NOT_FOUND : short_status == LUMA_E_TOO_LARGE);
        lt_fact("address_null_buf", net->address(lt_mod, 0, 5));
    }
#elif CASE == 2
    if (k == 1) {
        LumaHandle h = 0;
        lt_fact("host_far", net->tcp_connect(lt_mod, "8.8.8.8", 80, on_event, 0, &h));
        lt_fact("host_name", net->tcp_connect(lt_mod, "example.org", 80, on_event, 0, &h));
        lt_fact("port_zero", net->tcp_connect(lt_mod, "127.0.0.1", 0, on_event, 0, &h));
        lt_fact("null_fn", net->tcp_connect(lt_mod, "127.0.0.1", 9, 0, 0, &h));
        lt_fact("bad_network", net->tcp_listen(lt_mod, 0, 2, on_event, 0, &h));
        LumaHandle listeners[17];
        int32_t made = 0, last = 0;
        for (int i = 0; i < 17; i++) {
            last = net->tcp_listen(lt_mod, 0, 0, on_event, 0, &listeners[i]);
            if (last == LUMA_OK) made++;
        }
        lt_fact("listeners_made", made);
        lt_fact("listener_17", last);
        lt_fact("send_listener", net->send(lt_mod, listeners[0], "x", 1));
        char buf[4];
        lt_fact("take_outside", net->take(lt_mod, listeners[0], buf, sizeof(buf)));
        /* A port nobody listens on: listen, read its port, close it, connect there. */
        uint16_t port = 0;
        net->port(lt_mod, listeners[0], &port);
        lt_fact("close_once", net->close(lt_mod, listeners[0]));
        lt_fact("close_twice", net->close(lt_mod, listeners[0]));
        for (int i = 1; i < 16; i++) net->close(lt_mod, listeners[i]);
        refused_port = port;
    }
    /* The connect waits for a later poke: close() gives the listener back to the I/O thread, which closes its system
       socket at its next turn (up to 10 ms); a connect in the same call would land in the still open listener's
       backlog and never be refused. */
    if (k == 2) {
        LumaHandle h = 0;
        lt_fact("refused_connect", net->tcp_connect(lt_mod, "127.0.0.1", refused_port, on_event, 0, &h));
    }
    if (k == 3) lt_fact("refused_events", refused_events);
#elif CASE == 3
    if (k == 1) {
        LumaHandle h = 0;
        lt_fact("not_named", inet->tcp_connect(lt_mod, "example.org", 443, on_event, 0, &h));
        lt_fact("wild_parent", inet->tcp_connect(lt_mod, "named.invalid", 443, on_event, 0, &h));
        lt_fact("bad_letter", inet->tcp_connect(lt_mod, "bad_host.invalid", 443, on_event, 0, &h));
        lt_fact("port_zero", inet->tcp_connect(lt_mod, "localhost", 0, on_event, 0, &h));
        lt_fact("null_host", inet->tcp_connect(lt_mod, 0, 443, on_event, 0, &h));
        LumaHandle listeners[16];
        int32_t made = 0;
        for (int i = 0; i < 16; i++) {
            if (net->tcp_listen(lt_mod, 0, 0, on_event, 0, &listeners[i]) == LUMA_OK) made++;
        }
        lt_fact("listeners_made", made);
        lt_fact("limit_shared", inet->tcp_connect(lt_mod, "127.0.0.1", 9, on_event, 0, &h));
        lt_fact("send_other_service", inet->send(lt_mod, listeners[0], "x", 1));
        lt_fact("close_other_service", inet->close(lt_mod, listeners[0]));
        uint16_t port = 0;
        lt_fact("port_other_service", inet->port(lt_mod, listeners[0], &port));
        lt_fact("listen_internet", inet->tcp_listen(lt_mod, 0, 0, on_event, 0, &h));
        int32_t closed = 0;
        for (int i = 0; i < 16; i++) {
            if (net->close(lt_mod, listeners[i]) == LUMA_OK) closed++;
        }
        lt_fact("listeners_closed", closed);
        lt_fact("loopback_name", inet->tcp_connect(lt_mod, "LOCALHOST", 9, on_event, 0, &h_loopback_name));
        lt_fact("loopback_address", inet->tcp_connect(lt_mod, "127.0.0.1", 9, on_event, 0, &h_loopback_address));
        lt_fact("none", inet->tcp_connect(lt_mod, "nothing.invalid", 443, on_event, 0, &h_none));
        lt_fact("wild", inet->tcp_connect(lt_mod, "a.named.invalid.", 443, on_event, 0, &h_wild));
    }
    if (k == 2) {
        lt_fact("errors", errors);
        lt_fact("others", others);
        lt_fact("close_after_error", inet->close(lt_mod, h_none));
    }
#elif CASE == 6
    if (k == 1) {
        LumaHandle h = 0;
        lt_fact("listen", net->tls_listen(lt_mod, 0, 0, "certs/server.pem", "certs/server.key", on_event, 0, &good));
        lt_fact("listen_wrong", net->tls_listen(lt_mod, 0, 0, "certs/wrong.pem", "certs/wrong.key", on_event, 0, &wrong));
        lt_fact("listen_stranger",
                net->tls_listen(lt_mod, 0, 0, "certs/stranger.pem", "certs/stranger.key", on_event, 0, &stranger));
        net->port(lt_mod, good, &good_port);
        net->port(lt_mod, wrong, &wrong_port);
        net->port(lt_mod, stranger, &stranger_port);
        lt_fact("ports_given", good_port != 0 && wrong_port != 0 && stranger_port != 0);
        lt_fact("missing", net->tls_listen(lt_mod, 0, 0, "certs/none.pem", "certs/server.key", on_event, 0, &h));
        lt_fact("key_mismatch", net->tls_listen(lt_mod, 0, 0, "certs/server.pem", "certs/wrong.key", on_event, 0, &h));
        lt_fact("key_as_certificate",
                net->tls_listen(lt_mod, 0, 0, "certs/server.key", "certs/server.key", on_event, 0, &h));
        lt_fact("dotdot", net->tls_listen(lt_mod, 0, 0, "certs/../luma.json", "certs/server.key", on_event, 0, &h));
        lt_fact("rooted", net->tls_listen(lt_mod, 0, 0, "/certs/server.pem", "certs/server.key", on_event, 0, &h));
        lt_fact("null_key", net->tls_listen(lt_mod, 0, 0, "certs/server.pem", 0, on_event, 0, &h));
        lt_fact("internet_listen",
                inet->tls_listen(lt_mod, 0, 0, "certs/server.pem", "certs/server.key", on_event, 0, &h));
        lt_fact("connect", net->tls_connect(lt_mod, "localhost", good_port, on_event, 0, &client));
        lt_fact("send_early", net->send(lt_mod, client, "x", 1));
        lt_fact("connect_address", net->tls_connect(lt_mod, "127.0.0.1", good_port, on_event, 0, &address_client));
        lt_fact("connect_wrong", net->tls_connect(lt_mod, "localhost", wrong_port, on_event, 0, &wrong_client));
        lt_fact("connect_stranger",
                net->tls_connect(lt_mod, "localhost", stranger_port, on_event, 0, &stranger_client));
        lt_fact("connect_far", net->tls_connect(lt_mod, "8.8.8.8", 443, on_event, 0, &h));
        lt_fact("connect_internet", inet->tls_connect(lt_mod, "LOCALHOST", good_port, on_event, 0, &inet_client));
    }
    if (k == 2) {
        lt_fact("connected", connected);
        lt_fact("address_connected", address_connected);
        lt_fact("accepted", accepted);
        lt_fact("server_data", server_data);
        lt_fact("client_data", client_data);
        lt_fact("closed_server", closed_server);
        lt_fact("closed_status", closed_status);
        lt_fact("wrong_status", wrong_status);
        lt_fact("stranger_status", stranger_status);
        lt_fact("inet_status", inet_status);
        lt_fact("refused_servers", refused_servers);
        lt_fact("other_closed_ok", other_closed_ok);
        lt_fact("others", others);
        lt_fact("close_listeners", net->close(lt_mod, good) == LUMA_OK && net->close(lt_mod, wrong) == LUMA_OK &&
                                       net->close(lt_mod, stranger) == LUMA_OK);
    }
#elif CASE == 7
    if (k == 1) {
        LumaHandle h = 0;
        lt_fact("listen", net->tcp_listen(lt_mod, 0, 0, on_server, 0, &plain_listener));
        lt_fact("listen_tls",
                net->tls_listen(lt_mod, 0, 0, "certs/server.pem", "certs/server.key", on_server, 0, &tls_listener));
        net->port(lt_mod, plain_listener, &plain_port);
        net->port(lt_mod, tls_listener, &tls_port);
        lt_fact("null_request", net->http(lt_mod, 0, on_other, 0, &h));
        {
            LumaNetHttpRequest q;
            q.size = sizeof(q) - 8u;
            q.url = "http://127.0.0.1/";
            lt_fact("wrong_size", net->http(lt_mod, &q, on_other, 0, &h));
        }
        mistake("ftp_url", 0, "ftp://127.0.0.1/", 0, 0, 0, 0);
        mistake("far_host", 0, "http://8.8.8.8/", 0, 0, 0, 0);
        mistake("trace", "TRACE", url_of(0, plain_port, "/plain"), 0, 0, 0, 0);
        mistake("host_header", 0, url_of(0, plain_port, "/plain"), "Host", "x", 0, 0);
        mistake("cr_in_value", 0, url_of(0, plain_port, "/plain"), "X-Bad", "a\rb", 0, 0);
        mistake("timeout_most", 0, url_of(0, plain_port, "/plain"), 0, 0, LUMA_NET_TIMEOUT_MOST_MS + 1u, 0);
        mistake("unknown_flag", 0, url_of(0, plain_port, "/plain"), 0, 0, 0, 4u);
        {
            LumaNetHttpRequest q;
            q.size = sizeof(q);
            q.timeout_ms = 0;
            q.method = 0;
            q.url = "http://example.org/";
            q.headers = 0;
            q.header_count = 0;
            q.flags = 0;
            q.body = 0;
            q.body_bytes = 0;
            lt_fact("internet_not_named", inet->http(lt_mod, &q, on_other, 0, &h));
            /* A port nobody listens on: a listener's, closed now; the requests to it wait for the next poke, since
               close() gives the listener back to the I/O thread, which closes its system socket at its next turn (up
               to 10 ms): a request in the same call would reach the still open listener and never be refused. */
            LumaHandle gone = 0;
            net->tcp_listen(lt_mod, 0, 0, on_other, 0, &gone);
            net->port(lt_mod, gone, &gone_port);
            net->close(lt_mod, gone);
        }
    }
    if (k == 2) {
        /* net.internet's request to this machine (refused: its ERROR comes later), still open while net.local's are
           made: both services count to LUMA_NET_REQUESTS_MOST, so 7 more are pending and the 8th of them is LIMIT. */
        LumaNetHttpRequest q = {0};
        q.size = sizeof(q);
        q.url = "http://LOCALHOST/plain";
        lt_fact("internet_loopback", inet->http(lt_mod, &q, on_other, 0, &inet_request));
        /* 8 requests to the closed listener's port: 7 pending, then LIMIT; a 9th LIMIT again. */
        q.url = url_of(0, gone_port, "/");
        int32_t pending = 0, last = 0;
        for (int i = 0; i < 9; i++) {
            last = net->http(lt_mod, &q, on_other, 0, &dead[i]);
            if (last == LUMA_PENDING) pending++;
        }
        lt_fact("requests_pending", pending);
        lt_fact("request_9", last);
        lt_fact("send_on_request", net->send(lt_mod, dead[0], "x", 1));
    }
    if (k == 6) {
        /* After the 7 dead requests and net.internet's have ended (their slots free again): the first served six. */
        for (int i = 0; i <= 4; i++) request(i);  /* plain, chunked, gzip, gzip_raw, redirect */
        request(REQS - 1);                         /* no_redirect */
    }
    if (k == 3) {
        for (int i = 5; i <= 10; i++) request(i);  /* toend, head, nocontent, echo, see_other, https */
    }
    if (k == 4) {
        for (int i = 11; i < REQS - 1; i++) request(i);  /* loop, far, tohttp, big, slow, garbage */
    }
    if (k == 5) {
        for (int i = 0; i < REQS; i++) {
            const Req* r = &reqs[i];
            fact_of(i, "made", r->made);
            fact_of(i, "headers", r->headers);
            fact_of(i, "status", r->status);
            fact_of(i, "done", r->done);
            fact_of(i, "done_value", r->done_value);
            fact_of(i, "error", r->error);
            fact_of(i, "others", r->others);
        }
        lt_fact("plain_body", body_is(&reqs[0], "hello"));
        lt_fact("plain_x_test", reqs[0].x_test);
        lt_fact("chunked_body", body_is(&reqs[1], "hello, world"));
        lt_fact("gzip_body", body_is(&reqs[2], "hello, gzip!"));
        lt_fact("gzip_raw_body", reqs[3].body_n == 32 && same(reqs[3].body_got, (const char*)kGzip, 32));
        lt_fact("redirect_body", body_is(&reqs[4], "hello"));
        lt_fact("toend_body", body_is(&reqs[5], "to the end"));
        const Req* e = &reqs[8];
        lt_fact("echo_line", find(e->body_got, e->body_n, "POST /echo HTTP/1.1\r\n") == 0);
        lt_fact("echo_host", find(e->body_got, e->body_n, "\r\nHost: 127.0.0.1:") > 0);
        lt_fact("echo_agent", find(e->body_got, e->body_n, "\r\nUser-Agent: Nebula/") > 0 &&
                                  find(e->body_got, e->body_n, " net-http\r\n") > 0);
        lt_fact("echo_gzip", find(e->body_got, e->body_n, "\r\nAccept-Encoding: gzip\r\n") > 0);
        lt_fact("echo_close", find(e->body_got, e->body_n, "\r\nConnection: close\r\n") > 0);
        lt_fact("echo_length", find(e->body_got, e->body_n, "\r\nContent-Length: 3\r\n") > 0);
        lt_fact("echo_mod_header", find(e->body_got, e->body_n, "\r\nX-Mod: 1\r\n") > 0);
        lt_fact("echo_body", e->body_n > 3 && same(e->body_got + e->body_n - 7, "\r\n\r\nabc", 7));
        const Req* s = &reqs[9];
        lt_fact("see_other_get", find(s->body_got, s->body_n, "GET /echo HTTP/1.1\r\n") == 0 &&
                                     find(s->body_got, s->body_n, "Content-Length") < 0);
        lt_fact("https_body", body_is(&reqs[10], "hello"));
        lt_fact("dead_errors", dead_errors);
        lt_fact("dead_unreachable", dead_unreachable);
        lt_fact("inet_status", inet_status);
        lt_fact("inet_others", inet_others);
        lt_fact("server_others", server_others);
        int32_t open_conns = 0;
        for (int i = 0; i < 16; i++) {
            if (conns[i].used) {
                open_conns++;
                net->close(lt_mod, conns[i].h);  /* /slow's socket */
                conns[i].used = 0;
            }
        }
        lt_fact("open_conns", open_conns);
        lt_fact("close_listeners",
                net->close(lt_mod, plain_listener) == LUMA_OK && net->close(lt_mod, tls_listener) == LUMA_OK);
    }
#elif CASE == 4
    if (k == 1) {
        LumaHandle h = 0;
        lt_fact("open_a", net->udp_open(lt_mod, 0, 0, on_event, 0, &a));
        lt_fact("port_a", net->port(lt_mod, a, &a_port));
        lt_fact("port_given", a_port != 0);
        lt_fact("connect_b", net->udp_connect(lt_mod, "127.0.0.1", a_port, on_event, 0, &b));
        lt_fact("send_on_open", net->send(lt_mod, a, "x", 1));
        lt_fact("send_to_on_tied", net->send_to(lt_mod, b, "127.0.0.1", a_port, "x", 1));
        lt_fact("send_to_far", net->send_to(lt_mod, a, "8.8.8.8", 53, "x", 1));
        lt_fact("send_to_v6", net->send_to(lt_mod, a, "::1", a_port, "x", 1));
        lt_fact("too_large", net->send_to(lt_mod, a, "127.0.0.1", a_port, big, sizeof(big)));
        char buf[4];
        LumaNetPeer from;
        lt_fact("take_datagram_outside", net->take_datagram(lt_mod, a, buf, sizeof(buf), &from));
        lt_fact("internet_udp_open", inet->udp_open(lt_mod, 0, 0, on_event, 0, &h));
        lt_fact("internet_send_to", inet->send_to(lt_mod, a, "127.0.0.1", a_port, "x", 1));
        lt_fact("internet_connect", inet->udp_connect(lt_mod, "LOCALHOST", 9, on_event, 0, &far_away));
    }
    if (k == 2) {
        lt_fact("b_connected", b_connected);
        lt_fact("a_events_some", a_events >= 1);
        lt_fact("a_taken", a_taken);
        lt_fact("a_ping", a_ping);
        lt_fact("a_empty", a_empty);
        lt_fact("a_from_loopback", a_from_loopback);
        lt_fact("b_events", b_events);
        lt_fact("b_pong", b_pong);
        lt_fact("b_from_a", b_from_a);
        lt_fact("sent_a_some", sent_a >= 1);
        lt_fact("sent_b_some", sent_b >= 1);
        lt_fact("errors_far", errors_far);
        lt_fact("others", others);
        lt_fact("close_a", net->close(lt_mod, a));
    }
#elif CASE == 5
    if (k == 1) {
        LumaHandle h = 0;
        uint16_t port = 0;
        lt_fact("listen", net->ipc_listen(lt_mod, "echo", on_event, 0, &listener));
        lt_fact("listen_twice", net->ipc_listen(lt_mod, "echo", on_event, 0, &h));
        lt_fact("port_ipc", net->port(lt_mod, listener, &port));
        lt_fact("bad_name", net->ipc_connect(lt_mod, "bad/name", on_event, 0, &h));
        lt_fact("long_name", net->ipc_connect(lt_mod,
                                              "a123456789b123456789c123456789d123456789e123456789f123456789g1234",
                                              on_event, 0, &h));
        lt_fact("internet_connect", inet->ipc_connect(lt_mod, "nebula.net-ipc.echo", on_event, 0, &h));
        lt_fact("internet_listen", inet->ipc_listen(lt_mod, "other", on_event, 0, &h));
        lt_fact("connect", net->ipc_connect(lt_mod, "nebula.net-ipc.echo", on_event, 0, &client));
        lt_fact("connect_nobody", net->ipc_connect(lt_mod, "nebula.net-ipc.nobody", on_event, 0, &nobody));
    }
    if (k == 2) {
        lt_fact("connected", connected);
        lt_fact("accepted", accepted);
        lt_fact("server_data", server_data);
        lt_fact("client_data", client_data);
        lt_fact("sent", sent);
        lt_fact("closed", closed);
        lt_fact("errors_client", errors_client);
        lt_fact("nobody_errors", nobody_errors);
        lt_fact("others", others);
        lt_fact("close_listener", net->close(lt_mod, listener));
    }
#endif
    return 0;
}

static const LumaImport imports[] = {
#if CASE == 4
    LUMA_SERVICE(net, LUMA_NET_LOCAL_NAME, "1.1"),
    LUMA_SERVICE(inet, LUMA_NET_INTERNET_NAME, "1.1"),
#elif CASE == 5
    LUMA_SERVICE(net, LUMA_NET_LOCAL_NAME, "1.2"),
    LUMA_SERVICE(inet, LUMA_NET_INTERNET_NAME, "1.2"),
#elif CASE == 6 || CASE == 7
    LUMA_SERVICE(net, LUMA_NET_LOCAL_NAME, "1.3"),
    LUMA_SERVICE(inet, LUMA_NET_INTERNET_NAME, "1.3"),
#else
    LUMA_SERVICE(net, LUMA_NET_LOCAL_NAME, "1.0"),
#endif
#if CASE == 3
    LUMA_SERVICE(inet, LUMA_NET_INTERNET_NAME, "1.0"),
#endif
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
