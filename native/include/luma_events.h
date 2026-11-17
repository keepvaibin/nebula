/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's events service (docs/modding/reference/services/events.md): the game's events (a coin, a star, a stage
   starting, an object killed, a sound starting) told to a mod at the start of the frame after they happened, where its
   code may do anything. To act at the moment itself (cancel a hit, change a count), hook the function that raises the
   event (docs/modding/reference/events.md names each). No permission. Import: LUMA_SERVICE(slot, "events", "1.0"). */
#ifndef LUMA_EVENTS_H
#define LUMA_EVENTS_H

#include <stdint.h>

#include "luma_services.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_EVENTS_NAME "events"
#define LUMA_EVENTS_MAJOR 1u
#define LUMA_EVENTS_MINOR 0u

/* The bytes of an event's name a LumaEventsGame holds, its NUL included. */
#define LUMA_EVENTS_NAME_MOST 128u

/* LumaEventsGame.flags. */
#define LUMA_EVENTS_OTHER_THREAD 1u /* raised off the game thread (the game raises its events on the game thread, stage loads included) */

/* The game's events (LumaEvent.kind), as docs/modding/reference/events.md lists them; watch's `kinds` has bit
   (1 << kind) for each. Each kind's data is a LumaEventsGame. */
typedef enum LumaEventsEvent {
    LUMA_EVENTS_EVENT_STAGE_FRAME = 1,        /* data, a LumaEventsGame (valid during the call): a frame of a stage ran */
    LUMA_EVENTS_EVENT_STAGE_START = 2,        /* data, a LumaEventsGame (valid during the call): a stage started; name the stage, value the mission */
    LUMA_EVENTS_EVENT_PLAYER_DAMAGE = 3,      /* data, a LumaEventsGame (valid during the call): the player lost health */
    LUMA_EVENTS_EVENT_PLAYER_MISS = 4,        /* data, a LumaEventsGame (valid during the call): the player lost a life; value the cause (0 a fall out of the stage) */
    LUMA_EVENTS_EVENT_POWER_UP = 5,           /* data, a LumaEventsGame (valid during the call): the player's power-up changed; value the new one (NEBULA_POWER_UP_*) */
    LUMA_EVENTS_EVENT_COIN = 6,               /* data, a LumaEventsGame (valid during the call): coins were added; value how many */
    LUMA_EVENTS_EVENT_STAR_BIT = 7,           /* data, a LumaEventsGame (valid during the call): star bits were added; value how many */
    LUMA_EVENTS_EVENT_STAR_GET = 8,           /* data, a LumaEventsGame (valid during the call): a star was collected; name the galaxy, value the star */
    LUMA_EVENTS_EVENT_CUTSCENE_START = 9,     /* data, a LumaEventsGame (valid during the call): a cutscene started; name the cutscene */
    LUMA_EVENTS_EVENT_CUTSCENE_END = 10,      /* data, a LumaEventsGame (valid during the call): a cutscene ended; name the cutscene */
    LUMA_EVENTS_EVENT_ACTOR_APPEAR = 11,      /* data, a LumaEventsGame (valid during the call): an object appeared; actor the object, name its name */
    LUMA_EVENTS_EVENT_ACTOR_KILL = 12,        /* data, a LumaEventsGame (valid during the call): an object was killed; actor the object, name its name */
    LUMA_EVENTS_EVENT_PLAYER_SPIN = 13,       /* data, a LumaEventsGame (valid during the call): the player started a spin */
    LUMA_EVENTS_EVENT_PLAYER_JUMP = 14,       /* data, a LumaEventsGame (valid during the call): the player jumped off the ground */
    LUMA_EVENTS_EVENT_PLAYER_LAND = 15,       /* data, a LumaEventsGame (valid during the call): the player landed */
    LUMA_EVENTS_EVENT_PAUSE = 16,             /* data, a LumaEventsGame (valid during the call): the pause menu opened (value 1) or closed (value 0) */
    LUMA_EVENTS_EVENT_STAGE_END = 17,         /* data, a LumaEventsGame (valid during the call): a stage ended; name the stage */
    LUMA_EVENTS_EVENT_GAME_SAVE = 18,         /* data, a LumaEventsGame (valid during the call): the game wrote the save */
    LUMA_EVENTS_EVENT_POWER_STAR_APPEAR = 19, /* data, a LumaEventsGame (valid during the call): a power star appeared; value its number */
    LUMA_EVENTS_EVENT_TALK_LINE = 20,         /* data, a LumaEventsGame (valid during the call): a line of talk opened; actor the speaker, name the message id */
    LUMA_EVENTS_EVENT_TALK_CHOICE = 21,       /* data, a LumaEventsGame (valid during the call): the player answered a yes/no question; value 1 yes, 0 no; actor who asked, name the message id */
    LUMA_EVENTS_EVENT_CUTSCENE_PART = 22,     /* data, a LumaEventsGame (valid during the call): a part of a cutscene started; value its index, name its name */
    LUMA_EVENTS_EVENT_CAMERA_CHANGE = 23,     /* data, a LumaEventsGame (valid during the call): the camera changed; value 1 an event camera, 0 an area camera; name its id */
    LUMA_EVENTS_EVENT_SOUND_START = 24,       /* data, a LumaEventsGame (valid during the call): a sound effect started; value its number, name its name */
    LUMA_EVENTS_EVENT_EFFECT_START = 25,      /* data, a LumaEventsGame (valid during the call): an object started an effect; actor the object, name the effect */
    LUMA_EVENTS_EVENT_SCREEN = 26,            /* data, a LumaEventsGame (valid during the call): a screen layout was shown (value 1) or removed (value 0); name its name */
    LUMA_EVENTS_EVENT_ACTOR_MESSAGE = 27,     /* data, a LumaEventsGame (valid during the call): an object received a message; value the message, actor the receiver, name its name */
    LUMA_EVENTS_EVENT_PLAYER_ACTION = 28      /* data, a LumaEventsGame (valid during the call): the player started an action; value the action */
} LumaEventsEvent;

/* Every kind watch takes: bits 1 to 28. */
#define LUMA_EVENTS_ALL 0x1FFFFFFEu

/* One game event as it happened (LumaEvent.data of every kind). */
typedef struct LumaEventsGame {
    uint32_t size;      /* sizeof(LumaEventsGame) in the game that runs */
    int32_t value;      /* the kind's number (LumaEvent.value holds it too); 0 where the kind has none */
    uint32_t flags;     /* LUMA_EVENTS_OTHER_THREAD */
    uint32_t name_size; /* the name's length in bytes, without the NUL, even when `name` holds less (0: no name) */
    uint64_t actor;     /* the object's address (a LiveActor *); 0 where the kind has none, or once its scene is gone */
    char name[128];     /* the name, UTF-8 with a NUL, cut at LUMA_EVENTS_NAME_MOST - 1 bytes */
} LumaEventsGame;

typedef struct LumaEventsService {
    LumaServiceTable base;
    /* 1.0. Game thread. `fn` gets each game event of `kinds` (bit 1 << LUMA_EVENTS_EVENT_*; LUMA_EVENTS_ALL for all)
       at the start of the frame after it happened, in the order they happened, every event of that frame in that
       call of the frame's start; never inside this call. A later call replaces kinds, fn and user; kinds 0 or a null
       fn stops the events (those not yet given are dropped). LUMA_OK. A bit outside LUMA_EVENTS_ALL: LUMA_E_ARGUMENT,
       reported (Service.Argument "kinds").
       Each event's `status` is LUMA_OK, or LUMA_E_MEMORY when events between the previous one and this one were lost
       (no memory to keep them). `actor` is the object the event was raised for: the game keeps its objects until their
       scene (a stage, the title) ends, so it is that object whenever it is not 0; it is 0 once that scene has ended. */
    /* events: STAGE_FRAME(data), STAGE_START(data), PLAYER_DAMAGE(data), PLAYER_MISS(data), POWER_UP(data), COIN(data),
       STAR_BIT(data), STAR_GET(data), CUTSCENE_START(data), CUTSCENE_END(data), ACTOR_APPEAR(data), ACTOR_KILL(data),
       PLAYER_SPIN(data), PLAYER_JUMP(data), PLAYER_LAND(data), PAUSE(data), STAGE_END(data), GAME_SAVE(data),
       POWER_STAR_APPEAR(data), TALK_LINE(data), TALK_CHOICE(data), CUTSCENE_PART(data), CAMERA_CHANGE(data),
       SOUND_START(data), EFFECT_START(data), SCREEN(data), ACTOR_MESSAGE(data), PLAYER_ACTION(data) */
    int32_t (*watch)(const LumaMod* mod, uint32_t kinds, LumaEventFn fn, void* user);
} LumaEventsService;

#ifdef __cplusplus
}
#endif

#endif
