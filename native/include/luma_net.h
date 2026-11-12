/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's net services (contract services-net; plan 2.7). Layer 1: `net.local` TCP on this machine and the local
   network, permission "net.local". Layer 2: `net.internet` TCP to the hosts the mod's luma.json names, by name or
   address, permission "net.internet" (the player sees the hosts). Every call is the game thread's, never blocks, and
   what waits is an event at a frame's start (services-registry 6). Both share the events, the limits and the
   socket calls' rules; a handle belongs to the service that made it. Minor 1 of both adds UDP: a socket open to
   anyone on this machine or the local network (net.local's udp_open), one tied to a single peer (udp_connect,
   both services), datagrams sent whole and taken whole. */
#ifndef LUMA_NET_H
#define LUMA_NET_H

#include <luma_services.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_NET_LOCAL_NAME "net.local"
#define LUMA_NET_LOCAL_MAJOR 1u
#define LUMA_NET_LOCAL_MINOR 1u
#define LUMA_NET_INTERNET_NAME "net.internet"
#define LUMA_NET_INTERNET_MAJOR 1u
#define LUMA_NET_INTERNET_MINOR 1u

#define LUMA_NET_SOCKETS_MOST 16u            /* sockets and listeners a mod holds at once, both services together */
#define LUMA_NET_SEND_QUEUED_MOST (4u << 20u) /* bytes a mod's sockets hold queued, not yet sent */
#define LUMA_NET_RECEIVED_MOST (1u << 20u)    /* bytes a socket holds received and not taken; reading pauses past it */
#define LUMA_NET_DATAGRAM_MOST 65507u        /* 1.1: bytes of one datagram (IPv4's most) */

/* Events (LumaEvent.kind) to the fn given at connect or listen. `handle` is the socket's (ACCEPTED: the listener's). */
typedef enum LumaNetEvent {
    LUMA_NET_EVENT_CONNECTED = 1, /* status LUMA_OK; value 0. The socket may send. */
    LUMA_NET_EVENT_ACCEPTED = 2,  /* a listener's: status LUMA_OK; value: the new socket's handle, open, its events to
                                     the listener's fn and user */
    LUMA_NET_EVENT_DATA = 3,      /* status LUMA_OK; value: the bytes waiting; `take` inside this call. Bytes not taken
                                     stay, and another DATA follows at a later frame. */
    LUMA_NET_EVENT_SENT = 4,      /* status LUMA_OK; value: the bytes sent since the last SENT; nothing is queued now */
    LUMA_NET_EVENT_CLOSED = 5,    /* the other side closed (status LUMA_OK) or the connection broke (LUMA_E_IO); the
                                     handle is closed after this event (bytes still waiting came first as DATA) */
    LUMA_NET_EVENT_ERROR = 6,     /* a connect that failed: LUMA_E_UNREACHABLE (nobody listens, no route),
                                     LUMA_E_TIMEOUT (10 s, the name's lookup included), LUMA_E_IO; net.internet
                                     also LUMA_E_NOT_FOUND (the name has no address) and LUMA_E_DENIED (its every
                                     address is this machine's, the local network's or not one a host has); the
                                     handle is closed after it */
    LUMA_NET_EVENT_DATAGRAM = 7   /* 1.1, a UDP socket's: status LUMA_OK; value: the datagrams waiting;
                                     `take_datagram` inside this call, one datagram each. Datagrams not taken stay,
                                     and another DATAGRAM follows at a later frame. */
} LumaNetEvent;

/* 1.1: who sent a datagram. */
typedef struct LumaNetPeer {
    char host[48];  /* the address in text, 0-terminated ("192.168.1.20", "fe80::1") */
    uint16_t port;
    uint16_t reserved;
} LumaNetPeer;

typedef struct LumaNetLocalService {
    LumaServiceTable base;
    /* 1.0. Connects to `host`:`port` by TCP: "localhost", a loopback or local-network address in text (127.0.0.0/8,
       ::1, 10/8, 172.16/12, 192.168/16, 169.254/16, fc00::/7, fe80::/10). LUMA_PENDING and *out: CONNECTED or ERROR
       follows. A host outside those, port 0, a null host, fn or out: LUMA_E_ARGUMENT, reported; a 17th socket:
       LUMA_E_LIMIT (Service.Limit), reported. */
    int32_t (*tcp_connect)(const LumaMod* mod, const char* host, uint16_t port, LumaEventFn fn, void* user,
                           LumaHandle* out);
    /* 1.0. Listens on `port` (0: one the system picks; `port` gives it), on this machine only (`local_network` 0) or
       on the local network too (1). LUMA_OK and *out at once; each connection is an ACCEPTED event. A port in use:
       LUMA_E_EXISTS (an answer, not reported); a local_network other than 0 or 1, a null fn or out: LUMA_E_ARGUMENT,
       reported; a 17th socket: LUMA_E_LIMIT, reported. */
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
       0) or the local network too (1), IPv4. LUMA_OK and *out at once; anyone there may send to it (DATAGRAM); it
       sends with send_to (`send` on it: LUMA_E_HANDLE, reported). A port in use: LUMA_E_EXISTS (an answer); a
       local_network other than 0 or 1, a null fn or out: LUMA_E_ARGUMENT, reported; a 17th socket: LUMA_E_LIMIT,
       reported. */
    int32_t (*udp_open)(const LumaMod* mod, uint16_t port, uint32_t local_network, LumaEventFn fn, void* user,
                        LumaHandle* out);
    /* 1.1. A UDP socket tied to `host`:`port` (the same hosts as tcp_connect): it sends there with send and hears only
       from there. LUMA_PENDING and *out: CONNECTED (or ERROR) follows, then DATAGRAM events. Mistakes as
       tcp_connect's. */
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
    int32_t (*tcp_connect)(const LumaMod* mod, const char* host, uint16_t port, LumaEventFn fn, void* user,
                           LumaHandle* out);
    /* 1.0. LUMA_E_DENIED, an answer (not reported): no server on the open internet in 1.0; net.local listens. Here so
       both services keep one layout (services-registry 2 item 4). */
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
    int32_t (*udp_open)(const LumaMod* mod, uint16_t port, uint32_t local_network, LumaEventFn fn, void* user,
                        LumaHandle* out);
    /* 1.1. A UDP socket tied to `host`:`port`, the hosts and the rules of this service's tcp_connect (its name looked
       up, public addresses only, the first one taken: UDP cannot tell which answers). It hears only from there. */
    int32_t (*udp_connect)(const LumaMod* mod, const char* host, uint16_t port, LumaEventFn fn, void* user,
                           LumaHandle* out);
    /* 1.1. LUMA_E_DENIED, an answer (only udp_open's sockets send_to). */
    int32_t (*send_to)(const LumaMod* mod, LumaHandle socket, const char* host, uint16_t port, const void* data,
                       uint64_t bytes);
    /* 1.1. As net.local's. */
    int64_t (*take_datagram)(const LumaMod* mod, LumaHandle socket, void* buf, uint64_t size, LumaNetPeer* from);
} LumaNetInternetService;

#ifdef __cplusplus
}
#endif

#endif /* LUMA_NET_H */
