/* SPDX-License-Identifier: GPL-3.0-only */
/* Nebula's plugin interface: the functions a .luma's code can call, and the events it can receive. Plain C with no
   other dependencies, so any compiler that builds native libraries for the platform can use it.

   A plugin exports the two functions NEBULA_PLUGIN_ENTRY declares. New fields are only ever added at the end of
   NebulaModApi, so a plugin keeps working on later versions of Nebula. On an older Nebula, newer fields are missing:
   check with NEBULA_MOD_API_HAS(api, field) before calling one.

   Passing null where a call needs an object, layout, point or name makes the call do nothing and return 0, null, or
   -1 (where -1 means "not found"). Calls where null has a meaning (find_actor's `name`, stop_effect's) say so. Passing
   NaN or infinity as a position, velocity, angle, scale, time or tunable value also does nothing. */
#ifndef NEBULA_MOD_API_H
#define NEBULA_MOD_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version 2 replaced setting() and setting_by_number() with get_tunable(), replaced object_arg(index) with
   placement_number(column) and placement_text(column), and added the nerve calls after state_frames. Plugins built
   for version 1 are not started. */
#define NEBULA_MOD_API_VERSION 3u

#if defined(_WIN32)
#define NEBULA_PLUGIN_EXPORT __declspec(dllexport)
#else
#define NEBULA_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/* An object in the game: an enemy, a coin, the player (a LiveActor in the game's code). Valid until the stage ends;
   don't keep one across NEBULA_MOD_EVENT_STAGE_START. */
typedef struct NebulaActor NebulaActor;

/* Identifies the plugin, for option() and the store calls. */
typedef struct NebulaPlugin {
    uint32_t size;
    const char* id;       /* the mod's id */
    const char* version;  /* the mod's version */
} NebulaPlugin;

/* Callbacks run on the thread where the event happens: objects created while a stage loads are reported on the
   loading thread, everything else on the game thread. */
typedef enum NebulaModEvent {
    /* Every frame, in every scene including menus. player() is null here. */
    NEBULA_MOD_EVENT_FRAME = 0,
    /* Every frame of a stage, before objects move. */
    NEBULA_MOD_EVENT_STAGE_FRAME,
    /* A stage started. name: the stage. value: the mission. */
    NEBULA_MOD_EVENT_STAGE_START,
    /* The player is about to lose one health. Return 0 to cancel the hit. */
    NEBULA_MOD_EVENT_PLAYER_DAMAGE,
    /* The player is about to die. value: the cause (0 is falling out of the level). Return -1 to cancel: the player
       stays where they are, so move them somewhere safe (set_position) and restore health (set_value) yourself. */
    NEBULA_MOD_EVENT_PLAYER_MISS,
    /* The player's power-up is about to change. value: the new one (NEBULA_POWER_UP_*). The return value is used. */
    NEBULA_MOD_EVENT_POWER_UP,
    /* Coins are about to be added. value: how many. The return value is added instead. */
    NEBULA_MOD_EVENT_COIN,
    /* Star bits are about to be added. value: how many. The return value is added instead. */
    NEBULA_MOD_EVENT_STAR_BIT,
    /* A star was collected. name: the galaxy. value: the star number. */
    NEBULA_MOD_EVENT_STAR_GET,
    /* A cutscene started or ended. name: the cutscene. */
    NEBULA_MOD_EVENT_CUTSCENE_START,
    NEBULA_MOD_EVENT_CUTSCENE_END,
    /* An object appeared or was killed. actor: the object. name: its name. Costs one call per object. */
    NEBULA_MOD_EVENT_ACTOR_APPEAR,
    NEBULA_MOD_EVENT_ACTOR_KILL,
    /* The player started a spin. */
    NEBULA_MOD_EVENT_PLAYER_SPIN,
    /* The player jumped off the ground, or landed. */
    NEBULA_MOD_EVENT_PLAYER_JUMP,
    NEBULA_MOD_EVENT_PLAYER_LAND,
    /* The pause menu opened (value 1) or closed (value 0). */
    NEBULA_MOD_EVENT_PAUSE,
    /* The stage is ending; all its actors and layouts are destroyed after this. name: the stage. */
    NEBULA_MOD_EVENT_STAGE_END,
    /* The game is about to write the save. */
    NEBULA_MOD_EVENT_GAME_SAVE,
    /* A power star appeared in the stage. value: its number. */
    NEBULA_MOD_EVENT_POWER_STAR_APPEAR,
    /* A line of dialogue opened in its balloon. actor: the speaker. name: the message id, as used by luma.json "text"
       and add_text (null for a line without an id). */
    NEBULA_MOD_EVENT_TALK_LINE,
    /* The player answered a yes/no question. value: 1 yes, 0 no. actor: who asked. name: the question's message id. */
    NEBULA_MOD_EVENT_TALK_CHOICE,
    /* A part of a cutscene started. value: the part's index, from 0. name: the part's name in the cutscene's
       time sheet. */
    NEBULA_MOD_EVENT_CUTSCENE_PART,
    /* The camera changed. value: 0 for an area camera (also when the last event camera ends), 1 for an event camera
       (from a cutscene, a conversation or start_camera). name: its id in the stage's camera table. */
    NEBULA_MOD_EVENT_CAMERA_CHANGE,
    /* A sound effect is about to play. value: its number. name: its name. The returned number plays instead (the same,
       another sound's, or -1 for silence). Costs one call per sound. */
    NEBULA_MOD_EVENT_SOUND_START,
    /* An object started an effect. actor: the object. name: the effect's name in the object's effect table. Costs one
       call per effect. */
    NEBULA_MOD_EVENT_EFFECT_START,
    /* A screen layout (a counter, a menu, a message box) was shown (value 1) or removed (value 0). name: its name in
       the game's code. */
    NEBULA_MOD_EVENT_SCREEN,
    /* An object received a message from another: an attack, a stomp, a push, a grab (numbers in
       docs/modding/EVENTS.md). actor: the receiver. name: its name. value: the message. Return -1 to cancel it, as if
       it was never sent. Costs one call per message. */
    NEBULA_MOD_EVENT_ACTOR_MESSAGE,
    /* The player started an action: swimming, hanging, sliding, falling (numbers in docs/modding/EVENTS.md).
       value: the action. */
    NEBULA_MOD_EVENT_PLAYER_ACTION,
    NEBULA_MOD_EVENT_COUNT
} NebulaModEvent;

typedef struct NebulaModEventData {
    int32_t value;
    NebulaActor* actor;
    const char* name;
} NebulaModEventData;

/* Plugins subscribed to the same event run in list order, the top of the Mods page last; each receives the previous
   one's return value. Return data->value to change nothing. */
typedef int32_t (*NebulaModCallback)(const NebulaModEventData* data, void* user);

/* Values that can be read with value(). */
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
    /* 0 or 1: what the player is doing. */
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

/* LumaCore values: what scripts and plugins pass to the toolkit, fields and registries (docs/modding/TOOLKIT.md). */

/* A value's kind. */
typedef enum NebulaKind {
    NEBULA_KIND_NONE = 0,
    NEBULA_KIND_BOOL,
    NEBULA_KIND_INT,
    NEBULA_KIND_FLOAT,
    NEBULA_KIND_VEC3,
    NEBULA_KIND_MATRIX, /* a handle to 12 floats (3 rows of 4) */
    NEBULA_KIND_TEXT,   /* UTF-8, owned by whoever made the value; valid for the call */
    NEBULA_KIND_HANDLE, /* one of the game's things: an object, a sensor, a triangle... (NebulaValue.type) */
    NEBULA_KIND_VEC4,
    NEBULA_KIND_VEC2
} NebulaKind;

/* One value: 24 bytes on every platform. */
typedef struct NebulaValue {
    uint32_t kind; /* NebulaKind */
    uint32_t type; /* handles: NEBULA_HANDLE_* in the low 16 bits; for objects, the class id in the upper 16 */
    union {
        int64_t i;  /* bool and int */
        double f;   /* float */
        float v[4]; /* vec2, vec3, vec4 */
        const char* s;
        struct {
            void* p;
            uint32_t generation; /* the scene it was made in; a later scene reads it as none */
        } h;
    } u;
} NebulaValue;

/* What a handle names. */
typedef enum NebulaHandleType {
    NEBULA_HANDLE_OBJECT = 1, /* an actor, area, layout or other scene object */
    NEBULA_HANDLE_SENSOR,
    NEBULA_HANDLE_TRIANGLE,
    NEBULA_HANDLE_COLLISION,
    NEBULA_HANDLE_JOINT,
    NEBULA_HANDLE_PLACEMENT,
    NEBULA_HANDLE_MATRIX,
    NEBULA_HANDLE_TALK,
    NEBULA_HANDLE_CAMERA_INFO
} NebulaHandleType;

/* Values that can be changed with set_value(). */
typedef enum NebulaModSet {
    NEBULA_SET_LIFE = 0,
    NEBULA_SET_MAX_LIFE,
    NEBULA_SET_POWER_UP,
    NEBULA_SET_ADD_COINS,
    NEBULA_SET_ADD_STAR_BITS,
    NEBULA_SET_ADD_LIVES
} NebulaModSet;

/* The player's power-ups. */
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

/* What play() starts on an object, by name. */
typedef enum NebulaModPlay {
    NEBULA_PLAY_ANIMATION = 0, /* an action: its animations, effects and sounds together */
    NEBULA_PLAY_EFFECT,
    NEBULA_PLAY_SOUND,
    NEBULA_PLAY_PLAYER_ANIMATION, /* on the player, by animation name */
    /* One part of a model's animation by file name; the other parts keep playing. */
    NEBULA_PLAY_SKELETON,        /* .bck */
    NEBULA_PLAY_TEXTURE_MOVE,    /* .btk */
    NEBULA_PLAY_COLOUR,          /* .brk */
    NEBULA_PLAY_SHOWN_PARTS,     /* .bva */
    NEBULA_PLAY_TEXTURE_FRAMES,  /* .btp */
    NEBULA_PLAY_MATERIAL_COLOUR  /* .bpk */
} NebulaModPlay;

/* An object class that levels place by `name` and spawn() creates. Its behaviour is these callbacks. A plugin built
   with the game's headers can subclass LiveActor instead, through register_creator. */
typedef struct NebulaObjectClass {
    uint32_t size;
    const char* name;
    const char* archive; /* model and animations: ObjectData/<archive>.arc. Null: invisible */
    float radius;        /* collision radius for touching */
    void* user;
    void (*start)(NebulaActor* actor, void* user);
    void (*update)(NebulaActor* actor, void* user);
    void (*touch_player)(NebulaActor* actor, void* user);
    /* The player attacked it. Return 1 if the attack hit. */
    int32_t (*attacked)(NebulaActor* actor, void* user);
    /* Like `attacked`, with the kind of attack (NEBULA_ATTACK_*). Called instead of `attacked` when set. */
    int32_t (*attacked_by)(NebulaActor* actor, int32_t how, void* user);
    /* The fields below may be missing from an older plugin's class; `size` tells. */
    int32_t kind;         /* NEBULA_OBJECT_*: what it collides with and how it is drawn */
    int32_t collision;    /* 1: the archive's collision model is solid and moves with the object */
    float shadow_radius;  /* round shadow underneath; 0: none */
    float clip_radius;    /* not updated or drawn when this far off screen; 0: always */
    void (*appeared)(NebulaActor* actor, void* user);
    void (*killed)(NebulaActor* actor, void* user);
} NebulaObjectClass;

/* The kind of a plugin's object. */
typedef enum NebulaObjectKind {
    NEBULA_OBJECT_ENEMY = 0,
    NEBULA_OBJECT_MAP,      /* a platform or wall the player can stand on */
    NEBULA_OBJECT_NPC,
    NEBULA_OBJECT_RIDE,
    NEBULA_OBJECT_DECORATION  /* nothing collides with it */
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

/* A placed object's stage switches, set in the level data. */
typedef enum NebulaSwitch {
    NEBULA_SWITCH_A = 0,
    NEBULA_SWITCH_B,
    NEBULA_SWITCH_APPEAR,
    NEBULA_SWITCH_DEAD
} NebulaSwitch;

/* Items an object will give, declared so the level counts them. */
typedef enum NebulaItem {
    NEBULA_ITEM_COINS = 0,
    NEBULA_ITEM_STAR_BITS,
    NEBULA_ITEM_POWER_STAR /* count: the star's number in the galaxy; -1: the next one */
} NebulaItem;

/* Camera shake strength. */
typedef enum NebulaShake {
    NEBULA_SHAKE_WEAK = 0,
    NEBULA_SHAKE_NORMAL,
    NEBULA_SHAKE_NORMAL_STRONG,
    NEBULA_SHAKE_STRONG,
    NEBULA_SHAKE_VERY_STRONG
} NebulaShake;

/* A new galaxy. Its files go in StageData/<name>/ like any galaxy's. */
typedef struct NebulaGalaxy {
    uint32_t size;
    const char* name;
    const char* map_pane;           /* its icon's pane in the map layout (MapGrandGalaxy). Null: not on the map */
    const char* open_condition[3];  /* event flags that open it, or null */
    int32_t stars_to_open;          /* power stars needed to open it */
    int32_t dome;                   /* its dome: 1 to 6, 0 for none */
    int32_t has_map_position;       /* 1: the game creates the icon's pane at map_position */
    float map_position[2];          /* right and up from the middle of the dome's page (or the grand map with no dome), on a 608 x 456 page */
} NebulaGalaxy;

/* A new power-up. Give it to the player with set_value(NEBULA_SET_POWER_UP, its number). */
typedef struct NebulaPowerUpClass {
    uint32_t size;
    const char* name;
    int32_t frames;       /* how long it lasts, at 60 per second. 0: until something ends it */
    const char* music;    /* music while it lasts: a BGM_ name, or null */
    int32_t color_frame;  /* the player's colours: a frame of Mario.arc's ColorChange (0 normal, 1 Flying, 2 Fire). 0: those of
                             looks_like. A mod's Mario.arc can add more frames. */
    void* user;
    void (*start)(void* user);
    void (*update)(void* user); /* every frame while it lasts */
    void (*end)(void* user);
    /* The fields below may be missing from an older plugin's class; `size` tells. */
    const char* model;    /* a whole-body model, ObjectData/<model>.arc, on the player's skeleton. Null: the normal model */
    int32_t looks_like;   /* without a model: look like this power-up (NEBULA_POWER_UP_*) */
} NebulaPowerUpClass;

/* When a hook_nerve callback runs. */
typedef enum NebulaNervePhase {
    NEBULA_NERVE_ENTER = 0, /* frames is 0 */
    NEBULA_NERVE_FRAME,     /* before the game's code for this frame; return 0 to skip the game's code */
    NEBULA_NERVE_LEAVE      /* another nerve was set; the game's exit code for this one has run */
} NebulaNervePhase;

/* actor: the object, or the object a state works for; null when there is no object (a screen, the scene control).
   nerve: the nerve's name. frames: as nerve_frames. Runs on the object's thread. The return value only matters for
   NEBULA_NERVE_FRAME; when several plugins hook one nerve they all run, and any 0 skips the game's code. */
typedef int32_t (*NebulaNerveCallback)(NebulaActor* actor, const char* nerve, NebulaNervePhase phase, int32_t frames, void* user);

/* A material's texture coordinate (list_materials): how it is made (the game's TexGen type, source and matrix), its
   matrix's kind and projection (255: no matrix), and whether a stage of the material reads it. */
typedef struct NebulaMaterialCoord {
    /* How the coordinate is made: the game's TexGen type, its source and its matrix slot. */
    uint32_t type, source, matrix;
    /* Its matrix's kind and projection; 255 for both when it has no matrix. */
    uint32_t matrix_kind, projection;
    /* 1 when a stage of the material reads it, 0 when none does. */
    uint32_t used;
} NebulaMaterialCoord;
/* A model's material (list_materials). Valid during the callback. */
typedef struct NebulaMaterialInfo {
    /* sizeof(NebulaMaterialInfo): fields are only added at the end. */
    uint32_t size;
    /* Its place in the model and its name. */
    int32_t index;
    const char* name;
    /* Its texture coordinates: coord_count of them at coords. */
    int32_t coord_count;
    const NebulaMaterialCoord* coords;
} NebulaMaterialInfo;
/* list_materials calls it once for each material of the model, with list_materials' `user`. It returns nothing. */
typedef void (*NebulaMaterialCallback)(const NebulaMaterialInfo* material, void* user);

typedef struct NebulaModApi {
    uint32_t size;
    uint32_t version;
    /* Identifies the game headers this build was made from; see nebula_game_sdk.h. */
    uint64_t game_abi;

    /* Writes one line to the game's log. */
    void (*log)(const NebulaPlugin* self, const char* text);
    /* The choice id the player picked for one of the mod's options. Null: no such option. */
    const char* (*option)(const NebulaPlugin* self, const char* id);
    /* Only in nebula_plugin_start. Calls `callback` at each `event` (NEBULA_MOD_EVENT_*). Its return value matters
       where the event says so (a cancelled hit, changed coins); otherwise return data->value. Returns 0. */
    int32_t (*subscribe)(NebulaModEvent event, NebulaModCallback callback, void* user);
    /* Adds an object class that levels can place by name, run by the plugin's callbacks (NebulaObjectClass).
       0: done; -1: no name, a name already taken, or a class too old to read. */
    int32_t (*register_object)(const NebulaObjectClass* object);
    /* `create` builds the game's own object type (a NameObj) for a placed name; `archive` is loaded for it, or null. */
    int32_t (*register_creator)(const char* name, void* (*create)(const char* name), const char* archive);
    /* Runs `replacement` instead of the game's function at `target`; `original` receives a function that runs the
       previous code (possibly another plugin's hook). Functions the compiler inlined everywhere can't be reached this
       way; use events. Works on Windows x64, and Linux and Mac on x64 and arm64. Returns -1 elsewhere, or when the function's first instructions can't be moved. */
    int32_t (*hook)(void* target, void* replacement, void** original);
    /* A game function by its symbol (on Windows, as dumpbin /exports names it). Null: not found. */
    void* (*find_function)(const char* symbol);

    /* The player. Null when no stage is running, or outside its update, animation and drawing (a hook on
       GameScene::draw is inside; "In a stage" calls below work there too). */
    NebulaActor* (*player)(void);
    /* Position: x, y, z in level units. */
    void (*get_position)(NebulaActor* actor, float* xyz);
    /* Moves it there at once. The player is placed as after a warp; an object just moves. */
    void (*set_position)(NebulaActor* actor, const float* xyz);
    /* Movement per frame: x, y, z. For the player, the game's own. */
    void (*get_velocity)(NebulaActor* actor, float* xyz);
    /* Sets the movement; the game's gravity and friction continue from it. */
    void (*set_velocity)(NebulaActor* actor, const float* xyz);
    /* A game value (NEBULA_VALUE_*). Player values are 0 when no stage is running. */
    int32_t (*value)(NebulaModValue what);
    /* Sets a value (NEBULA_SET_*); does nothing when no stage is running. Life 0 or less kills the player; above the
       maximum is clamped. A power-up is the game's (NEBULA_POWER_UP_*) or one from add_power_up. */
    void (*set_value)(NebulaModSet what, int32_t value);
    /* Kills the player, as a fall out of the level does. */
    void (*kill_player)(void);
    /* Launches the player off the ground with this velocity, like a spring. */
    void (*jump_player)(const float* velocity);
    /* Brings it into the level (after a kill, or before its first appearance): drawn, moving and touchable. */
    void (*appear)(NebulaActor* actor);
    /* Removes it from the level, like a defeated enemy: not drawn, moved or touched. appear brings it back. */
    void (*kill)(NebulaActor* actor);
    /* 1 while it is out of the level (killed, or not yet appeared). */
    int32_t (*is_dead)(NebulaActor* actor);
    /* Shows (1) or hides (0) its model; it still moves and collides. Does nothing for an object without a model. For
       the player this covers hands, face and power-up suit; the shadow stays. */
    void (*show_model)(NebulaActor* actor, int32_t show);
    /* 1: started. 0: the object has no such animation, effect or sound, or nothing to play it on. */
    int32_t (*play)(NebulaActor* actor, NebulaModPlay what, const char* name);
    /* The name it was created with: a placed object's level name (Kuribo). Some of the game's own objects have
       Japanese names. */
    const char* (*actor_name)(NebulaActor* actor);
    /* The current stage (EggStarGalaxy); null when there is none. */
    const char* (*stage_name)(void);
    /* Creates a registered object at a point. Its archive must already be loaded (placed in the level, or part of
       the stage). Null: it can't be created now. */
    NebulaActor* (*spawn)(const char* name, const float* xyz);
    /* Buttons held now: NEBULA_BUTTON_* flags. 0 when no stage is running. */
    uint32_t (*buttons_held)(void);
    /* Buttons pressed this frame only: NEBULA_BUTTON_* flags. */
    uint32_t (*buttons_pressed)(void);
    /* The stick, each axis from -1 to 1 (either pointer may be null). */
    void (*stick)(float* x, float* y);
    /* In a stage: shakes the camera as when something heavy lands (shake_camera_by sets the strength). */
    void (*shake_camera)(void);

    /* Only in nebula_plugin_start. Adds a galaxy. 0: done; -2: no scenario archive (StageData/<name>/<name>Scenario.arc
       in the mod, StageData/<name>Scenario in the store), so it is skipped; -1: any other problem (its name is
       already one of the game's). */
    int32_t (*add_galaxy)(const NebulaGalaxy* galaxy);
    /* A hungry Luma: placed in a level as TicoGalaxy with Arg7 set to `slot`, it opens `galaxy` when fed
       `star_bits`. The game uses slots 0 to 6; using one replaces the game's Luma. */
    int32_t (*add_hungry_luma)(const char* galaxy, int32_t slot, int32_t star_bits);
    /* Replaces or adds a message by id: GalaxyName_<galaxy>, ScenarioName_<galaxy><n>, a conversation's id. An
       existing line keeps its talk settings. The text may use the game's message tags in brackets (nebula_text_tags.h). */
    int32_t (*add_text)(const char* id, const char* utf8);
    /* Adds a power-up and returns its number for set_value; below 0 when it can't be added. */
    int32_t (*add_power_up)(const NebulaPowerUpClass* power_up);
    /* Its down direction, unit length (on a planet this is not a fixed axis). */
    void (*get_gravity)(NebulaActor* actor, float* xyz);
    /* Only in nebula_plugin_start. Adds a music track: `file` is its path in the game, AudioRes/Stream/MyTrack.wav
       (.ogg, .ast). Pick any `name` not starting with SE_, BGM_, MBGM_, STM_ or ME_, which are the game's. 0: done. */
    int32_t (*add_music)(const char* name, const char* file);
    /* Only in nebula_plugin_start. The music a stage plays: `scenario` is a mission, 0 for every mission without its own entry. */
    int32_t (*set_stage_music)(const char* stage, int32_t scenario, const char* name);
    /* Only in nebula_plugin_start. The music a ChangeBgmCube with Obj_arg1 equal to `area` starts in the stage. */
    int32_t (*set_area_music)(const char* stage, int32_t area, const char* name);
    /* In a stage: plays this track (a mod's or the game's, by name) instead of the current one. 1: started. */
    int32_t (*play_music)(const char* name);
    /* In a stage: fades out the current music over this many frames. */
    void (*stop_music)(int32_t frames);
    /* In a stage: plays a movie over the game by its path, MovieData/MyMovie.thp. The game waits while it plays. 1: started. */
    int32_t (*play_movie)(const char* file);
    /* Ends the movie now; the game continues. */
    void (*stop_movie)(void);
    /* 1 while a movie plays. */
    int32_t (*is_movie_playing)(void);
    /* Only in nebula_plugin_start. Changes or adds a progression row by name: what unlocks what, and what each hungry
       Luma costs. `kind`: 1 stars, 2 a galaxy's stars, 3 a special star, 4 two flags, 5 story, 6 a galaxy, 7 a comet,
       8 a hungry Luma. `a` and `b` depend on the kind: a star number, or a Luma's slot and price. 0: done. */
    int32_t (*set_event_flag)(const char* name, int32_t kind, int32_t a, int32_t b, const char* first, const char* second);
    /* Only in nebula_plugin_start. A dome's full comet schedule, in order (domes 1 to 6 are the game's; 7, 8 and up add
       domes as long as the numbers have no gap): `galaxies[i]`, with `coins[i]` 1 for the purple coin comet. */
    int32_t (*set_comet_schedule)(int32_t dome, const char* const* galaxies, const int32_t* coins, int32_t count);

    /* Replaces one of the game's objects with another, or removes it (`becomes` null). Where: NEBULA_OBJECTS_*.
       Call before stages load. */
    int32_t (*set_object_change)(int32_t where, const char* object, const char* becomes);
    /* A comet mission's time limit in seconds. */
    int32_t (*set_comet_time_limit)(const char* galaxy, int32_t mission, int32_t seconds);

    /* The skeleton animation started by play(NEBULA_PLAY_ANIMATION): 1 when it has reached its end, 0 while playing. */
    int32_t (*is_animation_done)(NebulaActor* actor);
    /* Current frame, from 0; 0 without a model. */
    float (*animation_frame)(NebulaActor* actor);
    /* Last frame; 0 without a model. */
    float (*animation_length)(NebulaActor* actor);
    /* Continues from this frame. */
    void (*set_animation_frame)(NebulaActor* actor, float frame);
    /* Playback speed: 1 normal, 2 double, 0 paused, below 0 backwards. */
    void (*set_animation_rate)(NebulaActor* actor, float rate);
    /* Stops its effect with this name; null stops all of them. */
    void (*stop_effect)(NebulaActor* actor, const char* name);
    /* Stops its sound with this name. */
    void (*stop_sound)(NebulaActor* actor, const char* name);

    /* For register_object objects: the current step of the object's behaviour, a number of the plugin's choosing
       (from 0). state_frames counts from the set_state call that started it. */
    void (*set_state)(NebulaActor* actor, int32_t state);
    /* The last value given to set_state; 0 before that, or for objects not from register_object. */
    int32_t (*state)(NebulaActor* actor);
    /* Frames since that set_state, from 0. */
    int32_t (*state_frames)(NebulaActor* actor);

    /* The game's own objects (and screens, and the states objects delegate to) run one nerve at a time: Kuribo
       walks in "Kuribo.Walk". Names are "<class>.<nerve>"; docs/modding/NERVES.md lists them all. nerve: the
       object's current nerve, or null for objects without nerves (register_object objects use set_state). */
    const char* (*nerve)(NebulaActor* actor);
    /* Frames spent in the current nerve, from 0. */
    int32_t (*nerve_frames)(NebulaActor* actor);
    /* Switches to another nerve of the same class from the next frame; the game's code for that nerve runs as if the
       game set it. 0: done; -1: no such nerve in its class. */
    int32_t (*set_nerve)(NebulaActor* actor, const char* nerve);
    /* Only in nebula_plugin_start. Calls `callback` for every object in this nerve: on entry, every frame, and on
       exit. 0: done; -1: no such nerve. */
    int32_t (*hook_nerve)(const char* nerve, NebulaNerveCallback callback, void* user);
    /* The name of the index-th nerve, for listing them all. Null: past the last. */
    const char* (*nerve_at)(int32_t index);

    /* Rotation about x, y and z in degrees, as levels place it. */
    void (*get_rotation)(NebulaActor* actor, float* xyz);
    /* Sets the rotation in degrees. A register_object object with a front (set_front) turns by that instead. */
    void (*set_rotation)(NebulaActor* actor, const float* xyz);
    /* Scale on each axis; 1 is normal size. */
    void (*get_scale)(NebulaActor* actor, float* xyz);
    /* Sets the scale on each axis. */
    void (*set_scale)(NebulaActor* actor, const float* xyz);
    /* For register_object objects: the facing direction, upright against gravity. Its model then turns by this, not by its rotation. */
    void (*set_front)(NebulaActor* actor, const float* xyz);
    /* The facing direction (from set_front, or from turning to the player); 0, 0, 1 for other objects. */
    void (*get_front)(NebulaActor* actor, float* xyz);
    /* Turns to face the player, by at most `degrees` this frame. */
    void (*turn_to_player)(NebulaActor* actor, float degrees);

    /* For register_object objects: whether anything can touch it, including touches that hurt the player. */
    void (*set_touchable)(NebulaActor* actor, int32_t touchable);
    /* Hurts the player through its body, as the game's enemies do (NEBULA_HIT_*). 1 if the player was hit. */
    int32_t (*hit_player)(NebulaActor* actor, NebulaHit how);

    /* For register_object objects: a column of its row in the level, by the name level editors show: Obj_arg0 to
       Obj_arg7 (-1 when unset), CommonPath_ID, ShapeModelNo, CameraSetId, MessageId, GroupId, ClippingGroupId,
       DemoGroupId, ViewGroupId, CastId, ParentID, the SW_ columns, l_id... placement_number returns 1 and the value
       for a number column; 0 for a missing or text column, or for an object not placed by the level (spawn).
       placement_text reads text columns (name), or returns null. */
    int32_t (*placement_number)(NebulaActor* actor, const char* column, double* value);
    /* A text column of its row (its level name, a camera name), or null. */
    const char* (*placement_text)(NebulaActor* actor, const char* column);
    /* Its stage switches (NEBULA_SWITCH_*): 1 when the level assigned that switch. The other switch calls do nothing for an unassigned one. */
    int32_t (*has_switch)(NebulaActor* actor, NebulaSwitch which);
    /* 1 while that switch is on. */
    int32_t (*is_switch_on)(NebulaActor* actor, NebulaSwitch which);
    /* Turns that switch on or off for every object in the level that listens to it. */
    void (*set_switch)(NebulaActor* actor, NebulaSwitch which, int32_t on);

    /* Its rail from the level (CommonPath_ID): 1 when it has one. Without one, the rail calls do nothing or return 0. */
    int32_t (*has_rail)(NebulaActor* actor);
    /* Moves it `speed` along its rail (below 0: backwards) and places it there. */
    void (*move_on_rail)(NebulaActor* actor, float speed);
    /* 1 once it reached the end it is moving toward. */
    int32_t (*rail_at_end)(NebulaActor* actor);
    /* Reverses its direction on the rail. */
    void (*rail_turn_back)(NebulaActor* actor);
    /* Distance along the rail from its start, in level units. */
    float (*rail_coord)(NebulaActor* actor);
    /* Places it this far along the rail (the next move_on_rail continues from there). */
    void (*set_rail_coord)(NebulaActor* actor, float coord);
    /* The rail's total length. */
    float (*rail_length)(NebulaActor* actor);

    /* The world. Returns 1 and the hit point (`hit` may be null) when the line from `from` to `to` crosses the map's
       collision; 0 when it is clear. */
    int32_t (*raycast)(const float* from, const float* to, float* hit);
    /* 1 while standing on the map: the player, or an object with its own collision. */
    int32_t (*is_on_ground)(NebulaActor* actor);
    /* 1 while touching a wall of the map (objects with their own collision). */
    int32_t (*is_touching_wall)(NebulaActor* actor);
    /* 1 if a point is in the stage's water. */
    int32_t (*is_in_water)(const float* xyz);
    /* Distance to the player; 0 when there is no player. */
    float (*distance_to_player)(NebulaActor* actor);
    /* Current position of one of its model's joints; its own position when there is no such joint or model. */
    void (*get_joint_position)(NebulaActor* actor, const char* joint, float* xyz);

    /* The player's facing direction, unit length; 0, 0, 1 when there is no player. */
    void (*player_front)(float* xyz);
    /* 0 takes the player's controls away (they stand still, as in a cutscene); 1 gives them back. */
    void (*set_player_control)(int32_t on);

    /* Items an object gives: declare them in the class's `start` (the game prepares them with the stage), then call
       appear_item when one comes out. */
    void (*declare_item)(NebulaActor* actor, NebulaItem what, int32_t count);
    /* Releases items at a point: `count` coins or star bits at a time, or the power star numbered `count` (below 0:
       the stage's). */
    void (*appear_item)(NebulaActor* actor, NebulaItem what, const float* xyz, int32_t count);

    /* Starts the actor's part in a stage cutscene (by name, from its DemoGroupId). 1: started; 0: another cutscene is
       playing (try again next frame). */
    int32_t (*start_cutscene)(NebulaActor* actor, const char* name);
    /* Ends the actor's part in that cutscene; the cutscene ends when all parts have. */
    void (*end_cutscene)(NebulaActor* actor, const char* name);
    /* 1 while any cutscene plays, while the stage opens (the opening camera and the player's arrival), and during a
       star's "You got a Star!" sequence, none of which send NEBULA_MOD_EVENT_CUTSCENE. The player has control when it
       is 0, which is the best time to show a message. */
    int32_t (*is_cutscene_playing)(void);
    /* In a stage: shakes the camera with this strength (NEBULA_SHAKE_*). */
    void (*shake_camera_by)(NebulaShake how);
    /* In a stage: switches the view to one of the stage's event cameras (by its name in the camera table) over
       `frames` frames. */
    void (*start_camera)(const char* name, int32_t frames);
    /* Hands the view back from that camera over `frames` frames. */
    void (*end_camera)(const char* name, int32_t frames);
    /* In a stage: fades the screen to black over `frames` frames. */
    void (*fade_out)(int32_t frames);
    /* In a stage: fades the screen back in over `frames` frames. */
    void (*fade_in)(int32_t frames);

    /* Sends the player to a galaxy's mission, as a launch star does (the stage ends and the new one loads). */
    void (*change_stage)(const char* galaxy, int32_t mission);
    /* In a stage: sends the player back to the dome, as leaving a galaxy does. */
    void (*back_to_dome)(void);
    /* 1 if the save has this event flag on (a progression name, or one added with set_event_flag).
       "SpinAndStarPointer" is the spin and Star Pointer the Gateway's Luma gives. */
    int32_t (*is_event_flag_on)(const char* name);
    /* Turns the flag on in the save (written at the next save). "SpinAndStarPointer" also grants the spin and the
       Star Pointer at once, in the current stage too. */
    void (*set_event_flag_on)(const char* name);
    /* 1 if the save has that galaxy's star with that number. */
    int32_t (*has_star)(const char* galaxy, int32_t star);
    /* In a stage: shows the game's information box (at the top, or in the middle when `centre` is 1) with a message
       id (the game's or add_text's) or with this text (message tags allowed; lines wrap between words). It stays until
       hide_message. Requested while the stage opens, it appears once the player has control. */
    void (*show_message)(const char* utf8_or_id, int32_t centre);
    /* Hides the information box. */
    void (*hide_message)(void);

    /* A number saved for this mod across sessions (in the mods folder's .plugin-data, written at once). Keys can't
       contain '=' or line breaks. `missing` is returned for a key never set. */
    int32_t (*store_get)(const NebulaPlugin* self, const char* key, int32_t missing);
    /* Saves `value` under `key`. */
    void (*store_set)(const NebulaPlugin* self, const char* key, int32_t value);

    /* Only in nebula_plugin_start. Makes a stage follow another stage's rules: the game's stage-specific rules (a
       Bowser arena's star, a hub's counters, ocean swimming) treat it as `acts_as`. own_rules 0: its own rules no longer
       apply. `scenario`: one mission, 0 for all. 0: done. */
    int32_t (*set_stage_rules)(const char* stage, int32_t scenario, const char* acts_as, int32_t own_rules);
    /* Sets a value in the player's constants table by name (mJumpHeight, mFireModeTime). `character`: 0 Mario, 1 Luigi. `index`: position in a list, else 0.
       Any time. 0: done; -1: no such value. */
    int32_t (*set_player_constant)(int32_t character, const char* name, int32_t index, float value);
    /* The current value; 0 when there is no such value. */
    float (*player_constant)(int32_t character, const char* name, int32_t index);
    /* Only in nebula_plugin_start. A new object name levels can place, built like the game's object `like`, with its files from ObjectData/<name>.arc.
       No code needed. 0: done. */
    int32_t (*set_object_class)(const char* name, const char* like);
    /* Only in nebula_plugin_start. A sound effect played from a file (AudioRes/Stream/Boom.wav, .ogg, .ast): a new name, or one of
       the game's (SE_OJ_COIN_GET) to replace it everywhere, whether played by name or by number (player voices, SE_PV_*).
       "<name>@luigi" replaces it only while playing as Luigi. 0: done. */
    int32_t (*add_sound_effect)(const char* name, const char* file);

    /* Finds an actor by its level name. Pass `after` null to start, or a found actor to continue from it. `name` null matches every actor. */
    NebulaActor* (*find_actor)(const char* name, NebulaActor* after);
    /* Living actors within `radius`. Returns how many; up to `max` are written to `out`. */
    int32_t (*actors_near)(const float* xyz, float radius, NebulaActor** out, int32_t max);
    /* Attacks any actor as the player's attack `how` would. 1 if it hit. `by`: the attacker, null for the player. */
    int32_t (*attack_actor)(NebulaActor* target, NebulaActor* by, NebulaAttack how);
    /* The plugin's own pointer stored on a register_object object (set_actor_data), else null. */
    void* (*actor_data)(NebulaActor* actor);
    /* Stores a pointer of the plugin's on the object; the plugin frees it (in the class's `kill` or at STAGE_END). */
    void (*set_actor_data)(NebulaActor* actor, void* data);
    /* Its level name. */
    const char* (*actor_placed_name)(NebulaActor* actor);
    /* Freezes the whole scene for `frames` frames (0 resumes at once): a hit-stop. */
    void (*stop_scene)(int32_t frames);

    /* The down direction at a point, unit length, from the stage's gravity. */
    void (*gravity_at)(const float* xyz, float* out);
    /* 1 if a point is inside one of the stage's areas of that kind (WaterArea, DeathArea, ChangeBgmCube...). */
    int32_t (*is_in_area)(const char* area, const float* xyz);
    /* The camera's position. */
    void (*camera_position)(float* xyz);
    /* A point 1000 units in front of the camera. */
    void (*camera_target)(float* xyz);
    /* Where a point appears on the screen; returns 0 if it is behind the camera. */
    int32_t (*screen_position)(const float* xyz, float* xy);
    /* Plays a sound not tied to a place. */
    void (*play_system_sound)(const char* name);

    /* The pointer's position in screen coordinates. 0: off the screen. */
    int32_t (*pointer)(float* xy);

    /* Save data: an event value (a named number kept in the save). */
    int32_t (*event_value)(const char* name);
    /* Sets it, clamped to 0..65535 (the save keeps 16 bits). */
    void (*set_event_value)(const char* name, int32_t value);
    /* Gives or takes away a star. */
    void (*set_star)(const char* galaxy, int32_t star, int32_t have);
    /* How many power stars a galaxy has (its scenarios with a star). */
    int32_t (*stars_in_galaxy)(const char* galaxy);
    /* How many of them the save has. */
    int32_t (*stars_won_in_galaxy)(const char* galaxy);
    /* Reads a tunable by its key, as set_tunable takes it ("GameRules.cPlayerHealth"): the current value and the
       game's default. 0: done; -1: no such tunable. Added in version 2 in place of setting and setting_by_number. */
    int32_t (*get_tunable)(const char* key, double* value, double* game_value);

    /* Opens a screen layout (LayoutData/<name>.arc, the game's or a mod's) for the current stage; it is removed with
       the stage. Null if there is no such archive. */
    NebulaLayout* (*layout_open)(const char* name);
    /* Shows (1) or hides (0) it. */
    void (*layout_show)(NebulaLayout* layout, int32_t show);
    /* Sets a pane's text (message tags allowed). */
    void (*layout_set_text)(NebulaLayout* layout, const char* pane, const char* utf8);
    /* Sets a pane's text to a number in the game's digits. */
    void (*layout_set_number)(NebulaLayout* layout, const char* pane, int32_t number);
    /* Shows (1) or hides (0) a pane and the panes inside it. */
    void (*layout_show_pane)(NebulaLayout* layout, const char* pane, int32_t show);
    /* Moves a pane to x, y on the screen (screen_position's coordinates); it stays there. */
    void (*layout_move_pane)(NebulaLayout* layout, const char* pane, float x, float y);
    /* Plays one of its animations by name. */
    void (*layout_play)(NebulaLayout* layout, const char* animation);
    /* 1 while its animation plays. */
    int32_t (*layout_is_playing)(NebulaLayout* layout);
    /* Opacity: 1 as designed, 0 invisible. */
    void (*layout_set_alpha)(NebulaLayout* layout, float alpha);

    /* Text saved for this mod across sessions, next to store_get's numbers. Writes up to `size` - 1 bytes and a
       terminator to `out`; returns how many bytes were read (0 for a key never set). */
    int32_t (*store_get_text)(const NebulaPlugin* self, const char* key, char* out, int32_t size);
    /* Saves `text` under `key`. */
    void (*store_set_text)(const NebulaPlugin* self, const char* key, const char* text);
    /* Opens a galaxy's star select screen, as a launch star does. */
    void (*open_scenario_select)(const char* galaxy);
    /* Only in nebula_plugin_start. Changes the game's story: "first_stage" (with "first_mission", a number as text) is where a new file starts instead of
       the prologue; "hub" is where the player returns after a galaxy instead of the Comet Observatory; "story_scenes" "off" skips the game's story scenes. 0: done. */
    int32_t (*set_story)(const char* key, const char* value);
    /* Writes the save, as the game does after a star. */
    void (*save_game)(void);
    /* Sets a tunable, keyed "<file stem>.<name>" ("GameRules.cPlayerHealth") or "<file stem>.<name>[<i>]" for a list entry, the same keys as luma.json's
       "tunables". Any time; most are read when an object starts, so luma.json's are applied before the first stage. Integer types
       are rounded and clamped. 0: done; -1: no such tunable. */
    int32_t (*set_tunable)(const char* key, double value);
    /* The index-th tunable (each list entry counts): its file ("Kuribo.cpp"), name, list position (-1: not a list), type (1 f32,
       2 f64, 3 s32, 4 u32, 5 s16, 6 u16, 7 s8, 8 u8, 9 bool), the game's default and the current value. 0: done; -1: past the last. */
    int32_t (*tunable_at)(int32_t index, const char** file, const char** name, int32_t* place, int32_t* kind, double* game_value, double* value);

    /* Game rules a plugin can change at any time. Each setting holds until changed and survives stage changes.
       Gravity strength for the player and every object: 1 normal, 0.5 half, 0 none; below 0 pushes away. */
    void (*set_gravity_scale)(float scale);
    /* The current gravity scale. */
    float (*gravity_scale)(void);
    /* The on-screen countdown (a comet's time limit). Returns 1 while one runs, with the seconds left and the total;
       0 when none runs. */
    int32_t (*timer)(float* seconds_left, float* seconds_limit);
    /* Sets the seconds left, within the total. 0: done; -1: no countdown is running. */
    int32_t (*set_timer)(float seconds_left);
    /* 1 freezes every countdown, 0 lets them run. */
    void (*pause_timer)(int32_t paused);
    /* Places the camera at `position` looking at `target`, with a vertical field of view of `fov` degrees. Null for a
       point, or 0 for fov, leaves that part to the game; all three left: the game's camera again. A set position is
       used that frame exactly, with no easing, and passes through walls. */
    void (*set_camera)(const float* position, const float* target, float fov);
    /* The vertical field of view of the last drawn frame, in degrees. */
    float (*camera_fov)(void);
    /* Hides (0) or shows (1, as the game normally would) HUD parts (NEBULA_HUD_*). */
    void (*show_hud)(uint32_t parts, int32_t show);
    /* Scene speed: 1 normal, 0.5 one step every other frame, 0 frozen (menus and the HUD still run). Between 0 and 1.
       Set in a STAGE_FRAME callback, it applies from that frame. */
    void (*set_game_speed)(float speed);
    /* While frozen: runs this many scene steps, one per frame. */
    void (*step_game)(int32_t steps);
    /* Draws a line of text over the game in the game's message font. `line`: slot 0-15. x, y: top-left corner, 0 to 1
       across and down the screen. `size`: height as a fraction of the screen height (0.04 matches the HUD numbers).
       `rgba`: colour, 0xRRGGBBAA. Null or "" removes the line; otherwise it stays, across stages. 0: done; -1: no
       such slot. */
    int32_t (*draw_text)(int32_t line, float x, float y, float size, uint32_t rgba, const char* utf8);
    /* Like add_text; a new message also copies the camera, sound, talk and balloon settings of the game's message
       `like_id` (null or "" behaves like add_text). The text may use the game's message tags in brackets ([wait 30],
       [color 1], [player]...; see nebula_text_tags.h). */
    int32_t (*add_text_like)(const char* id, const char* utf8, const char* like_id);
    /* A player animation: `name` plays the bck `file` from the player's animations (ObjectData/MarioAnime.arc: the
       game's, or one the mod adds there as a .bck or inside a .glb) at `rate` (1: the bck's own speed), blended in over
       `blend_frames` frames (0: the game's 6). Play it by name (NEBULA_PLAY_PLAYER_ANIMATION); a name the game already
       uses replaces that animation wherever the game plays it. Call from the plugin's entry, before the first stage.
       0: done; -1: bad name or file, or called too late. */
    int32_t (*add_player_animation)(const char* name, const char* file, float rate, int32_t blend_frames);
    /* Memory from the game's C library, for plugins built without one (as portable plugins are, so they have no
       malloc). alloc returns null for 0 bytes or when memory runs out; the memory is aligned for any type.
       release(null) does nothing. Memory stays valid until released and is never moved. */
    void* (*alloc)(uint64_t bytes);
    void (*release)(void* memory);
    /* printf-style formatting into `buffer` of `size` bytes, always zero-terminated when size is above 0. Returns what
       snprintf would: the length the full text needs, or below 0 for a bad format. */
    int32_t (*format)(char* buffer, uint64_t size, const char* format, ...);
    /* The camera's near plane in units: nothing closer is drawn. 0: the game's (100; 10 in its own first-person view).
       Kept until set again. The distances at which the game hides objects near its camera are tunables:
       ClippingJudge.cNearClip (500), ClippingJudge.cNearClipSubjective (100) and ObjUtil.cNearClipScale (1). */
    void (*set_camera_near)(float distance);
    /* Unlocks one of the mod's achievements (luma.json "achievements", by id). Returns 1 when newly unlocked, 0 when
       already unlocked or the mod has no such id. Its pop-up shows and the board lists it under Mods. */
    int32_t (*unlock_achievement)(const NebulaPlugin* self, const char* id);
    /* The game's own camera this frame, before set_camera changes it: its position and the point it looks at (either
       may be null). In a cutscene or a conversation this is the shot the game frames. 1 in a stage, 0 otherwise. */
    int32_t (*game_camera)(float* position, float* target);
    /* The character in conversation with the player now, or null when there is none. */
    NebulaActor* (*talking_actor)(void);
    /* While `on` is 1, the large talk box (the one with the A button, also used by signs) is centred on x, y in
       screen_position's coordinates, kept inside the screen, and its pointer turns to the speaker. 0 puts it back in its
       own place. Small balloons already follow their speaker. Reset when the stage ends. */
    void (*place_talk_box)(float x, float y, int32_t on);
    /* `on` 0 hides one joint of an actor's model and every joint under it, on that actor only; 1 shows them again.
       The player keeps it in every form, and his face and hands go with the joint they hang from ("Head" takes his
       head, cap and face). Joint names as in get_joint_position. A new stage starts with all shown. */
    void (*show_joint)(NebulaActor* actor, const char* joint, int32_t on);
    /* The name of joint `index` of an actor's model (0 is the root), or null past the last joint or without a model.
       Names are what get_joint_position and show_joint take. */
    const char* (*joint_name)(NebulaActor* actor, int32_t index);
    /* A joint's place and turn this frame: 12 floats, the rows of a 3x4 matrix. Columns 0, 1 and 2 are the joint's own
       x, y and z axes in the world (scaled with the model), column 3 its position. Without the joint or a model: no
       turn, at the actor's position. */
    void (*get_joint_matrix)(NebulaActor* actor, const char* joint, float* matrix);

    /* A star of the stage played won, as the player wins it: its result counted and its get demo (the Grand Star's with
       `grand` 1) once the stage is in play (after its opening camera and starter). `galaxy`: null or the stage played.
       0: refused (no galaxy is played, or another one is named); 1: done. */
    int32_t (*give_star)(const char* galaxy, int32_t star, int32_t grand);
    /* The save holds Grand Stars 1 to `count` (1-7), and the stage played starts again, so the Observatory's domes are
       built with them. 0: refused; 1: done. */
    int32_t (*give_grand_stars)(int32_t count);
    /* The file is played as Luigi (1) or Mario (0) from the next stage. 0: refused; 1: done. */
    int32_t (*set_player_character)(int32_t luigi);
    /* The materials of the first living actor whose model is `model` (any case), each given to `callback`: how many. */
    int32_t (*list_materials)(const char* model, NebulaMaterialCallback callback, void* user);
    /* A point inside the stage's `index`-th body of water (its oceans first, then its water areas; the index wraps):
       how many bodies of water the stage has (0: none, `xyz` untouched). */
    int32_t (*water_point)(int32_t index, float* xyz);
    /* The circle wipe closes (`open` 0) or opens (1) over `frames` frames. 0: refused (no stage); 1: done. */
    int32_t (*circle_wipe)(int32_t open, int32_t frames);
    /* The story's grant of the spin and the Star Pointer (the Gateway Luma's), which a new file has not reached. 0:
       refused (no stage); 1: done. */
    int32_t (*grant_spin_and_pointer)(void);

    /* Version 3: LumaCore. */
    /* The toolkit: every game function scripts and plugins call (docs/modding/TOOLKIT.md). Its index by name
       ("MR::startBck"; an overloaded name with its value letters after ':'), or -1. */
    int32_t (*toolkit_find)(const char* name);
    /* Calls it. `out` gets the result, then each value an in-out parameter gives back; returns how many values were
       written, or -1 when the call was refused (a missing or stale handle, a wrong class, too few arguments: logged once
       per reason). */
    int32_t (*toolkit_call)(int32_t index, const NebulaValue* args, uint32_t count, NebulaValue* out, uint32_t out_count);
    /* The value for an actor the API gave, and back (null for anything but a living object). */
    NebulaValue (*actor_value)(NebulaActor* actor);
    NebulaActor* (*value_actor)(const NebulaValue* value);
    /* A field of an object's class (or a base's) by name ("mVelocity"): its index for field_get and field_set, or -1
       (docs/modding/FIELDS.md). Handles, text and pointers are read-only. 1: done; 0: refused. */
    int32_t (*field_find)(const NebulaValue* object, const char* name);
    int32_t (*field_get)(const NebulaValue* object, int32_t field, uint32_t element, NebulaValue* out);
    int32_t (*field_set)(const NebulaValue* object, int32_t field, uint32_t element, const NebulaValue* value);
    /* A class's name and its parent's id, by id (0 is NameObj; null past the last), and whether an object is one. */
    const char* (*class_name)(uint32_t id, uint32_t* parent);
    int32_t (*is_a)(const NebulaValue* object, const char* class_name);
    /* The host's: the table it completed (log, store, format), once, before any mod row. Plugins never call it. */
    void (*luma_start)(const struct NebulaModApi* api);
    /* An object a script runs (luma.json "objects"): the mod's /LumaScripts/<mod id>/<script>.lumascript on `shape`,
       whose callbacks the script's handlers take. 0: added; -1: refused (logged with the reason). */
    int32_t (*add_script_object)(const NebulaPlugin* mod, const NebulaObjectClass* shape, const char* script);
} NebulaModApi;

/* True when this Nebula has `field` (fields are added at the end over time):
   if (NEBULA_MOD_API_HAS(api, unlock_achievement)) api->unlock_achievement(self, "coins"); */
#define NEBULA_MOD_API_HAS(api, field) ((api)->size >= offsetof(NebulaModApi, field) + sizeof((api)->field) && (api)->field != 0)

/* HUD parts for show_hud; combine with |. */
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
    NEBULA_OBJECTS_PLACED = 0,       /* anything the game creates by name: a level's objects, areas */
    NEBULA_OBJECTS_DISPLAY_MODEL = 1, /* the still models shown before an item is collected */
    NEBULA_OBJECTS_TREASURE_BOX = 2, /* what a treasure box holds */
    NEBULA_OBJECTS_IN_COUNT
} NebulaObjectsIn;

/* The player's controls, whatever the device. */
typedef enum NebulaButton {
    NEBULA_BUTTON_JUMP = 1u << 0,
    NEBULA_BUTTON_SPIN = 1u << 1, /* held while the spin lasts, pressed on the frame it starts */
    NEBULA_BUTTON_CROUCH = 1u << 2,
    NEBULA_BUTTON_CAMERA = 1u << 3,
    NEBULA_BUTTON_FIRE = 1u << 4, /* the star bit shot */
    NEBULA_BUTTON_DPAD_UP = 1u << 5,
    NEBULA_BUTTON_DPAD_DOWN = 1u << 6,
    NEBULA_BUTTON_DPAD_LEFT = 1u << 7,
    NEBULA_BUTTON_DPAD_RIGHT = 1u << 8,
    NEBULA_BUTTON_PAUSE = 1u << 9,
    NEBULA_BUTTON_MAP = 1u << 10
} NebulaButton;

#if defined(_WIN32) && defined(__cplusplus)
/* A plugin built without a C library that uses floats needs this one symbol the compiler expects. */
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

/* Declares a plugin's two exports: the interface version it was built against, and the start function the game
   calls once before the game starts. */
#define NEBULA_PLUGIN_ENTRY(api, self)                                                                         \
    NEBULA_PLUGIN_FLOATS                                                                                       \
    NEBULA_PLUGIN_C NEBULA_PLUGIN_EXPORT uint32_t nebula_plugin_api(void) { return NEBULA_MOD_API_VERSION; }   \
    NEBULA_PLUGIN_C NEBULA_PLUGIN_EXPORT void nebula_plugin_start(const NebulaModApi* api, const NebulaPlugin* self)

#ifdef __cplusplus
}
#endif

#endif /* NEBULA_MOD_API_H */
