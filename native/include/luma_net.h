/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's net services (contract services-net; plan 2.7). Layer 1: `net.local` TCP on this machine and the local
   network. One permission: "net.local" (consent 2.x). Every call is the game thread's, never blocks, and what waits
   is an event at a frame's start (services-registry 6). */
#ifndef LUMA_NET_H
#define LUMA_NET_H

#include <luma_services.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_NET_LOCAL_NAME "net.local"
#define LUMA_NET_LOCAL_MAJOR 1u
#define LUMA_NET_LOCAL_MINOR 0u

#define LUMA_NET_SOCKETS_MOST 16u            /* sockets and listeners a mod holds at once */
#define LUMA_NET_SEND_QUEUED_MOST (4u << 20u) /* bytes a mod's sockets hold queued, not yet sent */
#define LUMA_NET_RECEIVED_MOST (1u << 20u)    /* bytes a socket holds received and not taken; reading pauses past it */

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
    LUMA_NET_EVENT_ERROR = 6      /* a connect that failed: LUMA_E_UNREACHABLE (nobody listens, no route),
                                     LUMA_E_TIMEOUT (10 s), LUMA_E_IO; the handle is closed after it */
} LumaNetEvent;

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
} LumaNetLocalService;

#ifdef __cplusplus
}
#endif

#endif /* LUMA_NET_H */
