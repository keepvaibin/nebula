/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's net services (contract services-net; plan 2.7). Layer 1: `net.local` TCP on this machine and the local
   network, permission "net.local". Layer 2: `net.internet` TCP to the hosts the mod's luma.json names, by name or
   address, permission "net.internet" (the player sees the hosts). Every call is the game thread's, never blocks, and
   what waits is an event at a frame's start (services-registry 6). Both share the events, the limits and the
   socket calls' rules; a handle belongs to the service that made it. Minor 1 of both adds UDP: a socket open to
   anyone on this machine or the local network (net.local's udp_open), one tied to a single peer (udp_connect,
   both services), datagrams sent whole and taken whole. Minor 2 adds local endpoints by name (plan 2.7's net.ipc,
   under net.local's permission: "programs on this device"): named pipes on Windows, Unix sockets elsewhere, the
   stream calls and events of TCP. Minor 3 adds TLS (1.2 and 1.3; the game's one library on every platform):
   tls_connect checks the host's certificate against the root list shipped with the game plus the roots the mod's
   luma.json adds for itself (`roots` on its net permission), tls_listen (net.local) serves with the mod's own
   certificate; and HTTP/1.1 requests (http, https) whose answers come as events. Minor 4 adds WebSocket clients
   (ws://, wss://; RFC 6455): whole messages in (MESSAGE) and out (ws_send), pings answered by the service. */
#ifndef LUMA_NET_H
#define LUMA_NET_H

#include <luma_services.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_NET_LOCAL_NAME "net.local"
#define LUMA_NET_LOCAL_MAJOR 1u
#define LUMA_NET_LOCAL_MINOR 4u
#define LUMA_NET_INTERNET_NAME "net.internet"
#define LUMA_NET_INTERNET_MAJOR 1u
#define LUMA_NET_INTERNET_MINOR 4u

#define LUMA_NET_SOCKETS_MOST 16u            /* sockets and listeners a mod holds at once, both services together */
#define LUMA_NET_SEND_QUEUED_MOST (4u << 20u) /* bytes a mod's sockets hold queued, not yet sent */
#define LUMA_NET_RECEIVED_MOST (1u << 20u)    /* bytes a socket holds received and not taken; reading pauses past it */
#define LUMA_NET_DATAGRAM_MOST 65507u        /* 1.1: bytes of one datagram (IPv4's most) */
#define LUMA_NET_ENDPOINT_MOST 64u           /* 1.2: bytes of an endpoint's name */
#define LUMA_NET_REQUESTS_MOST 8u            /* 1.3: requests a mod has open at once (each also counts as a socket) */
#define LUMA_NET_BODY_MOST (64u << 20u)      /* 1.3: bytes of one answer's body (after gzip); more: ERROR TOO_LARGE */
#define LUMA_NET_URL_MOST 2048u              /* 1.3: bytes of a request's url */
#define LUMA_NET_REQUEST_HEADERS_MOST 64u    /* 1.3: headers a request names */
#define LUMA_NET_REDIRECTS_MOST 5u           /* 1.3: redirects a request follows */
#define LUMA_NET_TIMEOUT_DEFAULT_MS 30000u   /* 1.3: a request's time when it names none */
#define LUMA_NET_TIMEOUT_MOST_MS 600000u     /* 1.3: the longest time a request may name */
#define LUMA_NET_MESSAGE_MOST (16u << 20u)   /* 1.4: bytes of one WebSocket message, either way */
#define LUMA_NET_PROTOCOL_MOST 256u          /* 1.4: bytes of ws_connect's protocol list */
#define LUMA_NET_WS_TEXT 1u                  /* 1.4: MESSAGE's flags, ws_send's `binary` 0: UTF-8 text */
#define LUMA_NET_WS_BINARY 2u                /* 1.4: MESSAGE's flags, ws_send's `binary` 1: bytes */

/* Events (LumaEvent.kind) to the fn given at connect or listen. `handle` is the socket's (ACCEPTED: the listener's). */
typedef enum LumaNetEvent {
    LUMA_NET_EVENT_CONNECTED = 1, /* status LUMA_OK; value 0. The socket may send. 1.4, a WebSocket's: after the
                                     server's 101 answer; data the protocol it chose (data_size 0: none) */
    LUMA_NET_EVENT_ACCEPTED = 2,  /* a listener's: status LUMA_OK; value: the new socket's handle, open, its events to
                                     the listener's fn and user */
    LUMA_NET_EVENT_DATA = 3,      /* status LUMA_OK; value: the bytes waiting; `take` inside this call. Bytes not taken
                                     stay, and another DATA follows at a later frame. */
    LUMA_NET_EVENT_SENT = 4,      /* status LUMA_OK; value: the bytes sent since the last SENT; nothing is queued now */
    LUMA_NET_EVENT_CLOSED = 5,    /* the other side closed (status LUMA_OK) or the connection broke (LUMA_E_IO); the
                                     handle is closed after this event (bytes still waiting came first as DATA).
                                     1.4, a WebSocket's: value the close code (status LUMA_OK: the server's, 1005
                                     when it sent none, data its reason; LUMA_E_IO 1006: the connection ended
                                     without a close; LUMA_E_FORMAT 1002 or 1007: the server broke the protocol or
                                     sent text not UTF-8; LUMA_E_TOO_LARGE 1009: a message past
                                     LUMA_NET_MESSAGE_MOST), the service having sent its own close */
    LUMA_NET_EVENT_ERROR = 6,     /* a connect that failed: LUMA_E_UNREACHABLE (nobody listens, no route),
                                     LUMA_E_TIMEOUT (10 s, the name's lookup included), LUMA_E_IO; net.internet
                                     also LUMA_E_NOT_FOUND (the name has no address) and LUMA_E_DENIED (its every
                                     address is this machine's, the local network's or not one a host has); the
                                     handle is closed after it */
    LUMA_NET_EVENT_DATAGRAM = 7,  /* 1.1, a UDP socket's: status LUMA_OK; value: the datagrams waiting;
                                     `take_datagram` inside this call, one datagram each. Datagrams not taken stay,
                                     and another DATAGRAM follows at a later frame. */
    LUMA_NET_EVENT_HEADERS = 8,   /* 1.3, a request's, once: status LUMA_OK; value: the HTTP status of the answer
                                     (after redirects); `take` inside this call gives its header lines as received
                                     ("Name: value\r\n" each, the status line left out). DATA events then bring the
                                     body (gzip undone), then DONE. */
    LUMA_NET_EVENT_DONE = 9,      /* 1.3, a request's: the whole body came; status LUMA_OK; value: its bytes. The
                                     handle is closed after it. A request that fails instead ends with ERROR:
                                     LUMA_E_TIMEOUT (its time passed), LUMA_E_TOO_LARGE (a body past
                                     LUMA_NET_BODY_MOST), LUMA_E_DENIED (a certificate refused, a redirect to a host
                                     luma.json does not name or from https to http: reported), LUMA_E_FORMAT (not an
                                     HTTP answer, a broken gzip or chunk), LUMA_E_LIMIT (more than 5 redirects), or
                                     the connect's (LUMA_E_UNREACHABLE, LUMA_E_NOT_FOUND, LUMA_E_IO). */
    LUMA_NET_EVENT_MESSAGE = 10   /* 1.4, a WebSocket's: one whole message; status LUMA_OK; data its bytes (valid
                                     during the call), value their count; flags LUMA_NET_WS_TEXT (UTF-8, checked)
                                     or LUMA_NET_WS_BINARY. A WebSocket's ERROR before its CONNECTED: the connect's,
                                     or LUMA_E_FORMAT (value: the HTTP status, 0 when not HTTP) when the server did
                                     not accept the upgrade. */
} LumaNetEvent;

/* 1.1: who sent a datagram. */
typedef struct LumaNetPeer {
    char host[48];  /* the address in text, 0-terminated ("192.168.1.20", "fe80::1") */
    uint16_t port;
    uint16_t reserved;
} LumaNetPeer;

/* 1.3: one header a request sends. */
typedef struct LumaNetHeader {
    const char* name;   /* 1 to 64 of letters, digits and !#$%&'*+-.^_`|~ ; not Host, Content-Length,
                           Transfer-Encoding, Connection or Accept-Encoding (the service writes those) */
    const char* value;  /* printable text and tabs, at most 4096 bytes, no line ends */
} LumaNetHeader;

/* 1.3: what http sends. Everything is copied before http returns. */
typedef struct LumaNetHttpRequest {
    uint32_t size;                 /* sizeof(LumaNetHttpRequest) */
    uint32_t timeout_ms;           /* the whole request, redirects included; 0: LUMA_NET_TIMEOUT_DEFAULT_MS */
    const char* method;            /* "GET", "HEAD", "POST", "PUT", "PATCH", "DELETE" or "OPTIONS"; null: "GET" */
    const char* url;               /* "http://" or "https://", the host as the service's tcp_connect takes it (an
                                      IPv6 address in brackets), an optional ":port", then the path and query as
                                      they go on the wire ("/" when none) */
    const LumaNetHeader* headers;  /* header_count of them (may be null with 0) */
    uint32_t header_count;
    uint32_t flags;                /* LUMA_NET_HTTP_* */
    const void* body;              /* body_bytes of it (may be null with 0); sent with Content-Length */
    uint64_t body_bytes;
} LumaNetHttpRequest;

#define LUMA_NET_HTTP_NO_REDIRECT 1u  /* a 3xx answer is the answer (HEADERS with its status), not followed */
#define LUMA_NET_HTTP_RAW 2u          /* no Accept-Encoding sent and the body given as it came */

typedef struct LumaNetLocalService {
    LumaServiceTable base;
    /* 1.0. Connects to `host`:`port` by TCP: "localhost", a loopback or local-network address in text (127.0.0.0/8,
       ::1, 10/8, 172.16/12, 192.168/16, 169.254/16, fc00::/7, fe80::/10). LUMA_PENDING and *out: CONNECTED or ERROR
       follows. A host outside those, port 0, a null host, fn or out: LUMA_E_ARGUMENT, reported; a 17th socket:
       LUMA_E_LIMIT (Service.Limit), reported. */
    /* events: CONNECTED(socket, data), ERROR(socket, status, value), DATA(socket, data), SENT(socket, value),
       CLOSED(socket, status, value, data) */
    int32_t (*tcp_connect)(const LumaMod* mod, const char* host, uint16_t port, LumaEventFn fn, void* user,
                           LumaHandle* out);
    /* 1.0. Listens on `port` (0: one the system picks; `port` gives it), on this machine only (`local_network` 0) or
       on the local network too (1). LUMA_OK and *out at once; each connection is an ACCEPTED event. A connection from
       outside this machine and the local network is closed with no event (the first one said once in the mod log,
       Net.PeerRefused). A port in use:
       LUMA_E_EXISTS (an answer, not reported); a local_network other than 0 or 1, a null fn or out: LUMA_E_ARGUMENT,
       reported; a 17th socket: LUMA_E_LIMIT, reported. */
    /* events: ACCEPTED(socket), DATA(socket, data), SENT(socket, value), CLOSED(socket, status, value, data) */
    int32_t (*tcp_listen)(const LumaMod* mod, uint16_t port, uint32_t local_network, LumaEventFn fn, void* user,
                          LumaHandle* out);
    /* 1.0. The port a listener listens on, into *out. LUMA_OK; a handle that is not this mod's listener:
       LUMA_E_HANDLE, a null out: LUMA_E_ARGUMENT, both reported. */
    int32_t (*port)(const LumaMod* mod, LumaHandle listener, uint16_t* out);
    /* 1.0. Queues `bytes` of `data` (copied) to send in order. LUMA_OK; a SENT event follows once all is sent.
       A socket not yet CONNECTED, or closed by the other side: LUMA_E_CLOSED (an answer, not reported); more than
       LUMA_NET_SEND_QUEUED_MOST queued for the mod: LUMA_E_LIMIT, nothing queued, reported; 0 bytes: LUMA_OK, nothing
       sent; a null data with bytes, a handle not this mod's socket: LUMA_E_ARGUMENT, LUMA_E_HANDLE, reported. */
    int32_t (*send)(const LumaMod* mod, LumaHandle socket, const void* data, uint64_t bytes);
    /* 1.0. Inside a DATA event's call: up to `size` of the bytes waiting into buf, the oldest first; returns how many
       were taken (the rest stay). A null buf with size 0: the bytes waiting. Outside a DATA call of this socket:
       LUMA_E_HANDLE, reported. */
    int64_t (*take)(const LumaMod* mod, LumaHandle socket, void* buf, uint64_t size);
    /* 1.0. Closes a socket or listener: bytes queued are still sent (for up to 10 s), then the connection ends; no
       event for the handle after this returns. LUMA_OK; a handle that is not this mod's open one: LUMA_E_HANDLE,
       reported. */
    int32_t (*close)(const LumaMod* mod, LumaHandle handle);
    /* 1.1. A UDP socket on `port` (0: one the system picks; `port` gives it), on this machine only (`local_network`
       0) or the local network too (1), IPv4. LUMA_OK and *out at once; anyone there may send to it (DATAGRAM: a
       datagram from outside this machine and the local network is dropped, as tcp_listen's connections); it
       sends with send_to (`send` on it: LUMA_E_HANDLE, reported). A port in use: LUMA_E_EXISTS (an answer); a
       local_network other than 0 or 1, a null fn or out: LUMA_E_ARGUMENT, reported; a 17th socket: LUMA_E_LIMIT,
       reported. */
    /* events: DATAGRAM(socket, data, from), SENT(socket, value) */
    int32_t (*udp_open)(const LumaMod* mod, uint16_t port, uint32_t local_network, LumaEventFn fn, void* user,
                        LumaHandle* out);
    /* 1.1. A UDP socket tied to `host`:`port` (the same hosts as tcp_connect): it sends there with send and hears only
       from there. LUMA_PENDING and *out: CONNECTED (or ERROR) follows, then DATAGRAM events. Mistakes as
       tcp_connect's. */
    /* events: CONNECTED(socket, data), ERROR(socket, status, value), DATAGRAM(socket, data, from), SENT(socket, value)
       */
    int32_t (*udp_connect)(const LumaMod* mod, const char* host, uint16_t port, LumaEventFn fn, void* user,
                           LumaHandle* out);
    /* 1.1. Queues one datagram of `bytes` (copied) to `host`:`port` from a udp_open socket; a SENT event follows once
       all queued is sent. `host` as tcp_connect's, IPv4. LUMA_OK; more than LUMA_NET_DATAGRAM_MOST bytes:
       LUMA_E_TOO_LARGE (an answer); the 4 MiB queue full: LUMA_E_LIMIT, reported; a host outside this machine and the
       local network, port 0, a null host, a null data with bytes: LUMA_E_ARGUMENT, reported; a handle not this mod's
       UDP socket, or one made by udp_connect: LUMA_E_HANDLE, reported. On a udp_connect socket, `send` queues one
       datagram per call (LUMA_E_TOO_LARGE past the most). A datagram the network loses is not told: UDP. */
    int32_t (*send_to)(const LumaMod* mod, LumaHandle socket, const char* host, uint16_t port, const void* data,
                       uint64_t bytes);
    /* 1.1. Inside a DATAGRAM event's call: takes the oldest datagram waiting: up to `size` bytes into buf (the rest of
       a longer one is lost), its sender into *from (may be null). Returns its whole length (0 is a datagram too);
       none waiting: LUMA_E_NOT_FOUND (an answer). Outside a DATAGRAM call of this socket: LUMA_E_HANDLE, reported;
       a null buf with size: LUMA_E_ARGUMENT, reported. `take` on a UDP socket: LUMA_E_HANDLE. */
    int64_t (*take_datagram)(const LumaMod* mod, LumaHandle socket, void* buf, uint64_t size, LumaNetPeer* from);
    /* 1.2. Connects to the endpoint `name` on this device (letters, digits, '.', '_', '-'; 1 to
       LUMA_NET_ENDPOINT_MOST bytes): the named pipe \\.\pipe\<name> on Windows (LiveSplit's is "LiveSplit"), a Unix
       socket elsewhere (Linux and Android: the abstract name <name>; Apple: <TMPDIR>/<name>.sock). A mod's own
       endpoints are "nebula.<mod id>.<name>" (ipc_listen). LUMA_PENDING and *out: CONNECTED or ERROR (LUMA_E_NOT_FOUND:
       nothing listens there; LUMA_E_TIMEOUT: every pipe instance busy for 10 s; LUMA_E_DENIED: the system refused;
       LUMA_E_IO), then the stream calls and events of tcp_connect's sockets. A wrong name, a null fn or out:
       LUMA_E_ARGUMENT, reported; a 17th socket: LUMA_E_LIMIT, reported. */
    /* events: CONNECTED(socket, data), ERROR(socket, status, value), DATA(socket, data), SENT(socket, value),
       CLOSED(socket, status, value, data) */
    int32_t (*ipc_connect)(const LumaMod* mod, const char* name, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.2. Listens on this mod's endpoint "nebula.<mod id>.<name>" (`name` as ipc_connect's): only programs on this
       device reach it; each connection is an ACCEPTED, as tcp_listen's. LUMA_OK and *out at once; something already
       listening there: LUMA_E_EXISTS (an answer); a wrong name, a null fn or out: LUMA_E_ARGUMENT, reported; a 17th
       socket: LUMA_E_LIMIT, reported. `port` on it: LUMA_E_HANDLE. */
    /* events: ACCEPTED(socket), DATA(socket, data), SENT(socket, value), CLOSED(socket, status, value, data) */
    int32_t (*ipc_listen)(const LumaMod* mod, const char* name, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.3. As tcp_connect, the bytes then encrypted by TLS 1.2 or 1.3: CONNECTED once the handshake is done and the
       host's certificate names `host` and leads to a root (the game's list, or one the mod's luma.json adds for
       itself). A certificate refused: ERROR LUMA_E_DENIED, reported (Net.CertificateRefused with the reason). Then
       the stream calls and events of tcp_connect's sockets (send and take carry the plain bytes). */
    /* events: CONNECTED(socket, data), ERROR(socket, status, value), DATA(socket, data), SENT(socket, value),
       CLOSED(socket, status, value, data) */
    int32_t (*tls_connect)(const LumaMod* mod, const char* host, uint16_t port, LumaEventFn fn, void* user,
                           LumaHandle* out);
    /* 1.3. As tcp_listen, each connection then encrypted by TLS with `certificate` (a PEM file of the chain, the
       server's first) and `key` (a PEM file of its private key), paths inside the mod's own files ("certs/a.pem").
       Each connection is an ACCEPTED at once; its socket may send at once (the bytes go after the handshake) and its
       DATA come after it; a handshake that fails is that socket's CLOSED with LUMA_E_DENIED. A file missing:
       LUMA_E_NOT_FOUND, reported; not PEM, or a key not the certificate's: LUMA_E_FORMAT, reported; a path with
       ".." or a root: LUMA_E_ARGUMENT, reported; the rest as tcp_listen's. */
    /* events: ACCEPTED(socket), DATA(socket, data), SENT(socket, value), CLOSED(socket, status, value, data) */
    int32_t (*tls_listen)(const LumaMod* mod, uint16_t port, uint32_t local_network, const char* certificate,
                          const char* key, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.3. Sends an HTTP/1.1 request to a host of this machine or the local network (as tcp_connect's). LUMA_PENDING
       and *out (the request's handle): HEADERS, DATA, then DONE, or ERROR. `close` on it cancels it (no event after).
       A request of the wrong size, a url not http(s) or past LUMA_NET_URL_MOST, a host outside those, a method not
       listed, a header the service writes or of the wrong form, more than LUMA_NET_REQUEST_HEADERS_MOST, a timeout
       past LUMA_NET_TIMEOUT_MOST_MS, an unknown flag, a null request, url, fn or out: LUMA_E_ARGUMENT, reported; a
       9th request or a 17th socket: LUMA_E_LIMIT, reported. The service sends Host, Content-Length (with a body),
       Accept-Encoding: gzip (without LUMA_NET_HTTP_RAW), Connection: close and User-Agent: Nebula/<version> <mod
       id> unless the mod names one. */
    /* events: HEADERS(socket, value, data), DATA(socket, data), DONE(socket, value), ERROR(socket, status, value) */
    int32_t (*http)(const LumaMod* mod, const LumaNetHttpRequest* request, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.4. A WebSocket client to `url` ("ws://" or "wss://", then as http's url; the host as tcp_connect's), asking
       for `protocol` (null: none; else 1 to LUMA_NET_PROTOCOL_MOST bytes of tokens separated by ", ", sent as
       Sec-WebSocket-Protocol). LUMA_PENDING and *out: CONNECTED once the server answered 101 with the right
       Sec-WebSocket-Accept (and, if it chose one, a protocol asked), or ERROR. Then MESSAGE for each message, SENT once
       what ws_send queued is sent, CLOSED at its end. Pings are answered by the service. `close` sends a close (1000)
       first, then the connection ends as a socket's; no event after. `send` and `take` on it: LUMA_E_HANDLE. A url
       not ws(s) or past LUMA_NET_URL_MOST, a host outside tcp_connect's, a protocol not tokens, a null url, fn or out:
       LUMA_E_ARGUMENT, reported; a 17th socket: LUMA_E_LIMIT, reported. */
    /* events: CONNECTED(socket, data), MESSAGE(socket, data, flags), SENT(socket, value), CLOSED(socket, status,
       value, data), ERROR(socket, status, value) */
    int32_t (*ws_connect)(const LumaMod* mod, const char* url, const char* protocol, LumaEventFn fn, void* user,
                          LumaHandle* out);
    /* 1.4. Queues one message of `bytes` (copied): `binary` 0 text (must be UTF-8), 1 bytes. LUMA_OK; SENT follows
       once all queued is sent. Before CONNECTED or after the close: LUMA_E_CLOSED (an answer); past
       LUMA_NET_MESSAGE_MOST: LUMA_E_TOO_LARGE (an answer); the send queue full: LUMA_E_LIMIT, reported; text not
       UTF-8, a binary other than 0 or 1, a null data with bytes: LUMA_E_ARGUMENT, reported; a handle not this mod's
       WebSocket: LUMA_E_HANDLE, reported. */
    int32_t (*ws_send)(const LumaMod* mod, LumaHandle socket, const void* data, uint64_t bytes, uint32_t binary);
    /* 1.4. Game thread. This machine's IPv4 address on the local network in text ("192.168.1.20"): the one a phone on
       the same network reaches it at (the interface the system routes to the network by; no packet is sent), into
       buf with its NUL. Returns its length without the NUL; a null buf with size 0 asks the length only. No local
       network (only loopback, or a public address): LUMA_E_NOT_FOUND (an answer, not reported); a buf too short:
       LUMA_E_TOO_LARGE (an answer; nothing written); a null buf with size above 0: LUMA_E_ARGUMENT, reported. */
    int64_t (*address)(const LumaMod* mod, char* buf, uint64_t size);
} LumaNetLocalService;

/* A host name or address in text: at most this many bytes. */
#define LUMA_NET_HOST_MOST 253u

typedef struct LumaNetInternetService {
    LumaServiceTable base;
    /* 1.0. Connects to `host`:`port` by TCP. `host` is a name (looked up off the game thread) or an address in text;
       luma.json's net.internet permission must name it: the same name in any letter case, a "*.<name>" entry for any
       name under <name>, or "*" for every host. Only public addresses are reached (never this machine's or the local
       network's, whatever a name resolves to: that is net.local's). LUMA_PENDING and *out: CONNECTED or ERROR
       follows. A host luma.json does not name: LUMA_E_DENIED, reported (Net.HostNotNamed); a host not a name or an
       address (or past LUMA_NET_HOST_MOST), port 0, a null host, fn or out: LUMA_E_ARGUMENT, reported; a 17th socket:
       LUMA_E_LIMIT, reported. */
    /* events: CONNECTED(socket, data), ERROR(socket, status, value), DATA(socket, data), SENT(socket, value),
       CLOSED(socket, status, value, data) */
    int32_t (*tcp_connect)(const LumaMod* mod, const char* host, uint16_t port, LumaEventFn fn, void* user,
                           LumaHandle* out);
    /* 1.0. LUMA_E_DENIED, an answer (not reported): no server on the open internet in 1.0; net.local listens. Here so
       both services keep one layout (services-registry 2 item 4). */
    /* events: ACCEPTED(socket), DATA(socket, data), SENT(socket, value), CLOSED(socket, status, value, data) */
    int32_t (*tcp_listen)(const LumaMod* mod, uint16_t port, uint32_t local_network, LumaEventFn fn, void* user,
                          LumaHandle* out);
    /* 1.0. As net.local's; this service has no listener, so a handle is always LUMA_E_HANDLE, reported. */
    int32_t (*port)(const LumaMod* mod, LumaHandle listener, uint16_t* out);
    /* 1.0. As net.local's send, take and close, for this service's sockets (a handle belongs to the service that
       made it). */
    int32_t (*send)(const LumaMod* mod, LumaHandle socket, const void* data, uint64_t bytes);
    int64_t (*take)(const LumaMod* mod, LumaHandle socket, void* buf, uint64_t size);
    int32_t (*close)(const LumaMod* mod, LumaHandle handle);
    /* 1.1. LUMA_E_DENIED, an answer: a socket anyone on the internet may send to is a server (as tcp_listen). */
    /* events: DATAGRAM(socket, data, from), SENT(socket, value) */
    int32_t (*udp_open)(const LumaMod* mod, uint16_t port, uint32_t local_network, LumaEventFn fn, void* user,
                        LumaHandle* out);
    /* 1.1. As tcp_connect, a UDP socket tied to `host`:`port` (its name looked up, public addresses only, the first
       one taken: UDP cannot tell which answers). It hears only from there. */
    /* events: CONNECTED(socket, data), ERROR(socket, status, value), DATAGRAM(socket, data, from), SENT(socket, value)
       */
    int32_t (*udp_connect)(const LumaMod* mod, const char* host, uint16_t port, LumaEventFn fn, void* user,
                           LumaHandle* out);
    /* 1.1. LUMA_E_DENIED, an answer (only udp_open's sockets send_to). */
    int32_t (*send_to)(const LumaMod* mod, LumaHandle socket, const char* host, uint16_t port, const void* data,
                       uint64_t bytes);
    /* 1.1. As net.local's. */
    int64_t (*take_datagram)(const LumaMod* mod, LumaHandle socket, void* buf, uint64_t size, LumaNetPeer* from);
    /* 1.2. LUMA_E_DENIED, answers: endpoints on this device are net.local's. */
    /* events: CONNECTED(socket, data), ERROR(socket, status, value), DATA(socket, data), SENT(socket, value),
       CLOSED(socket, status, value, data) */
    int32_t (*ipc_connect)(const LumaMod* mod, const char* name, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.2. As ipc_connect. */
    /* events: ACCEPTED(socket), DATA(socket, data), SENT(socket, value), CLOSED(socket, status, value, data) */
    int32_t (*ipc_listen)(const LumaMod* mod, const char* name, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.3. As net.local's tls_connect, to the hosts and by the rules of this service's tcp_connect. */
    /* events: CONNECTED(socket, data), ERROR(socket, status, value), DATA(socket, data), SENT(socket, value),
       CLOSED(socket, status, value, data) */
    int32_t (*tls_connect)(const LumaMod* mod, const char* host, uint16_t port, LumaEventFn fn, void* user,
                           LumaHandle* out);
    /* 1.3. LUMA_E_DENIED, an answer: a server is net.local's. */
    /* events: ACCEPTED(socket), DATA(socket, data), SENT(socket, value), CLOSED(socket, status, value, data) */
    int32_t (*tls_listen)(const LumaMod* mod, uint16_t port, uint32_t local_network, const char* certificate,
                          const char* key, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.3. As net.local's http, to the hosts luma.json names (tcp_connect's rules; each redirect's host too). */
    /* events: HEADERS(socket, value, data), DATA(socket, data), DONE(socket, value), ERROR(socket, status, value) */
    int32_t (*http)(const LumaMod* mod, const LumaNetHttpRequest* request, LumaEventFn fn, void* user, LumaHandle* out);
    /* 1.4. As net.local's, to the hosts luma.json names (tcp_connect's rules). */
    /* events: CONNECTED(socket, data), MESSAGE(socket, data, flags), SENT(socket, value), CLOSED(socket, status,
       value, data), ERROR(socket, status, value) */
    int32_t (*ws_connect)(const LumaMod* mod, const char* url, const char* protocol, LumaEventFn fn, void* user,
                          LumaHandle* out);
    /* 1.4. As net.local's. */
    int32_t (*ws_send)(const LumaMod* mod, LumaHandle socket, const void* data, uint64_t bytes, uint32_t binary);
} LumaNetInternetService;

#ifdef __cplusplus
}
#endif

#endif /* LUMA_NET_H */
