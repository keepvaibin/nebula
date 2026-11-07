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
    /* The player's spin starts. */
    NEBULA_MOD_EVENT_PLAYER_SPIN,
    /* The player leaves the ground in a jump; lands. */
    NEBULA_MOD_EVENT_PLAYER_JUMP,
    NEBULA_MOD_EVENT_PLAYER_LAND,
    /* The pause menu opens or closes. value: 1 open, 0 closed. */
    NEBULA_MOD_EVENT_PAUSE,
    /* The stage is ending: every actor and layout of it goes after this. name: the stage. */
    NEBULA_MOD_EVENT_STAGE_END,
    /* The game is about to write the save. */
    NEBULA_MOD_EVENT_GAME_SAVE,
    /* A power star comes into the stage. value: its number. */
    NEBULA_MOD_EVENT_POWER_STAR_APPEAR,
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
    NEBULA_VALUE_MISSION,
    NEBULA_VALUE_POWER_STARS,
    NEBULA_VALUE_WATER_LIFE,
    /* 0 or 1, what the player is doing. */
    NEBULA_VALUE_SPINNING,
    NEBULA_VALUE_JUMP_RISING,
    NEBULA_VALUE_DAMAGED,
    NEBULA_VALUE_DEAD,
    NEBULA_VALUE_GROUND_POUNDING,
    NEBULA_VALUE_SKATING,
    NEBULA_VALUE_CROUCHING,
    NEBULA_VALUE_FLYING,
    NEBULA_VALUE_SWIMMING,
    NEBULA_VALUE_LUIGI
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
    NEBULA_PLAY_PLAYER_ANIMATION, /* on the player, by the animation's name */
    /* One part of a model's animation by its file's name, leaving the others playing. */
    NEBULA_PLAY_SKELETON,        /* .bck */
    NEBULA_PLAY_TEXTURE_MOVE,    /* .btk */
    NEBULA_PLAY_COLOUR,          /* .brk */
    NEBULA_PLAY_SHOWN_PARTS,     /* .bva */
    NEBULA_PLAY_TEXTURE_FRAMES,  /* .btp */
    NEBULA_PLAY_MATERIAL_COLOUR  /* .bpk */
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
    /* The same, saying how: NEBULA_ATTACK_*. If set, it's called in place of `attacked`. */
    int32_t (*attacked_by)(NebulaActor* actor, int32_t how, void* user);
    /* What follows may be left out of an older plugin's class: `size` says. */
    int32_t kind;         /* NEBULA_OBJECT_*: how it touches things and where it's drawn */
    int32_t collision;    /* 1: its archive's collision model is solid, and moves with it */
    float shadow_radius;  /* a round shadow under it; 0: none */
    float clip_radius;    /* not run or drawn when off screen past this; 0: always */
    void (*appeared)(NebulaActor* actor, void* user);
    void (*killed)(NebulaActor* actor, void* user);
} NebulaObjectClass;

/* What kind of thing an object of a plugin's is. */
typedef enum NebulaObjectKind {
    NEBULA_OBJECT_ENEMY = 0,
    NEBULA_OBJECT_MAP,      /* a platform, a wall: the player stands on it */
    NEBULA_OBJECT_NPC,
    NEBULA_OBJECT_RIDE,
    NEBULA_OBJECT_DECORATION  /* nothing touches it */
} NebulaObjectKind;

/* A screen layout: LayoutData/<name>.arc. */
typedef struct NebulaLayout NebulaLayout;

/* How the player attacked. */
typedef enum NebulaAttack {
    NEBULA_ATTACK_OTHER = 0,
    NEBULA_ATTACK_SPIN,
    NEBULA_ATTACK_STOMP,
    NEBULA_ATTACK_GROUND_POUND,
    NEBULA_ATTACK_STAR_BIT,
    NEBULA_ATTACK_FIRE,
    NEBULA_ATTACK_INVINCIBLE,
    NEBULA_ATTACK_SHELL
} NebulaAttack;

/* How an object hurts the player. */
typedef enum NebulaHit {
    NEBULA_HIT_NORMAL = 0,
    NEBULA_HIT_STRONG,
    NEBULA_HIT_FIRE,
    NEBULA_HIT_ELECTRIC,
    NEBULA_HIT_FREEZE,
    NEBULA_HIT_FLIP
} NebulaHit;

/* A placed object's stage switches, set in the level's data. */
typedef enum NebulaSwitch {
    NEBULA_SWITCH_A = 0,
    NEBULA_SWITCH_B,
    NEBULA_SWITCH_APPEAR,
    NEBULA_SWITCH_DEAD
} NebulaSwitch;

/* What an object promises to give, so the level counts it. */
typedef enum NebulaItem {
    NEBULA_ITEM_COINS = 0,
    NEBULA_ITEM_STAR_BITS,
    NEBULA_ITEM_POWER_STAR /* count: the star's number in the galaxy; -1: the next */
} NebulaItem;

/* The ground under the camera shake's size. */
typedef enum NebulaShake {
    NEBULA_SHAKE_WEAK = 0,
    NEBULA_SHAKE_NORMAL,
    NEBULA_SHAKE_NORMAL_STRONG,
    NEBULA_SHAKE_STRONG,
    NEBULA_SHAKE_VERY_STRONG
} NebulaShake;

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
    /* What follows may be left out of an older plugin's class: `size` says. */
    const char* model;    /* a whole-body model, ObjectData/<model>.arc, on the player's skeleton. Null: the game's */
    int32_t looks_like;   /* with no model: the look of one of the game's (NEBULA_POWER_UP_*) */
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
       Windows x64, and Linux and Mac on x64 and arm64. Elsewhere, or for a function whose first instructions can't be moved, it answers -1. */
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

    /* Only in nebula_plugin_start. 0: done; -2: the galaxy has no StageData/<name>/<name>Scenario.arc, so it is
       left out; -1: anything else (its name clashes with one the game has). */
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

    /* One of the game's objects becomes another, or (`becomes` null) is gone. Where: NEBULA_OBJECTS_*. Before stages load. */
    int32_t (*set_object_change)(int32_t where, const char* object, const char* becomes);
    /* A comet mission's time limit in seconds, for a galaxy and mission of the player's comets. */
    int32_t (*set_comet_time_limit)(const char* galaxy, int32_t mission, int32_t seconds);

    /* An object's animation: 1 while the last one started hasn't reached its end; its frame, its length, setting them. */
    int32_t (*is_animation_done)(NebulaActor* actor);
    float (*animation_frame)(NebulaActor* actor);
    float (*animation_length)(NebulaActor* actor);
    void (*set_animation_frame)(NebulaActor* actor, float frame);
    void (*set_animation_rate)(NebulaActor* actor, float rate);
    void (*stop_effect)(NebulaActor* actor, const char* name); /* null: all of them */
    void (*stop_sound)(NebulaActor* actor, const char* name);

    /* An object of register_object's own step in what it's doing. Starts at 0; state_frames counts from the set_state that began it. */
    void (*set_state)(NebulaActor* actor, int32_t state);
    int32_t (*state)(NebulaActor* actor);
    int32_t (*state_frames)(NebulaActor* actor);

    void (*get_rotation)(NebulaActor* actor, float* xyz);
    void (*set_rotation)(NebulaActor* actor, const float* xyz);
    void (*get_scale)(NebulaActor* actor, float* xyz);
    void (*set_scale)(NebulaActor* actor, const float* xyz);
    /* register_object's objects: the way it faces, standing against gravity. Its model then turns with this, not with its rotation. */
    void (*set_front)(NebulaActor* actor, const float* xyz);
    void (*get_front)(NebulaActor* actor, float* xyz);
    void (*turn_to_player)(NebulaActor* actor, float degrees); /* at most this far this frame */

    /* register_object's objects: whether anything can touch it, and hurting the player. */
    void (*set_touchable)(NebulaActor* actor, int32_t touchable);
    int32_t (*hit_player)(NebulaActor* actor, NebulaHit how);

    /* A placed object's numbers from the level (Obj_arg0 to 7; -1: none). */
    int32_t (*object_arg)(NebulaActor* actor, int32_t index);
    /* Its switches. is_valid: the level set one. */
    int32_t (*has_switch)(NebulaActor* actor, NebulaSwitch which);
    int32_t (*is_switch_on)(NebulaActor* actor, NebulaSwitch which);
    void (*set_switch)(NebulaActor* actor, NebulaSwitch which, int32_t on);

    /* Its rail from the level. move_on_rail goes `speed` along and puts the object there. */
    int32_t (*has_rail)(NebulaActor* actor);
    void (*move_on_rail)(NebulaActor* actor, float speed);
    int32_t (*rail_at_end)(NebulaActor* actor);
    void (*rail_turn_back)(NebulaActor* actor);
    float (*rail_coord)(NebulaActor* actor);
    void (*set_rail_coord)(NebulaActor* actor, float coord);
    float (*rail_length)(NebulaActor* actor);

    /* The world. raycast: 1 and where if the line from `from` to `to` meets the map. */
    int32_t (*raycast)(const float* from, const float* to, float* hit);
    int32_t (*is_on_ground)(NebulaActor* actor);
    int32_t (*is_touching_wall)(NebulaActor* actor);
    int32_t (*is_in_water)(const float* xyz);
    float (*distance_to_player)(NebulaActor* actor);
    void (*get_joint_position)(NebulaActor* actor, const char* joint, float* xyz);

    void (*player_front)(float* xyz);
    void (*set_player_control)(int32_t on);

    /* Items. declare_item in `start`; then appear_item at the moment it comes out. */
    void (*declare_item)(NebulaActor* actor, NebulaItem what, int32_t count);
    void (*appear_item)(NebulaActor* actor, NebulaItem what, const float* xyz, int32_t count);

    /* Cutscenes and cameras. start_cutscene: 1 when it began (another may be playing: ask again next frame). */
    int32_t (*start_cutscene)(NebulaActor* actor, const char* name);
    void (*end_cutscene)(NebulaActor* actor, const char* name);
    int32_t (*is_cutscene_playing)(void);
    void (*shake_camera_by)(NebulaShake how);
    void (*start_camera)(const char* name, int32_t frames);
    void (*end_camera)(const char* name, int32_t frames);
    void (*fade_out)(int32_t frames);
    void (*fade_in)(int32_t frames);

    /* The game's progress and text. */
    void (*change_stage)(const char* galaxy, int32_t mission);
    void (*back_to_dome)(void);
    int32_t (*is_event_flag_on)(const char* name);
    void (*set_event_flag_on)(const char* name);
    int32_t (*has_star)(const char* galaxy, int32_t star);
    void (*show_message)(const char* utf8_or_id, int32_t centre);
    void (*hide_message)(void);

    /* A number kept for this mod across plays. */
    int32_t (*store_get)(const NebulaPlugin* self, const char* key, int32_t missing);
    void (*store_set)(const NebulaPlugin* self, const char* key, int32_t value);

    /* Only in nebula_plugin_start. A stage plays by another's rules: the game's rules that go by a stage's name (a Bowser arena's
       star, a hub's counters, an ocean's swimming) then count it as `acts_as`. own_rules 0: it no longer counts as itself. `scenario`:
       one mission, 0 for all. 0: done. */
    int32_t (*set_stage_rules)(const char* stage, int32_t scenario, const char* acts_as, int32_t own_rules);
    /* A number of the player's table by its name (mJumpHeight, mFireModeTime). `character`: 0 Mario, 1 Luigi. `index`: its place in a list, else 0.
       Any time. 0: done; -1: no such number. */
    int32_t (*set_player_constant)(int32_t character, const char* name, int32_t index, float value);
    float (*player_constant)(int32_t character, const char* name, int32_t index);
    /* Only in nebula_plugin_start. A new name levels can place, made as the game's object `like`, with its files from ObjectData/<name>.arc.
       No code needed. 0: done. */
    int32_t (*set_object_class)(const char* name, const char* like);
    /* Only in nebula_plugin_start. A sound by its name, played from a file (AudioRes/Stream/Boom.wav, .ogg, .ast): a new name, or one of
       the game's (SE_OJ_COIN_GET) to replace it wherever it plays. 0: done. */
    int32_t (*add_sound_effect)(const char* name, const char* file);

    /* Finding things. An actor by the name levels place it under; `after` null starts, a found one goes on from it. `name` null: every actor. */
    NebulaActor* (*find_actor)(const char* name, NebulaActor* after);
    /* Living actors within `radius`. Their count; up to `max` go in `out`. */
    int32_t (*actors_near)(const float* xyz, float radius, NebulaActor** out, int32_t max);
    /* As the player's attack `how`, on any actor. 1 if it took it. `by`: where it comes from, null for the player. */
    int32_t (*attack_actor)(NebulaActor* target, NebulaActor* by, NebulaAttack how);
    /* A pointer of your own on any of register_object's objects. */
    void* (*actor_data)(NebulaActor* actor);
    void (*set_actor_data)(NebulaActor* actor, void* data);
    /* The name levels place it under. */
    const char* (*actor_placed_name)(NebulaActor* actor);
    /* Run again at `frames` frames, or not at all with 0: a plain hit-stop: the whole scene holds still. */
    void (*stop_scene)(int32_t frames);

    /* The world. */
    void (*gravity_at)(const float* xyz, float* out);
    int32_t (*is_in_area)(const char* area, const float* xyz);
    void (*camera_position)(float* xyz);
    void (*camera_target)(float* xyz);
    /* Where a point is on the screen; 0 if behind the camera. */
    int32_t (*screen_position)(const float* xyz, float* xy);
    /* A sound in no place; an effect's named sound at a point. */
    void (*play_system_sound)(const char* name);

    /* The pointer on the screen, in the screen's own coordinates. 0: off it. */
    int32_t (*pointer)(float* xy);

    /* Save data. An event value (a named number the game keeps), by its name. */
    int32_t (*event_value)(const char* name);
    void (*set_event_value)(const char* name, int32_t value);
    /* A star as if won, or taken away. */
    void (*set_star)(const char* galaxy, int32_t star, int32_t have);
    int32_t (*stars_in_galaxy)(const char* galaxy);
    int32_t (*stars_won_in_galaxy)(const char* galaxy);
    /* A game setting by its name (the names of a layer's `switches`). `missing`: no mod set it. */
    int32_t (*setting)(const char* name, int32_t missing);

    /* A layout on the screen, for this stage. Null if it has no such archive. */
    NebulaLayout* (*layout_open)(const char* name);
    void (*layout_show)(NebulaLayout* layout, int32_t show);
    void (*layout_set_text)(NebulaLayout* layout, const char* pane, const char* utf8);
    void (*layout_set_number)(NebulaLayout* layout, const char* pane, int32_t number);
    void (*layout_show_pane)(NebulaLayout* layout, const char* pane, int32_t show);
    void (*layout_move_pane)(NebulaLayout* layout, const char* pane, float x, float y);
    /* Its animation by name. */
    void (*layout_play)(NebulaLayout* layout, const char* animation);
    int32_t (*layout_is_playing)(NebulaLayout* layout);
    void (*layout_set_alpha)(NebulaLayout* layout, float alpha);

    /* Text kept for this mod across plays, in place of store_get's numbers: how much was read. */
    int32_t (*store_get_text)(const NebulaPlugin* self, const char* key, char* out, int32_t size);
    void (*store_set_text)(const NebulaPlugin* self, const char* key, const char* text);
    /* The game's answer behind setting(), by the setting's number. Plugins use setting(). */
    int32_t (*setting_by_number)(uint32_t number, int32_t missing);
    /* The star-choosing screen of a galaxy, as a launch star opens it. */
    void (*open_scenario_select)(const char* galaxy);
    /* Only in nebula_plugin_start. The game's journey: "first_stage" (and "first_mission", a number as text) is where a new file begins, in place of
       the prologue; "hub" is where the player goes after a galaxy, in place of the Comet Observatory; "story_scenes" "off" leaves out the game's own story scenes. 0: done. */
    int32_t (*set_story)(const char* key, const char* value);
    /* The game writes the save, as it does after a star. */
    void (*save_game)(void);
} NebulaModApi;

/* Where an object change applies. */
typedef enum NebulaObjectsIn {
    NEBULA_OBJECTS_PLACED = 0,       /* whatever the game creates by name: a level's objects, areas */
    NEBULA_OBJECTS_DISPLAY_MODEL = 1, /* the still models things show before they're collected */
    NEBULA_OBJECTS_TREASURE_BOX = 2, /* what a treasure box holds */
    NEBULA_OBJECTS_IN_COUNT
} NebulaObjectsIn;

/* The player's controls, whatever device they're on. */
typedef enum NebulaButton {
    NEBULA_BUTTON_JUMP = 1u << 0,
    NEBULA_BUTTON_SPIN = 1u << 1, /* the player's spin: held while it lasts, pressed on the frame it starts */
    NEBULA_BUTTON_CROUCH = 1u << 2,
    NEBULA_BUTTON_CAMERA = 1u << 3,
    NEBULA_BUTTON_FIRE = 1u << 4, /* the star-bit shot */
    NEBULA_BUTTON_DPAD_UP = 1u << 5,
    NEBULA_BUTTON_DPAD_DOWN = 1u << 6,
    NEBULA_BUTTON_DPAD_LEFT = 1u << 7,
    NEBULA_BUTTON_DPAD_RIGHT = 1u << 8,
    NEBULA_BUTTON_PAUSE = 1u << 9,
    NEBULA_BUTTON_MAP = 1u << 10
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
