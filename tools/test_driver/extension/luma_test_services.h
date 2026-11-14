/* SPDX-License-Identifier: GPL-3.0-only */
/* The test services the harness's host half provides (services-registry 14): "test.svc" 1.3, no permission, and
   "test.secret" 1.0, permission "test.secret". Only in a harness run; never in an install. The registry's tests
   import them. */
#ifndef LUMA_TEST_SERVICES_H
#define LUMA_TEST_SERVICES_H

#include <stdint.h>

#include "luma.h"
#include "luma_services.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_TEST_SVC_MAJOR 1u
#define LUMA_TEST_SVC_MINOR 3u
#define LUMA_TEST_SECRET_MAJOR 1u
#define LUMA_TEST_SECRET_MINOR 0u

/* LumaEvent.kind of test.svc: an event the service posted for the mod. `value` is its number in the mod's post order
   (from 0), `handle` the call's request id (never 0). */
#define LUMA_TEST_SVC_EVENT_POSTED 1u

/* The most events one post_events call asks for. */
#define LUMA_TEST_SVC_MOST_EVENTS 1024u

/* take_struct's argument as 1.3 has it: 16 bytes. */
typedef struct LumaTestSvcStruct {
    uint32_t size; /* sizeof(LumaTestSvcStruct) as the mod was built */
    uint32_t a;
    uint64_t b;
} LumaTestSvcStruct;

/* What test.svc knows of one mod, asked by any mod by the mod's id (after that mod's end too). */
typedef struct LumaTestSvcStats {
    uint32_t size;          /* sizeof(LumaTestSvcStats) as the asking mod was built */
    uint32_t known;         /* 1: the mod began in this session */
    uint64_t posted;        /* events posted for it */
    uint64_t delivered;     /* events given to its function */
    uint64_t dropped;       /* nodes the kernel dropped (its taking out) */
    uint64_t begin_seq;     /* the order of the provider's moments in the session, from 1; 0: not yet */
    uint64_t gone_seq;
    uint64_t end_seq;
    uint64_t after_gone;    /* deliveries after its mod_gone: 0 is right */
    uint64_t delivered_seq; /* the provider's moment of its latest delivery (the same count as begin_seq); 0: none */
} LumaTestSvcStats;

typedef struct LumaTestSvcService {
    LumaServiceTable base;
    /* 1.0. Game thread: *out = value + 1. Off the game thread LUMA_E_THREAD, reported (Service.Thread). */
    int32_t (*echo)(const LumaMod* mod, uint64_t value, uint64_t* out);
    /* 1.1. Game thread: posts `count` events for the mod, from a thread of the service, all before the next frame's
       start; LUMA_PENDING. More than LUMA_TEST_SVC_MOST_EVENTS: LUMA_E_LIMIT, reported (Service.Limit); 0 or a null
       fn: LUMA_E_ARGUMENT, reported (Service.Argument). */
    int32_t (*post_events)(const LumaMod* mod, uint32_t count, LumaEventFn fn, void* user);
    /* 1.2. Game thread: one event whose node is posted twice before the frame (given once). */
    int32_t (*post_twice)(const LumaMod* mod, LumaEventFn fn, void* user);
    /* 1.2. Game thread: one event whose delivery posts its node again once (given in two frames, one after the other). */
    int32_t (*post_again)(const LumaMod* mod, LumaEventFn fn, void* user);
    /* 1.3. Game thread: LUMA_OK when `s` is a whole LumaTestSvcStruct; null: LUMA_E_ARGUMENT (Service.Argument); a
       `size` below 16: LUMA_E_ARGUMENT (Service.StructSize with the size and 16). Both reported. */
    int32_t (*take_struct)(const LumaMod* mod, const LumaTestSvcStruct* s);
    /* 1.3. Game thread: what the service knows of the mod named `mod_id`. LUMA_OK; null text or out: LUMA_E_ARGUMENT
       (Service.Argument); a `size` below the whole struct (it came whole in 1.3): LUMA_E_ARGUMENT (Service.StructSize
       with the size and sizeof). Both reported. */
    int32_t (*stats)(const LumaMod* mod, const char* mod_id, LumaTestSvcStats* out);
} LumaTestSvcService;

typedef struct LumaTestSecretService {
    LumaServiceTable base;
    /* 1.0. Game thread: LUMA_OK. Bound only for a mod granted "test.secret". */
    int32_t (*ping)(const LumaMod* mod);
} LumaTestSecretService;

#ifdef __cplusplus
}
#endif

#endif
