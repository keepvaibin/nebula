/* SPDX-License-Identifier: GPL-3.0-only */
/* The rows of mods that are not code (luma.json), as the host hands them to the game module: galaxies, text, music,
   progression, object changes, power-ups, sounds, player animations and constants, story, tunables, and the events an
   achievement waits for. A mod's code is a LumaCore mod (luma.h); the old plugin interface, the script interpreter
   and its toolkit are gone.

   Passing null where a call needs a name makes the call do nothing and return -1. Passing NaN or infinity as a value
   also does nothing. */
#ifndef NEBULA_MOD_API_H
#define NEBULA_MOD_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The table's layout: the host and the game module are built together, so a different number is refused. */
#define NEBULA_MOD_API_VERSION 4u

/* An object in the game (a LiveActor in the game's code), as an event names it. */
typedef struct NebulaActor NebulaActor;

/* Callbacks run on the thread where the event happens: objects created while a stage loads are reported on the
   loading thread, everything else on the game thread. */
typedef enum NebulaModEvent {
    /* Every frame, in every scene including menus. */
    NEBULA_MOD_EVENT_FRAME = 0,
    /* Every frame of a stage, before objects move. */
    NEBULA_MOD_EVENT_STAGE_FRAME,
    /* A stage started. name: the stage. value: the mission. */
    NEBULA_MOD_EVENT_STAGE_START,
    /* The player is about to lose one health. Return 0 to cancel the hit. */
    NEBULA_MOD_EVENT_PLAYER_DAMAGE,
    /* The player is about to die. value: the cause (0 is falling out of the level). Return -1 to cancel. */
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
    /* A line of dialogue opened in its balloon. actor: the speaker. name: the message id, as luma.json "text" uses it
       (null for a line without an id). */
    NEBULA_MOD_EVENT_TALK_LINE,
    /* The player answered a yes/no question. value: 1 yes, 0 no. actor: who asked. name: the question's message id. */
    NEBULA_MOD_EVENT_TALK_CHOICE,
    /* A part of a cutscene started. value: the part's index, from 0. name: the part's name in the cutscene's
       time sheet. */
    NEBULA_MOD_EVENT_CUTSCENE_PART,
    /* The camera changed. value: 0 for an area camera (also when the last event camera ends), 1 for an event camera.
       name: its id in the stage's camera table. */
    NEBULA_MOD_EVENT_CAMERA_CHANGE,
    /* A sound effect is about to play. value: its number. name: its name. The returned number plays instead (the same,
       another sound's, or -1 for silence). Costs one call per sound. */
    NEBULA_MOD_EVENT_SOUND_START,
    /* An object started an effect. actor: the object. name: the effect's name in the object's effect table. */
    NEBULA_MOD_EVENT_EFFECT_START,
    /* A screen layout (a counter, a menu, a message box) was shown (value 1) or removed (value 0). name: its name in
       the game's code. */
    NEBULA_MOD_EVENT_SCREEN,
    /* An object received a message from another: an attack, a stomp, a push, a grab (numbers in
       docs/modding/EVENTS.md). actor: the receiver. name: its name. value: the message. Return -1 to cancel it. */
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

/* Callbacks subscribed to the same event run in the order they subscribed; each receives the previous one's return
   value. Return data->value to change nothing. */
typedef int32_t (*NebulaModCallback)(const NebulaModEventData* data, void* user);

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

/* A new power-up (luma.json "forms"). */
typedef struct NebulaPowerUpClass {
    uint32_t size;
    const char* name;
    int32_t frames;       /* how long it lasts, at 60 per second. 0: until something ends it */
    const char* music;    /* music while it lasts: a BGM_ name, or null */
    int32_t color_frame;  /* the player's colours: a frame of Mario.arc's ColorChange (0 normal, 1 Flying, 2 Fire). 0: those of
                             looks_like. A mod's Mario.arc can add more frames. */
    const char* model;    /* a whole-body model, ObjectData/<model>.arc, on the player's skeleton. Null: the normal model */
    int32_t looks_like;   /* without a model: look like this power-up (NEBULA_POWER_UP_*) */
    int32_t acts_like;    /* what the player can do in it: one of the game's power-ups (NEBULA_POWER_UP_*): Fire's throw,
                             Ice's skate, Bee's flight... Its music and time too, where the class gives none (frames 0,
                             music null). 0: the normal player. One the game can't give in this stage refuses the power-up. */
} NebulaPowerUpClass;

/* A material's texture coordinate (nebula_mod_list_materials, NebulaModCalls.hpp): how it is made (the game's TexGen
   type, source and matrix), its matrix's kind and projection (255: no matrix), and whether a stage of the material
   reads it. */
typedef struct NebulaMaterialCoord {
    uint32_t type, source, matrix;
    uint32_t matrix_kind, projection;
    uint32_t used;
} NebulaMaterialCoord;
/* A model's material (nebula_mod_list_materials). Valid during the callback. */
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
/* Called once for each material of the model, with the call's `user`. */
typedef void (*NebulaMaterialCallback)(const NebulaMaterialInfo* material, void* user);

/* Where an object change applies. */
typedef enum NebulaObjectsIn {
    NEBULA_OBJECTS_PLACED = 0,        /* anything the game creates by name: a level's objects, areas */
    NEBULA_OBJECTS_DISPLAY_MODEL = 1, /* the still models shown before an item is collected */
    NEBULA_OBJECTS_TREASURE_BOX = 2,  /* what a treasure box holds */
    NEBULA_OBJECTS_IN_COUNT
} NebulaObjectsIn;

/* The game module's table (nebula_game_mod_api). The host applies every mod's rows through it once, before the first
   stage; each call returns 0 when done unless it says otherwise. */
typedef struct NebulaModApi {
    uint32_t size;
    uint32_t version;

    /* Calls `callback` at each `event` (NEBULA_MOD_EVENT_*). Its return value matters where the event says so;
       otherwise it returns data->value. */
    int32_t (*subscribe)(NebulaModEvent event, NebulaModCallback callback, void* user);
    /* Adds a galaxy. -2: no scenario archive (StageData/<name>/<name>Scenario.arc in the mod, StageData/<name>Scenario
       in the store), so it is skipped; -1: any other problem (its name is already one of the game's). */
    int32_t (*add_galaxy)(const NebulaGalaxy* galaxy);
    /* A hungry Luma: placed in a level as TicoGalaxy with Arg7 set to `slot`, it opens `galaxy` when fed
       `star_bits`. The game uses slots 0 to 6; using one replaces the game's Luma. */
    int32_t (*add_hungry_luma)(const char* galaxy, int32_t slot, int32_t star_bits);
    /* Replaces or adds a message by id: GalaxyName_<galaxy>, ScenarioName_<galaxy><n>, a conversation's id. An
       existing line keeps its talk settings. The text may use the game's message tags in brackets (nebula_text_tags.h). */
    int32_t (*add_text)(const char* id, const char* utf8);
    /* Like add_text; a new message also copies the camera, sound, talk and balloon settings of the game's message
       `like_id` (null or "" behaves like add_text). */
    int32_t (*add_text_like)(const char* id, const char* utf8, const char* like_id);
    /* Adds a power-up: its number, or below 0 when it can't be added. */
    int32_t (*add_power_up)(const NebulaPowerUpClass* power_up);
    /* Adds a music track: `file` is its path in the game, AudioRes/Stream/MyTrack.wav (.ogg, .ast). Any `name` not
       starting with SE_, BGM_, MBGM_, STM_ or ME_, which are the game's. */
    int32_t (*add_music)(const char* name, const char* file);
    /* The music a stage plays: `scenario` is a mission, 0 for every mission without its own entry. */
    int32_t (*set_stage_music)(const char* stage, int32_t scenario, const char* name);
    /* The music a ChangeBgmCube with Obj_arg1 equal to `area` starts in the stage. */
    int32_t (*set_area_music)(const char* stage, int32_t area, const char* name);
    /* Changes or adds a progression row by name: what unlocks what, and what each hungry Luma costs. `kind`: 1 stars,
       2 a galaxy's stars, 3 a special star, 4 two flags, 5 story, 6 a galaxy, 7 a comet, 8 a hungry Luma. `a` and `b`
       depend on the kind: a star number, or a Luma's slot and price. */
    int32_t (*set_event_flag)(const char* name, int32_t kind, int32_t a, int32_t b, const char* first, const char* second);
    /* A dome's full comet schedule, in order (domes 1 to 6 are the game's; 7, 8 and up add domes as long as the
       numbers have no gap): `galaxies[i]`, with `coins[i]` 1 for the purple coin comet. */
    int32_t (*set_comet_schedule)(int32_t dome, const char* const* galaxies, const int32_t* coins, int32_t count);
    /* Replaces one of the game's objects with another, or removes it (`becomes` null). Where: NEBULA_OBJECTS_*. */
    int32_t (*set_object_change)(int32_t where, const char* object, const char* becomes);
    /* A comet mission's time limit in seconds. */
    int32_t (*set_comet_time_limit)(const char* galaxy, int32_t mission, int32_t seconds);
    /* Makes a stage follow another stage's rules: the game's stage-specific rules (a Bowser arena's star, a hub's
       counters, ocean swimming) treat it as `acts_as`. own_rules 0: its own rules no longer apply. `scenario`: one
       mission, 0 for all. */
    int32_t (*set_stage_rules)(const char* stage, int32_t scenario, const char* acts_as, int32_t own_rules);
    /* Sets a value in the player's constants table by name (mJumpHeight, mFireModeTime). `character`: 0 Mario,
       1 Luigi. `index`: position in a list, else 0. -1: no such value. */
    int32_t (*set_player_constant)(int32_t character, const char* name, int32_t index, float value);
    /* A new object name levels can place, built like the game's object `like`, with its files from
       ObjectData/<name>.arc. */
    int32_t (*set_object_class)(const char* name, const char* like);
    /* A sound effect played from a file (AudioRes/Stream/Boom.wav, .ogg, .ast): a new name, or one of the game's
       (SE_OJ_COIN_GET) to replace it everywhere, whether played by name or by number (player voices, SE_PV_*).
       "<name>@luigi" replaces it only while playing as Luigi. */
    int32_t (*add_sound_effect)(const char* name, const char* file);
    /* A player animation: `name` plays the bck `file` from the player's animations (ObjectData/MarioAnime.arc: the
       game's, or one the mod adds there as a .bck or inside a .glb) at `rate` (1: the bck's own speed), blended in over
       `blend_frames` frames (0: the game's 6); a name the game already uses replaces that animation wherever the game
       plays it. -1: bad name or file, or too late (the player is built). */
    int32_t (*add_player_animation)(const char* name, const char* file, float rate, int32_t blend_frames);
    /* Changes the game's story: "first_stage" (with "first_mission", a number as text) is where a new file starts
       instead of the prologue; "hub" is where the player returns after a galaxy instead of the Comet Observatory;
       "story_scenes" "off" skips the game's story scenes. */
    int32_t (*set_story)(const char* key, const char* value);
} NebulaModApi;

#ifdef __cplusplus
}
#endif

#endif /* NEBULA_MOD_API_H */
