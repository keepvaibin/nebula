#pragma once

#include <nebula_mod_api.h>
#include <nebula_game_api.h>

class NameObj;
class LiveActor;
class TableIter;
class TimeLimitLayout;

// The game's side of plugins (nebula_mod_api.h). An event no plugin wants is one load and one branch.
namespace NebulaMods {
    extern bool sWanted[NEBULA_MOD_EVENT_COUNT];
    // Whether any mod has started: its code may ask for what a scene otherwise only makes when its story needs it.
    bool hasCode();

    s32 dispatch(NebulaModEvent event, s32 value, void* pActor, const char* pName);
    // A sound effect's name by its number (NEBULA_MOD_EVENT_SOUND_START), or null.
    const char* soundName(u32 id);

    inline s32 fire(NebulaModEvent event, s32 value = 0, void* pActor = nullptr, const char* pName = nullptr) {
        return sWanted[event] ? dispatch(event, value, pActor, pName) : value;
    }

    // While one lives, a stage's frame is running: the player can be asked for.
    struct StageFrame {
        StageFrame();
        ~StageFrame();
    };

    // A game scene's animation or drawing: in a stage for the mod API as its update is (a plugin's hook on
    // GameScene::draw changes the stage, sets flags and places the player), with no frame events.
    struct InStage {
        explicit InStage(bool on);
        ~InStage();
        bool mOn;
    };

    // Power-ups plugins add. Numbers from here on are theirs; a mode below is the game's and these do nothing.
    const u16 cFirstPowerUp = 32;
    // How long it lasts, once started on the player.
    s32 powerUpStart(u16 mode, LiveActor* pPlayer);
    void powerUpUpdate(u16 mode);
    void powerUpEnd(u16 mode, LiveActor* pPlayer);
    s32 powerUpCount();
    const char* powerUpModel(s32 index);
    // The game's power-up a mode looks like: itself, if it's the game's.
    u16 powerUpLook(u16 mode);
    // A mod's power-up: whether `mode` is one, and the game's form the player is in under it (what he can do).
    bool isPowerUp(u16 mode);
    u16 powerUpActsLike(u16 mode);
    // The frame of Mario.arc's ColorChange the player wears in mode (a game's or a mod's power-up).
    u32 powerUpColorFrame(u16 mode);
    // A mod's words for a message: nullptr if none has any. ppLike, if given: the game message whose talk a new line takes
    // (nullptr: none named).
    const char16_t* text(const char* pId, const char** ppLike = nullptr);
    // What a hungry Luma of this slot costs, if a mod says: -1 otherwise.
    s32 hungryLumaPrice(s32 slot);
    s32 hungryLumaSlots();

    // Tracks mods add: sound ids from here on have no row in the game's tables. -1 where there's no such track.
    const u32 cFirstTrackWave = 0x4000;
    u32 trackId(const char* pName);
    s32 trackFile(u32 wave);
    u32 stageTrack(const char* pStage, s32 scenario);
    u32 areaTrack(const char* pStage, s32 area);

    // A galaxy whose icon has no pane in the map's layout: the pane to make, under `pParent`.
    bool mapSpot(const char* pGalaxy, f32* pX, f32* pY, s32* pDome);

    typedef NameObj* (*CreatorFuncPtr)(const char*);
    // A plugin's object by its placed name: how to make it. nullptr: not a plugin's.
    CreatorFuncPtr findCreator(const char* pName);
    // The mod object (luma.json "objects", a plugin's class) a placed name makes: by its own name, or by what an
    // object change turns it into. Null for the game's own.
    const char* modObjectName(const char* pPlacedName);
    // What a mod has made of an object of the game's, where `where` (NEBULA_OBJECTS_*) is: its own name unchanged, another's, or null: gone.
    const char* objectBecomes(s32 where, const char* pName);
    // Frames, from what mods say of a comet mission; 0: they say nothing.
    u32 cometTimeLimit(const char* pGalaxy, s32 mission);
    // The journey, if a mod says: the stage named, or the game's own.
    const char* firstStage();
    s32 firstMission();
    const char* hubStage();
    bool noStoryScenes();
    // The mods' player animations (add_player_animation), each name once (the last added), for the player's tables;
    // taken the first time the player is built: none are added after.
    struct PlayerAnimation {
        const char* mName;
        const char* mFile;
        f32 mRate;
        u32 mBlendFrames;  // 0: the game's
    };
    s32 takePlayerAnimations(const PlayerAnimation** ppAnimations);
    // A sound a mod plays from a file, by its name: -1 for none.
    extern bool sHasSoundEffects;
    u32 soundEffectSlow(const char* pName);
    // The sound to start for id: a mod's in place of a game sound it replaced (by name, wherever the game plays it, by name
    // or by number, as the player's voices are), with one for Luigi ("<name>@luigi") while he plays; else id.
    u32 soundFor(u32 id, bool luigi);
    // A stage asked about by its name. 1: a mod says this stage plays by that one's rules. 0: it says it doesn't. -1: nothing said.
    extern bool sHasStageRules;
    s32 stageRuleSlow(const char* pStage, const char* pAskedName);
    inline s32 stageRule(const char* pStage, const char* pAskedName) {
        return sHasStageRules ? stageRuleSlow(pStage, pAskedName) : -1;
    }
    // With a mod on: an object a level places whose mod-made class or plugin names an archive the store lacks. It's left
    // out and the log says so, not a stop. The game's own archives are in every store (a mod store lies over the base).
    bool isMissingFiles(const char* pName);
    // A name a level places that a mod has made as another of the game's objects: that object's name. The name itself otherwise.
    const char* objectLike(const char* pName);
    // What a model named by an object of that class comes from.
    const char* modelFor(const char* pObjectName, const char* pModelName);

    // The game's rules as plugins change them (set_gravity_scale, set_game_speed, set_camera, show_hud, the countdown):
    // with none changed each check is one load.
    extern f32 sGravityScale;
    inline f32 gravityScale() {
        return sGravityScale;
    }
    // Whether the scene runs a step this frame: decided once a frame (StageFrame), for the step and the scene's stops.
    extern bool sSceneStep;
    inline bool isSceneStep() {
        return sSceneStep;
    }
    // A plugin's camera: bit 0 a position, bit 1 a target, in pPosition and pTarget; 0 when it sets neither.
    extern bool sCameraSet;
    u32 cameraPoseSlow(f32* pPosition, f32* pTarget);
    inline u32 cameraPose(f32* pPosition, f32* pTarget) {
        return sCameraSet ? cameraPoseSlow(pPosition, pTarget) : 0;
    }
    // The near plane: a plugin's, else the game's.
    extern f32 sCameraNear;
    inline f32 cameraNear(f32 nearZ) {
        return sCameraNear > 0.0f ? sCameraNear : nearZ;
    }
    // The game's own camera this frame, before a plugin's (CameraDirector::calcPose), for game_camera.
    void recordGameCamera(f32 x, f32 y, f32 z, f32 targetX, f32 targetY, f32 targetZ);
    // Where a plugin wants the large talk box (place_talk_box): its middle in screen coordinates. False: its own place.
    bool talkBoxPlace(f32* pX, f32* pY);
    // The angle of view: a plugin's, else the game's `fovy`. Kept as the last drawn.
    f32 cameraFov(f32 fovy);
    // The HUD parts plugins hide (NEBULA_HUD_*).
    extern u32 sHudHidden;
    inline bool isHudHidden(u8 part) {
        return (sHudHidden & part) != 0u;
    }
    // The countdown on the screen while it shows (TimeLimitLayout), and whether plugins hold it.
    void setCountdown(TimeLimitLayout* pLayout, bool shown);
    extern bool sCountdownHeld;
    // The scene ends: what pointed into it goes.
    void sceneEnded();

    // The lines of text plugins draw over the game (NebulaModText).
    const s32 cTextLines = 16;
    const s32 cTextLength = 128;
    struct TextLine {
        bool mShown;
        f32 mX;
        f32 mY;
        f32 mSize;
        u32 mRgba;
        char16_t mText[cTextLength];
    };
    const TextLine& textLine(s32 line);
    // Counts up with every change to the lines.
    u32 textRevision();
}  // namespace NebulaMods
