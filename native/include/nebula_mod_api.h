/* SPDX-License-Identifier: GPL-3.0-only */
/* Nebula's plugin interface: what a .luma's code calls and is called by. Plain C, on its own: any compiler that makes a
   native library for the device can build against it.

   A plugin exports the two functions NEBULA_PLUGIN_ENTRY makes. Fields are only ever added at the end of NebulaModApi:
   a plugin built against this header keeps working on every later Nebula. */
#ifndef NEBULA_MOD_API_H
#define NEBULA_MOD_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEBULA_MOD_API_VERSION 1u

#if defined(_WIN32)
#define NEBULA_PLUGIN_EXPORT __declspec(dllexport)
#else
#define NEBULA_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/* A live object in the game: an enemy, a coin, the player. On the game's side it is a LiveActor. Valid until the stage
   ends; keep none across NEBULA_MOD_EVENT_STAGE_START. */
typedef struct NebulaActor NebulaActor;

/* Who a plugin is, for option(). */
typedef struct NebulaPlugin {
    uint32_t size;
    const char* id;       /* the mod's id */
    const char* version;  /* the mod's version */
} NebulaPlugin;

/* Called on the thread the event happens on: objects made while a stage loads are reported on the loading thread,
   the rest on the game's. */
typedef enum NebulaModEvent {
    /* Every frame, in any scene: menus too. player() is null here. */
    NEBULA_MOD_EVENT_FRAME = 0,
    /* Every frame of a stage, before its objects move. */
    NEBULA_MOD_EVENT_STAGE_FRAME,
    /* A stage has started. name: the stage. value: the mission. */
    NEBULA_MOD_EVENT_STAGE_START,
    /* The player is about to lose one health. Return 0 to refuse the hit. */
    NEBULA_MOD_EVENT_PLAYER_DAMAGE,
    /* The player is about to die. value: how. */
    NEBULA_MOD_EVENT_PLAYER_MISS,
    /* The player's power-up is about to change. value: the new one (NEBULA_POWER_UP_*); what you return is used. */
    NEBULA_MOD_EVENT_POWER_UP,
    /* Coins are about to be added. value: how many; what you return is added. */
    NEBULA_MOD_EVENT_COIN,
    /* Star bits are about to be added. value: how many; what you return is added. */
    NEBULA_MOD_EVENT_STAR_BIT,
    /* A star has been won. name: the galaxy. value: the star's number. */
    NEBULA_MOD_EVENT_STAR_GET,
    /* A cutscene starts or ends. name: the cutscene. */
    NEBULA_MOD_EVENT_CUTSCENE_START,
    NEBULA_MOD_EVENT_CUTSCENE_END,
    /* Any object appears or dies. actor: the object. name: its name. Subscribing to these costs a call per object. */
    NEBULA_MOD_EVENT_ACTOR_APPEAR,
    NEBULA_MOD_EVENT_ACTOR_KILL,
    NEBULA_MOD_EVENT_COUNT
} NebulaModEvent;

typedef struct NebulaModEventData {
    int32_t value;
    NebulaActor* actor;
    const char* name;
} NebulaModEventData;

/* Plugins subscribed to one event run in list order, top of the Mods page last; each sees what the last one returned.
   Return data->value to change nothing. */
typedef int32_t (*NebulaModCallback)(const NebulaModEventData* data, void* user);

/* Numbers that can be read. */
typedef enum NebulaModValue {
    NEBULA_VALUE_LIFE = 0,
    NEBULA_VALUE_MAX_LIFE,
    NEBULA_VALUE_POWER_UP,
    NEBULA_VALUE_ON_GROUND,
    NEBULA_VALUE_STAR_BITS,
    NEBULA_VALUE_COINS,
    NEBULA_VALUE_LIVES,
    NEBULA_VALUE_MISSION
} NebulaModValue;

/* Numbers that can be set. */
typedef enum NebulaModSet {
    NEBULA_SET_LIFE = 0,
    NEBULA_SET_MAX_LIFE,
    NEBULA_SET_POWER_UP,
    NEBULA_SET_ADD_COINS,
    NEBULA_SET_ADD_STAR_BITS,
    NEBULA_SET_ADD_LIVES
} NebulaModSet;

/* What the player's power-ups are numbered. */
typedef enum NebulaPowerUp {
    NEBULA_POWER_UP_NONE = 0,
    NEBULA_POWER_UP_INVINCIBLE = 1,
    NEBULA_POWER_UP_FIRE = 2,
    NEBULA_POWER_UP_ICE = 3,
    NEBULA_POWER_UP_BEE = 4,
    NEBULA_POWER_UP_SPRING = 5,
    NEBULA_POWER_UP_BOO = 6,
    NEBULA_POWER_UP_FLYING = 7,
    NEBULA_POWER_UP_TORNADO = 9
} NebulaPowerUp;

/* What an object can start by name. */
typedef enum NebulaModPlay {
    NEBULA_PLAY_ANIMATION = 0, /* its action: animations, effects and sounds together */
    NEBULA_PLAY_EFFECT,
    NEBULA_PLAY_SOUND,
    NEBULA_PLAY_PLAYER_ANIMATION /* on the player, by the animation's name */
} NebulaModPlay;

/* An object that a level places by `name`, or that spawn() makes. Everything it does is these calls; a plugin built with
   the game's headers can subclass LiveActor instead, through register_creator. */
typedef struct NebulaObjectClass {
    uint32_t size;
    const char* name;
    const char* archive; /* its model and animations: ObjectData/<archive>.arc. Null: nothing to see */
    float radius;        /* what it touches with */
    void* user;
    void (*start)(NebulaActor* actor, void* user);
    void (*update)(NebulaActor* actor, void* user);
    void (*touch_player)(NebulaActor* actor, void* user);
    /* The player attacked it. Return 1 if it took the attack. */
    int32_t (*attacked)(NebulaActor* actor, void* user);
} NebulaObjectClass;

/* A galaxy beside the game's. Its files are StageData/<name>/... as any galaxy's. */
typedef struct NebulaGalaxy {
    uint32_t size;
    const char* name;
    const char* map_pane;           /* its icon's pane in the map's layout (MapGrandGalaxy). Null: not on the map */
    const char* open_condition[3];  /* the event flags that open it, or null */
    int32_t stars_to_open;          /* power stars it takes to open */
    int32_t dome;                   /* the dome it belongs to: 1 to 6, 0 for none */
    int32_t has_map_position;       /* 1: the game makes its icon's pane, at map_position */
    float map_position[2];          /* from the middle of its dome's page, or of the grand map with no dome: right and up, 608 wide by 456 high */
} NebulaGalaxy;

/* A power-up the game doesn't have. A plugin gives it to the player with set_value(NEBULA_SET_POWER_UP, its number). */
typedef struct NebulaPowerUpClass {
    uint32_t size;
    const char* name;
    int32_t frames;       /* how long it lasts, at 60 a second. 0: until something ends it */
    const char* music;    /* the music while it lasts: a BGM_ name, or null */
    int32_t color_frame;  /* the player's colours: a frame of Mario.arc's ColorChange (0 his own, 1 Fire's). A mod's Mario.arc can add more. */
    void* user;
    void (*start)(void* user);
    void (*update)(void* user); /* every frame it lasts */
    void (*end)(void* user);
} NebulaPowerUpClass;

typedef struct NebulaModApi {
    uint32_t size;
    uint32_t version;
    /* The game's headers this game was built from; see nebula_game_sdk.h. */
    uint64_t game_abi;

    /* One line in the game's log. */
    void (*log)(const NebulaPlugin* self, const char* text);
    /* The choice a player made for one of the mod's options, by option id: its choice id. Null: no such option. */
    const char* (*option)(const NebulaPlugin* self, const char* id);
    /* Only in nebula_plugin_start. 0: done. */
    int32_t (*subscribe)(NebulaModEvent event, NebulaModCallback callback, void* user);
    int32_t (*register_object)(const NebulaObjectClass* object);
    /* `create` makes the game's own object (a NameObj) from its placed name; `archive` is loaded for it, or null. */
    int32_t (*register_creator)(const char* name, void* (*create)(const char* name), const char* archive);
    /* Run `replacement` in place of the game's function at `target`; `original` gets a call to what was there before it,
       which may be another plugin's. A function the compiler inlined everywhere can't be reached this way: use events.
       Only where the device allows it: Windows x64 for now. */
    int32_t (*hook)(void* target, void* replacement, void** original);
    /* A game function by its symbol: on Windows, as dumpbin /exports names it. Null: no such function. */
    void* (*find_function)(const char* symbol);

    /* Null when no stage is running, or outside a stage's frame. */
    NebulaActor* (*player)(void);
    void (*get_position)(NebulaActor* actor, float* xyz);
    void (*set_position)(NebulaActor* actor, const float* xyz);
    void (*get_velocity)(NebulaActor* actor, float* xyz);
    void (*set_velocity)(NebulaActor* actor, const float* xyz);
    int32_t (*value)(NebulaModValue what);
    void (*set_value)(NebulaModSet what, int32_t value);
    void (*kill_player)(void);
    void (*jump_player)(const float* velocity);
    void (*appear)(NebulaActor* actor);
    void (*kill)(NebulaActor* actor);
    int32_t (*is_dead)(NebulaActor* actor);
    void (*show_model)(NebulaActor* actor, int32_t show);
    /* 1: it started. 0: the object has no such thing, or nothing to play it with. */
    int32_t (*play)(NebulaActor* actor, NebulaModPlay what, const char* name);
    const char* (*actor_name)(NebulaActor* actor);
    const char* (*stage_name)(void);
    /* A registered object at a place. Its archive must already be loaded: a level places it, or the stage holds it.
       Null: it can't be made now. */
    NebulaActor* (*spawn)(const char* name, const float* xyz);
    /* The controls held, and those pressed on this frame: NEBULA_BUTTON_*. */
    uint32_t (*buttons_held)(void);
    uint32_t (*buttons_pressed)(void);
    /* The stick, each from -1 to 1. */
    void (*stick)(float* x, float* y);
    void (*shake_camera)(void);

    /* Only in nebula_plugin_start. 0: done. */
    int32_t (*add_galaxy)(const NebulaGalaxy* galaxy);
    /* A hungry Luma: placed in a level as TicoGalaxy with its Arg7 at `slot`, it opens `galaxy` when fed `star_bits`.
       The game's own are slots 0 to 6: using one takes its place. */
    int32_t (*add_hungry_luma)(const char* galaxy, int32_t slot, int32_t star_bits);
    /* A message of the game by its id, in place of its own or new: GalaxyName_<galaxy>, ScenarioName_<galaxy><n>, a talk's id. */
    int32_t (*add_text)(const char* id, const char* utf8);
    /* Its number, for set_value. Below 0: it can't be added. */
    int32_t (*add_power_up)(const NebulaPowerUpClass* power_up);
    /* Which way is down for it, a unit length: on a planet that is no one axis. */
    void (*get_gravity)(NebulaActor* actor, float* xyz);
    /* Only in nebula_plugin_start. A music track: `file` is its place in the game, AudioRes/Stream/MyTrack.wav (.ogg, .ast).
       `name` is yours to choose, not starting SE_, BGM_, MBGM_, STM_ or ME_, which are the game's. 0: done. */
    int32_t (*add_music)(const char* name, const char* file);
    /* Only in nebula_plugin_start. What a stage plays: `scenario` is a mission, 0 for all it has no row for. */
    int32_t (*set_stage_music)(const char* stage, int32_t scenario, const char* name);
    /* Only in nebula_plugin_start. What a ChangeBgmCube in the stage starts, with its Obj_arg1 at `area`. */
    int32_t (*set_area_music)(const char* stage, int32_t area, const char* name);
    /* In a stage: this track, a mod's or the game's by its name, in place of what's playing. 1: it started. */
    int32_t (*play_music)(const char* name);
    /* In a stage: what's playing fades out over this many frames. */
    void (*stop_music)(int32_t frames);
    /* In a stage: a movie over the game, by its place in the game: MovieData/MyMovie.thp. The game waits while it plays. 1: it started. */
    int32_t (*play_movie)(const char* file);
    void (*stop_movie)(void);
    int32_t (*is_movie_playing)(void);
    /* Only in nebula_plugin_start. A row of the game's progression, by its name: what opens what, and what each hungry Luma costs.
       A name the game has is changed, a new one added. `kind`: 1 stars, 2 a galaxy's stars, 3 a special star, 4 two flags, 5 story,
       6 a galaxy, 7 a comet, 8 a hungry Luma. `a` and `b` follow the kind: the star's number, or the Luma's slot and its price. 0: done. */
    int32_t (*set_event_flag)(const char* name, int32_t kind, int32_t a, int32_t b, const char* first, const char* second);
    /* Only in nebula_plugin_start. A dome's comets, 1 to 6, in the order they come, for the whole of its schedule: `galaxies[i]` with
       `coins[i]` at 1 for the hundred purple coins. */
    int32_t (*set_comet_schedule)(int32_t dome, const char* const* galaxies, const int32_t* coins, int32_t count);
} NebulaModApi;

/* The player's controls, whatever device they're on. */
typedef enum NebulaButton {
    NEBULA_BUTTON_JUMP = 1u << 0,
    NEBULA_BUTTON_SPIN = 1u << 1,
    NEBULA_BUTTON_CROUCH = 1u << 2,
    NEBULA_BUTTON_CAMERA = 1u << 3
} NebulaButton;

#if defined(_WIN32) && defined(__cplusplus)
/* Built with no C library, a plugin using floats needs the one symbol the compiler asks of it. */
#define NEBULA_PLUGIN_FLOATS extern "C" __declspec(selectany) int _fltused = 0;
#elif defined(_WIN32)
#define NEBULA_PLUGIN_FLOATS __declspec(selectany) int _fltused = 0;
#else
#define NEBULA_PLUGIN_FLOATS
#endif

#ifdef __cplusplus
#define NEBULA_PLUGIN_C extern "C"
#else
#define NEBULA_PLUGIN_C
#endif

/* What a plugin exports: the interface it was built against, then the game's call to it once, before the game starts. */
#define NEBULA_PLUGIN_ENTRY(api, self)                                                                         \
    NEBULA_PLUGIN_FLOATS                                                                                       \
    NEBULA_PLUGIN_C NEBULA_PLUGIN_EXPORT uint32_t nebula_plugin_api(void) { return NEBULA_MOD_API_VERSION; }   \
    NEBULA_PLUGIN_C NEBULA_PLUGIN_EXPORT void nebula_plugin_start(const NebulaModApi* api, const NebulaPlugin* self)

#ifdef __cplusplus
}
#endif

#endif /* NEBULA_MOD_API_H */
