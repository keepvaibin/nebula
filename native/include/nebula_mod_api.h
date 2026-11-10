/* SPDX-License-Identifier: GPL-3.0-only */
/* Nebula's plugin interface: what a .luma's code calls and is called by. Plain C, on its own: any compiler that makes a
   native library for the device can build against it.

   A plugin exports the two functions NEBULA_PLUGIN_ENTRY makes. Fields are only ever added at the end of NebulaModApi:
   a plugin built against this header keeps working on every later Nebula. On an earlier one, a field added after it
   is not there: NEBULA_MOD_API_HAS(api, field) says whether it is, before the plugin calls it.

   A null object, layout, point or name where a call needs one makes the call do nothing: it returns 0, null, or -1
   where -1 means "no such". Where null means something (find_actor's `name`, stop_effect's), the call says so. A
   number that is NaN or infinite, where a position, motion, angle, scale, time or tuning number is set, does the same:
   the call changes nothing. */
#ifndef NEBULA_MOD_API_H
#define NEBULA_MOD_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 2: setting() and setting_by_number() gave way to get_tunable(), and object_arg(index) to placement_number(column) and
   placement_text(column), and the game's nerves came in after state_frames; a plugin built for 1 is not started. */
#define NEBULA_MOD_API_VERSION 2u

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
    /* The player is about to die. value: how (0 a fall out of the level). Return -1 to refuse it: the player lives on where
       they are, so move them somewhere safe (set_position) and give back life (set_value) as the plugin's rule says. */
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
    /* A line of talk opens in its balloon. actor: who talks. name: the line's message id, as luma.json "text" and
       add_text name it (null for a line with no id). */
    NEBULA_MOD_EVENT_TALK_LINE,
    /* The player answered a talk's yes/no question. value: 1 yes, 0 no. actor: who asked. name: the question's
       message id. */
    NEBULA_MOD_EVENT_TALK_CHOICE,
    /* A part of a cutscene starts. value: the part's number in the cutscene, from 0. name: the part, as the
       cutscene's time sheet names it. */
    NEBULA_MOD_EVENT_CUTSCENE_PART,
    /* The camera changes. value: 0 an area's camera (also when the last event camera ends and the area's takes the view
       back), 1 an event camera (a cutscene's, a talk's, start_camera's). name: its id, as the stage's camera table names it. */
    NEBULA_MOD_EVENT_CAMERA_CHANGE,
    /* A sound effect is about to start. value: its number; name: its name. What you return plays: that number, another
       sound's, or -1 for none. Subscribing costs a call per sound. */
    NEBULA_MOD_EVENT_SOUND_START,
    /* An object starts one of its effects. actor: the object. name: the effect, as the object's effect table names
       it. Subscribing costs a call per effect. */
    NEBULA_MOD_EVENT_EFFECT_START,
    /* A screen layout (a counter, a menu, a message box) is shown or goes away. value: 1 shown, 0 gone. name: its
       name in the game's code. */
    NEBULA_MOD_EVENT_SCREEN,
    /* An object gets a message from another: an attack, a stomp, a push, a grab (docs/modding/EVENTS.md lists the
       numbers). actor: who gets it. name: its name. value: the message. Return -1 to refuse it: the object acts as if
       it never came. Subscribing costs a call per message. */
    NEBULA_MOD_EVENT_ACTOR_MESSAGE,
    /* The player starts an action of their own: a swim, a hang, a slide, a fall (docs/modding/EVENTS.md lists the
       numbers). value: the action. */
    NEBULA_MOD_EVENT_PLAYER_ACTION,
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
    int32_t color_frame;  /* the player's colours: a frame of Mario.arc's ColorChange (0 his own, 1 Flying's, 2 Fire's). 0: those of
                             looks_like. A mod's Mario.arc can add more. */
    void* user;
    void (*start)(void* user);
    void (*update)(void* user); /* every frame it lasts */
    void (*end)(void* user);
    /* What follows may be left out of an older plugin's class: `size` says. */
    const char* model;    /* a whole-body model, ObjectData/<model>.arc, on the player's skeleton. Null: the game's */
    int32_t looks_like;   /* with no model: the look of one of the game's (NEBULA_POWER_UP_*) */
} NebulaPowerUpClass;

/* When hook_nerve's callback runs. */
typedef enum NebulaNervePhase {
    NEBULA_NERVE_ENTER = 0, /* frames is 0 */
    NEBULA_NERVE_FRAME,     /* before the game's frame of it: return 0 and the game's is not run (yours stands in) */
    NEBULA_NERVE_LEAVE      /* another nerve was set; the game's own end of this one has run */
} NebulaNervePhase;

/* actor: the object, or the object a state works for; null for what is no object (a screen, the scene's control).
   nerve: its name. frames: as nerve_frames. On the thread the object runs on. What's returned counts only for
   NEBULA_NERVE_FRAME; several plugins hooking one nerve all run, and any 0 stops the game's frame. */
typedef int32_t (*NebulaNerveCallback)(NebulaActor* actor, const char* nerve, NebulaNervePhase phase, int32_t frames, void* user);

typedef struct NebulaModApi {
    uint32_t size;
    uint32_t version;
    /* The game's headers this game was built from; see nebula_game_sdk.h. */
    uint64_t game_abi;

    /* One line in the game's log. */
    void (*log)(const NebulaPlugin* self, const char* text);
    /* The choice a player made for one of the mod's options, by option id: its choice id. Null: no such option. */
    const char* (*option)(const NebulaPlugin* self, const char* id);
    /* Only in nebula_plugin_start: `callback` runs at each `event` (NEBULA_MOD_EVENT_*), with what it's about. What it returns
       counts where the event says so (a hit refused, coins changed); else return data->value. 0: done. */
    int32_t (*subscribe)(NebulaModEvent event, NebulaModCallback callback, void* user);
    /* A class of objects levels can place by its name, run by the plugin's callbacks (NebulaObjectClass).
       0: done; -1: no name, a name taken, or a class older than Nebula reads. */
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
    /* Where it is: x, y, z in the level's units. */
    void (*get_position)(NebulaActor* actor, float* xyz);
    /* It is there at once. The player is put there as after a warp; an object just moves. */
    void (*set_position)(NebulaActor* actor, const float* xyz);
    /* How far it moves each frame: x, y, z. The player's is the game's own. */
    void (*get_velocity)(NebulaActor* actor, float* xyz);
    /* Its movement from now: the game's gravity and friction go on from it. */
    void (*set_velocity)(NebulaActor* actor, const float* xyz);
    /* One of the game's numbers (NEBULA_VALUE_*). The player's are 0 with no stage running. */
    int32_t (*value)(NebulaModValue what);
    /* Sets one (NEBULA_SET_*); nothing with no stage running. Life 0 or less: the player dies; more than the most:
       the most. A power-up: the game's (NEBULA_POWER_UP_*) or one add_power_up gave. */
    void (*set_value)(NebulaModSet what, int32_t value);
    /* The player dies, as from a fall out of the level. */
    void (*kill_player)(void);
    /* The player leaves the ground with this velocity, as a spring sends them. */
    void (*jump_player)(const float* velocity);
    /* It comes into the level (one killed, or one not yet shown): drawn, moving and touchable, as the game's appear. */
    void (*appear)(NebulaActor* actor);
    /* It leaves the level, as a beaten enemy: not drawn, moved or touched. appear brings it back. */
    void (*kill)(NebulaActor* actor);
    /* 1 while it's out of the level (killed, or not yet appeared). */
    int32_t (*is_dead)(NebulaActor* actor);
    /* Its model drawn (1) or not (0); it still moves and touches. Nothing for an object with no model. The player: his
       hands, face and power-up suit with it; his shadow stays. */
    void (*show_model)(NebulaActor* actor, int32_t show);
    /* 1: it started. 0: the object has no such thing, or nothing to play it with. */
    int32_t (*play)(NebulaActor* actor, NebulaModPlay what, const char* name);
    /* The name it was made with: a placed object's is its name in the level (Kuribo); some of the game's own
       objects have Japanese names. */
    const char* (*actor_name)(NebulaActor* actor);
    /* The stage being played (EggStarGalaxy); null with none. */
    const char* (*stage_name)(void);
    /* A registered object at a place. Its archive must already be loaded: a level places it, or the stage holds it.
       Null: it can't be made now. */
    NebulaActor* (*spawn)(const char* name, const float* xyz);
    /* The controls held now: NEBULA_BUTTON_*, any together. 0 with no stage running. */
    uint32_t (*buttons_held)(void);
    /* The controls pressed on this frame only: NEBULA_BUTTON_*. */
    uint32_t (*buttons_pressed)(void);
    /* The stick, each from -1 to 1 (either may be null). */
    void (*stick)(float* x, float* y);
    /* In a stage: the camera shakes, as when something heavy lands (shake_camera_by chooses how hard). */
    void (*shake_camera)(void);

    /* Only in nebula_plugin_start. 0: done; -2: the galaxy has no scenario archive (StageData/<name>/<name>Scenario.arc in
       the mod, StageData/<name>Scenario in the store), so it is left out; -1: anything else (its name clashes with one the
       game has). */
    int32_t (*add_galaxy)(const NebulaGalaxy* galaxy);
    /* A hungry Luma: placed in a level as TicoGalaxy with its Arg7 at `slot`, it opens `galaxy` when fed `star_bits`.
       The game's own are slots 0 to 6: using one takes its place. */
    int32_t (*add_hungry_luma)(const char* galaxy, int32_t slot, int32_t star_bits);
    /* A message of the game by its id, in place of its own or new: GalaxyName_<galaxy>, ScenarioName_<galaxy><n>, a talk's id.
       A line of the game keeps its talk settings. The words may hold the game's message tags in brackets (nebula_text_tags.h). */
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
    /* The movie playing ends now; the game goes on. */
    void (*stop_movie)(void);
    /* 1 while a movie plays. */
    int32_t (*is_movie_playing)(void);
    /* Only in nebula_plugin_start. A row of the game's progression, by its name: what opens what, and what each hungry Luma costs.
       A name the game has is changed, a new one added. `kind`: 1 stars, 2 a galaxy's stars, 3 a special star, 4 two flags, 5 story,
       6 a galaxy, 7 a comet, 8 a hungry Luma. `a` and `b` follow the kind: the star's number, or the Luma's slot and its price. 0: done. */
    int32_t (*set_event_flag)(const char* name, int32_t kind, int32_t a, int32_t b, const char* first, const char* second);
    /* Only in nebula_plugin_start. A dome's comets (domes 1 to 6 are the game's; 7, 8 and on add domes while they run on unbroken), in
       the order they come, for the whole of its schedule: `galaxies[i]` with `coins[i]` at 1 for the hundred purple coins. */
    int32_t (*set_comet_schedule)(int32_t dome, const char* const* galaxies, const int32_t* coins, int32_t count);

    /* One of the game's objects becomes another, or (`becomes` null) is gone. Where: NEBULA_OBJECTS_*. Before stages load. */
    int32_t (*set_object_change)(int32_t where, const char* object, const char* becomes);
    /* A comet mission's time limit in seconds, for a galaxy and mission of the player's comets. */
    int32_t (*set_comet_time_limit)(const char* galaxy, int32_t mission, int32_t seconds);

    /* An object's skeleton animation (what play's NEBULA_PLAY_ANIMATION started): 1 once it has reached its end, 0 while it
       plays on. */
    int32_t (*is_animation_done)(NebulaActor* actor);
    /* Its frame now, from 0; 0 with no model. */
    float (*animation_frame)(NebulaActor* actor);
    /* Its last frame; 0 with no model. */
    float (*animation_length)(NebulaActor* actor);
    /* It goes on from this frame. */
    void (*set_animation_frame)(NebulaActor* actor, float frame);
    /* Its speed: 1 its own, 2 twice, 0 held, below 0 backwards. */
    void (*set_animation_rate)(NebulaActor* actor, float rate);
    /* Its effect of that name stops; null: all of them. */
    void (*stop_effect)(NebulaActor* actor, const char* name);
    /* Its sound of that name stops. */
    void (*stop_sound)(NebulaActor* actor, const char* name);

    /* An object of register_object's own step in what it's doing: a number of the plugin's own (its enum of steps),
       starting at 0; state_frames counts from the set_state that began it. */
    void (*set_state)(NebulaActor* actor, int32_t state);
    /* Its state: what set_state set last, 0 before that or for an object not register_object's. */
    int32_t (*state)(NebulaActor* actor);
    /* Frames since that set_state, from 0. */
    int32_t (*state_frames)(NebulaActor* actor);

    /* The game's own objects (and screens, and the states objects hand their work to) run one nerve at a time: Kuribo
       walks in "Kuribo.Walk". A nerve's name is "<class>.<nerve>", as docs/modding/NERVES.md lists every one.
       nerve: the object's now; null for one without nerves (register_object's objects have set_state). nerve_frames:
       its frames in it, from 0. set_nerve: it goes to another nerve of the class its nerve now is of, from its next frame
       (the game's code of that nerve runs, as when the game sets it). 0: done; -1: no such nerve of its class. */
    const char* (*nerve)(NebulaActor* actor);
    /* Its frames in the nerve, from 0. */
    int32_t (*nerve_frames)(NebulaActor* actor);
    /* It goes to another nerve of its class (as nerve names it) from its next frame. 0: done; -1: no such nerve. */
    int32_t (*set_nerve)(NebulaActor* actor, const char* nerve);
    /* Only in nebula_plugin_start. `callback` runs for every object in the nerve: as it enters, every frame of it, and as
       it leaves. 0: done; -1: no such nerve. */
    int32_t (*hook_nerve)(const char* nerve, NebulaNerveCallback callback, void* user);
    /* The index-th nerve's name, for listing them all. Null: past the last. */
    const char* (*nerve_at)(int32_t index);

    /* Its rotation about x, y and z, in degrees, as levels place it. */
    void (*get_rotation)(NebulaActor* actor, float* xyz);
    /* Its rotation, in degrees. A register_object object with a front (set_front) turns by that instead. */
    void (*set_rotation)(NebulaActor* actor, const float* xyz);
    /* Its size on each axis: 1 its own. */
    void (*get_scale)(NebulaActor* actor, float* xyz);
    /* Its size on each axis. */
    void (*set_scale)(NebulaActor* actor, const float* xyz);
    /* register_object's objects: the way it faces, standing against gravity. Its model then turns with this, not with its rotation. */
    void (*set_front)(NebulaActor* actor, const float* xyz);
    /* The way it faces (set_front's, or as it turned to the player); 0, 0, 1 for others. */
    void (*get_front)(NebulaActor* actor, float* xyz);
    /* It turns to face the player, at most `degrees` this frame. */
    void (*turn_to_player)(NebulaActor* actor, float degrees);

    /* register_object's objects: whether anything can touch it, and hurting the player. */
    void (*set_touchable)(NebulaActor* actor, int32_t touchable);
    /* It hurts the player as the game's enemies do (NEBULA_HIT_*), through its body. 1 if the player took it. */
    int32_t (*hit_player)(NebulaActor* actor, NebulaHit how);

    /* register_object's objects: a column of its row in the level, by the column's name, as a level editor shows it:
       Obj_arg0 to Obj_arg7 (-1 where the level sets none), CommonPath_ID, ShapeModelNo, CameraSetId, MessageId, GroupId,
       ClippingGroupId, DemoGroupId, ViewGroupId, CastId, ParentID, the SW_ columns, l_id... placement_number: 1 and the
       number for a number column (whole or not); 0 for a column the row lacks or one of words, or an object the level
       didn't place (spawn). placement_text: a column of words (name), or null. */
    int32_t (*placement_number)(NebulaActor* actor, const char* column, double* value);
    /* A column of words of its row (its name in the level, a camera's name), or null. */
    const char* (*placement_text)(NebulaActor* actor, const char* column);
    /* Its switches (NEBULA_SWITCH_*): 1 when the level set that one. The other switch calls do nothing for one it didn't. */
    int32_t (*has_switch)(NebulaActor* actor, NebulaSwitch which);
    /* 1 while that switch is on. */
    int32_t (*is_switch_on)(NebulaActor* actor, NebulaSwitch which);
    /* It turns that switch on or off, for all the level's objects that listen to it. */
    void (*set_switch)(NebulaActor* actor, NebulaSwitch which, int32_t on);

    /* Its rail from the level (CommonPath_ID): 1 when it has one. The rail calls do nothing, or answer 0, for an object without. */
    int32_t (*has_rail)(NebulaActor* actor);
    /* It goes `speed` along its rail (below 0: back) and is put there. */
    void (*move_on_rail)(NebulaActor* actor, float speed);
    /* 1 once it has come to the end it moves to. */
    int32_t (*rail_at_end)(NebulaActor* actor);
    /* It moves the other way along it from now. */
    void (*rail_turn_back)(NebulaActor* actor);
    /* How far along it is, in the level's units from the rail's start. */
    float (*rail_coord)(NebulaActor* actor);
    /* It is put this far along (the next move_on_rail moves it from there). */
    void (*set_rail_coord)(NebulaActor* actor, float coord);
    /* The rail's whole length. */
    float (*rail_length)(NebulaActor* actor);

    /* The world. 1 and where (`hit`, which may be null) if the line from `from` to `to` meets the map's collision; 0 if
       it's clear. */
    int32_t (*raycast)(const float* from, const float* to, float* hit);
    /* 1 while it stands on the map: the player, or an object with collision of its own. */
    int32_t (*is_on_ground)(NebulaActor* actor);
    /* 1 while it touches a wall of the map (objects with collision of their own). */
    int32_t (*is_touching_wall)(NebulaActor* actor);
    /* 1 if a point is in the stage's water. */
    int32_t (*is_in_water)(const float* xyz);
    /* How far it is from the player; 0 with no player. */
    float (*distance_to_player)(NebulaActor* actor);
    /* Where one of its model's joints is now; its position with no such joint or model. */
    void (*get_joint_position)(NebulaActor* actor, const char* joint, float* xyz);

    /* The way the player faces, a unit length; 0, 0, 1 with no player. */
    void (*player_front)(float* xyz);
    /* The player's controls: 0 takes them (the player stands still, as in a cutscene), 1 gives them back. */
    void (*set_player_control)(int32_t on);

    /* Items an object gives: declared in its class's `start` (the game readies them with the stage), then appear_item at the
       moment one comes out. */
    void (*declare_item)(NebulaActor* actor, NebulaItem what, int32_t count);
    /* One comes out at a point: coins and star bits pop out `count` at a time; a power star, the star of number
       `count` (below 0: the stage's). */
    void (*appear_item)(NebulaActor* actor, NebulaItem what, const float* xyz, int32_t count);

    /* A cutscene of the stage's (its DemoGroupId's, by name): the actor's part in it starts. 1 when it began; 0 while another
       plays (ask again next frame). */
    int32_t (*start_cutscene)(NebulaActor* actor, const char* name);
    /* The actor's part in that cutscene ends; the cutscene ends when every part has. */
    void (*end_cutscene)(NebulaActor* actor, const char* name);
    /* 1 while any cutscene plays, while the stage starts (its opening camera and the player's arrival) and while a star's
       get sequence plays ("You got a Star!"), which send no NEBULA_MOD_EVENT_CUTSCENE: the player has control when it is
       0. A message is best shown then. */
    int32_t (*is_cutscene_playing)(void);
    /* In a stage: the camera shakes, as hard as `how` (NEBULA_SHAKE_*). */
    void (*shake_camera_by)(NebulaShake how);
    /* In a stage: one of its event cameras (by name, as the stage's camera table has it) takes the view over
       `frames` frames. */
    void (*start_camera)(const char* name, int32_t frames);
    /* That camera hands the view back over `frames` frames. */
    void (*end_camera)(const char* name, int32_t frames);
    /* In a stage: the screen fades to black over `frames` frames. */
    void (*fade_out)(int32_t frames);
    /* In a stage: the screen fades back in over `frames` frames. */
    void (*fade_in)(int32_t frames);

    /* The player goes to a galaxy's mission, as a launch star sends them (the stage ends, the new one loads). */
    void (*change_stage)(const char* galaxy, int32_t mission);
    /* In a stage: the player goes back to the dome, as when a galaxy is left. */
    void (*back_to_dome)(void);
    /* 1 if the save has that event flag (a name of the game's progression, or one set_event_flag added) on.
       "SpinAndStarPointer" is the spin and the Star Pointer the Gateway's Luma grants. */
    int32_t (*is_event_flag_on)(const char* name);
    /* Turns it on in the save (written with the next save). "SpinAndStarPointer" grants the spin and the Star Pointer
       at once, in the stage being played too. */
    void (*set_event_flag_on)(const char* name);
    /* 1 if the save has that galaxy's star of that number. */
    int32_t (*has_star)(const char* galaxy, int32_t star);
    /* In a stage: the game's information box (at the top, or the middle with `centre` 1) shows a message
       by its id (the game's or add_text's), else these words (with the game's message tags; lines wrap between
       words). It stays until hide_message. Asked for while the stage starts, it appears when the player has control. */
    void (*show_message)(const char* utf8_or_id, int32_t centre);
    /* The information box goes. */
    void (*hide_message)(void);

    /* A number kept for this mod across plays (in the mods folder's .plugin-data, written at once). Keys: no '=' or line
       ends. `missing`: what a key never set answers. */
    int32_t (*store_get)(const NebulaPlugin* self, const char* key, int32_t missing);
    /* Keeps `value` under `key`. */
    void (*store_set)(const NebulaPlugin* self, const char* key, int32_t value);

    /* Only in nebula_plugin_start. A stage plays by another's rules: the game's rules that go by a stage's name (a Bowser arena's
       star, a hub's counters, an ocean's swimming) then count it as `acts_as`. own_rules 0: it no longer counts as itself. `scenario`:
       one mission, 0 for all. 0: done. */
    int32_t (*set_stage_rules)(const char* stage, int32_t scenario, const char* acts_as, int32_t own_rules);
    /* A number of the player's table by its name (mJumpHeight, mFireModeTime). `character`: 0 Mario, 1 Luigi. `index`: its place in a list, else 0.
       Any time. 0: done; -1: no such number. */
    int32_t (*set_player_constant)(int32_t character, const char* name, int32_t index, float value);
    /* The number now; 0 with no such number. */
    float (*player_constant)(int32_t character, const char* name, int32_t index);
    /* Only in nebula_plugin_start. A new name levels can place, made as the game's object `like`, with its files from ObjectData/<name>.arc.
       No code needed. 0: done. */
    int32_t (*set_object_class)(const char* name, const char* like);
    /* Only in nebula_plugin_start. A sound by its name, played from a file (AudioRes/Stream/Boom.wav, .ogg, .ast): a new name, or one of
       the game's (SE_OJ_COIN_GET) to replace it wherever it plays, by name or by number (the player's voices, SE_PV_*).
       "<name>@luigi": in its place while Luigi plays. 0: done. */
    int32_t (*add_sound_effect)(const char* name, const char* file);

    /* Finding things. An actor by the name levels place it under; `after` null starts, a found one goes on from it. `name` null: every actor. */
    NebulaActor* (*find_actor)(const char* name, NebulaActor* after);
    /* Living actors within `radius`. Their count; up to `max` go in `out`. */
    int32_t (*actors_near)(const float* xyz, float radius, NebulaActor** out, int32_t max);
    /* As the player's attack `how`, on any actor. 1 if it took it. `by`: where it comes from, null for the player. */
    int32_t (*attack_actor)(NebulaActor* target, NebulaActor* by, NebulaAttack how);
    /* A pointer of your own on one of register_object's objects: what set_actor_data put there, else null. */
    void* (*actor_data)(NebulaActor* actor);
    /* Puts a pointer of your own on it: yours to free (at its class's `kill` or the STAGE_END event). */
    void (*set_actor_data)(NebulaActor* actor, void* data);
    /* The name levels place it under. */
    const char* (*actor_placed_name)(NebulaActor* actor);
    /* Run again at `frames` frames, or not at all with 0: a plain hit-stop: the whole scene holds still. */
    void (*stop_scene)(int32_t frames);

    /* Which way is down at a point, a unit length: from the stage's gravity, as an object there would fall. */
    void (*gravity_at)(const float* xyz, float* out);
    /* 1 if a point is in one of the stage's areas of that kind (WaterArea, DeathArea, a ChangeBgmCube...). */
    int32_t (*is_in_area)(const char* area, const float* xyz);
    /* Where the camera is. */
    void (*camera_position)(float* xyz);
    /* A point the camera looks at, 1000 units before it. */
    void (*camera_target)(float* xyz);
    /* Where a point is on the screen; 0 if behind the camera. */
    int32_t (*screen_position)(const float* xyz, float* xy);
    /* A sound in no place; an effect's named sound at a point. */
    void (*play_system_sound)(const char* name);

    /* The pointer on the screen, in the screen's own coordinates. 0: off it. */
    int32_t (*pointer)(float* xy);

    /* Save data. An event value (a named number the save keeps), by its name. */
    int32_t (*event_value)(const char* name);
    /* Sets it, held between 0 and 65535 (the save keeps 16 bits). */
    void (*set_event_value)(const char* name, int32_t value);
    /* A star as if won, or taken away. */
    void (*set_star)(const char* galaxy, int32_t star, int32_t have);
    /* How many power stars a galaxy has (its scenarios with a star). */
    int32_t (*stars_in_galaxy)(const char* galaxy);
    /* How many of them the save has. */
    int32_t (*stars_won_in_galaxy)(const char* galaxy);
    /* A tuning number by its key, as set_tunable takes it ("GameRules.cPlayerHealth"): the number now and the game's. 0: done; -1: no such
       number. Interface 2: it took the place of setting and setting_by_number. */
    int32_t (*get_tunable)(const char* key, double* value, double* game_value);

    /* A layout on the screen (LayoutData/<name>.arc, the game's or a mod's), for this stage: it goes with the stage. Null
       if there is no such archive. */
    NebulaLayout* (*layout_open)(const char* name);
    /* Shown (1) or hidden (0). */
    void (*layout_show)(NebulaLayout* layout, int32_t show);
    /* A pane's text: these words, with the game's message tags. */
    void (*layout_set_text)(NebulaLayout* layout, const char* pane, const char* utf8);
    /* A pane's text as a number, in the game's digits. */
    void (*layout_set_number)(NebulaLayout* layout, const char* pane, int32_t number);
    /* A pane shown (1) or hidden (0), with the panes in it. */
    void (*layout_show_pane)(NebulaLayout* layout, const char* pane, int32_t show);
    /* A pane moved to x, y on the screen, where screen_position puts a point; it stays there. */
    void (*layout_move_pane)(NebulaLayout* layout, const char* pane, float x, float y);
    /* Its animation by name. */
    void (*layout_play)(NebulaLayout* layout, const char* animation);
    /* 1 while its animation plays. */
    int32_t (*layout_is_playing)(NebulaLayout* layout);
    /* How opaque it is: 1 as made, 0 not seen. */
    void (*layout_set_alpha)(NebulaLayout* layout, float alpha);

    /* Text kept for this mod across plays, beside store_get's numbers: up to `size` - 1 bytes and an end go in `out`; how many
       were read (0 for a key never set). */
    int32_t (*store_get_text)(const NebulaPlugin* self, const char* key, char* out, int32_t size);
    /* Keeps `text` under `key`. */
    void (*store_set_text)(const NebulaPlugin* self, const char* key, const char* text);
    /* The star-choosing screen of a galaxy, as a launch star opens it. */
    void (*open_scenario_select)(const char* galaxy);
    /* Only in nebula_plugin_start. The game's journey: "first_stage" (and "first_mission", a number as text) is where a new file begins, in place of
       the prologue; "hub" is where the player goes after a galaxy, in place of the Comet Observatory; "story_scenes" "off" leaves out the game's own story scenes. 0: done. */
    int32_t (*set_story)(const char* key, const char* value);
    /* The game writes the save, as it does after a star. */
    void (*save_game)(void);
    /* A file's tuning number by "<file stem>.<name>" ("GameRules.cPlayerHealth"), or a list's place by "<file stem>.<name>[<i>]", as luma.json's
       "tunables" set them. Any time; most are read when an object starts, so the host sets luma.json's before the first stage. Whole kinds
       round and clamp. 0: done; -1: no such number. */
    int32_t (*set_tunable)(const char* key, double value);
    /* The index-th tuning number (a list counts each place): its file ("Kuribo.cpp"), name, place in its list (-1: one number), kind (1 f32,
       2 f64, 3 s32, 4 u32, 5 s16, 6 u16, 7 s8, 8 u8, 9 bool), the game's number and the number now. 0: done; -1: past the last. */
    int32_t (*tunable_at)(int32_t index, const char** file, const char** name, int32_t* place, int32_t* kind, double* game_value, double* value);

    /* The game's rules as a plugin changes them, any time. Each holds until set again; a stage change keeps it.
       How hard gravity pulls: the player's fall and every object's. 1 the game's, 0.5 half, 0 none; below 0 pulls away. */
    void (*set_gravity_scale)(float scale);
    /* The scale now. */
    float (*gravity_scale)(void);
    /* The countdown on the screen (a comet's time limit). 1 while one runs, with its seconds left and its whole length; 0
       when none does. */
    int32_t (*timer)(float* seconds_left, float* seconds_limit);
    /* Its seconds left, within its length. 0: done; -1: no countdown runs. */
    int32_t (*set_timer)(float seconds_left);
    /* 1 holds every countdown where it is, 0 lets them run. */
    void (*pause_timer)(int32_t paused);
    /* The camera at `position` looking at `target`, with `fov` degrees from the picture's top to its bottom. A null point or
       a fov of 0 leaves that to the game; all three left: the game's camera again. A position set is where the camera is
       that frame, with no easing, and it passes through walls. */
    void (*set_camera)(const float* position, const float* target, float fov);
    /* The angle of view the camera drew with last, in degrees. */
    float (*camera_fov)(void);
    /* HUD parts (NEBULA_HUD_*) hidden (0) or shown as the game shows them (1). */
    void (*show_hud)(uint32_t parts, int32_t show);
    /* The scene's speed: 1 the game's, 0.5 one step every other frame, 0 held (menus and the HUD still run). Between 0
       and 1. Set in a STAGE_FRAME callback, it counts from that frame. */
    void (*set_game_speed)(float speed);
    /* While held: the scene runs this many steps, one a frame. */
    void (*step_game)(int32_t steps);
    /* A line of text over the game, in the game's message font. `line`: its slot, 0-15. x, y: its top-left corner, 0 to 1
       across and down the screen. `size`: its height, of the screen's height (0.04 reads like the HUD's numbers).
       `rgba`: its colour, 0xRRGGBBAA. Null or "" takes the line away; it stays until then, across stages. 0: done; -1: no
       such slot. */
    int32_t (*draw_text)(int32_t line, float x, float y, float size, uint32_t rgba, const char* utf8);
    /* As add_text, and a new line takes the talk (camera, sound, talk and balloon type) of the game's message `like_id`;
       null or "": add_text. The words may hold the game's message tags in brackets ([wait 30], [color 1], [player]...:
       nebula_text_tags.h), as add_text's may. */
    int32_t (*add_text_like)(const char* id, const char* utf8, const char* like_id);
    /* A player animation: `name` plays the bck `file` of the player's animations (ObjectData/MarioAnime.arc: the game's
       or one the mod adds there, a .bck or an animation in a .glb), at `rate` (1: the bck's own speed), blended in over
       `blend_frames` frames (0: the game's 6). Played by name (NEBULA_PLAY_PLAYER_ANIMATION), and where the game plays
       that name: a name the game has (its own animation table's) is played from the mod's bck. Called from the plugin's
       entry, before the first stage: 0 done; -1 a bad name or file, or too late. */
    int32_t (*add_player_animation)(const char* name, const char* file, float rate, int32_t blend_frames);
    /* Memory of the plugin's own, from the game's C library: a plugin built with none (as one built for every device
       is) has no malloc. alloc: null for 0 bytes or when none is left; the memory is aligned for any type.
       release(null) does nothing. Kept until released; a plugin's memory is never moved or reused under it. */
    void* (*alloc)(uint64_t bytes);
    void (*release)(void* memory);
    /* printf's formatting into `buffer` of `size` bytes, always ended with a 0 when size is above 0: what snprintf
       returns (the length the whole text needs; below 0 for a bad format). */
    int32_t (*format)(char* buffer, uint64_t size, const char* format, ...);
    /* The camera's near plane in units: nothing closer to the camera is drawn. 0: the game's (100; 10 in its own
       first-person view). Kept until set again. */
    void (*set_camera_near)(float distance);
    /* One of the mod's achievements (its luma.json "achievements", by id) is unlocked: 1 now, 0 when it was already or
       the mod has none of that id. Its pop-up shows; the board keeps it under Mods. */
    int32_t (*unlock_achievement)(const NebulaPlugin* self, const char* id);
} NebulaModApi;

/* Whether this Nebula's interface has `field` (one added at the end since the plugin's oldest Nebula):
   if (NEBULA_MOD_API_HAS(api, unlock_achievement)) api->unlock_achievement(self, "coins"); */
#define NEBULA_MOD_API_HAS(api, field) ((api)->size >= offsetof(NebulaModApi, field) + sizeof((api)->field) && (api)->field != 0)

/* The HUD's parts, for show_hud: any together. */
typedef enum NebulaHudPart {
    NEBULA_HUD_COINS = 1u << 0,
    NEBULA_HUD_STAR_BITS = 1u << 1,
    NEBULA_HUD_LIVES = 1u << 2,
    NEBULA_HUD_STARS = 1u << 3,
    NEBULA_HUD_LIFE_METER = 1u << 4,
    NEBULA_HUD_POINTER = 1u << 5,
    NEBULA_HUD_TIMER = 1u << 6,
    NEBULA_HUD_ALL = 0x7Fu
} NebulaHudPart;

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
