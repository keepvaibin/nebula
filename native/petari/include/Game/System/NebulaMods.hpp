#pragma once

#include <nebula_mod_api.h>
#include <nebula_game_api.h>

class NameObj;
class NameObjArchiveListCollector;
class LiveActor;

// The game's side of plugins (nebula_mod_api.h). An event no plugin wants is one load and one branch.
namespace NebulaMods {
    extern bool sWanted[NEBULA_MOD_EVENT_COUNT];
    // Whether any mod has started: its code may ask for what a scene otherwise only makes when its story needs it.
    bool hasCode();

    s32 dispatch(NebulaModEvent event, s32 value, void* pActor, const char* pName);

    inline s32 fire(NebulaModEvent event, s32 value = 0, void* pActor = nullptr, const char* pName = nullptr) {
        return sWanted[event] ? dispatch(event, value, pActor, pName) : value;
    }

    // While one lives, a stage's frame is running: the player can be asked for.
    struct StageFrame {
        StageFrame();
        ~StageFrame();
    };

    // Power-ups plugins add. Numbers from here on are theirs; a mode below is the game's and these do nothing.
    const u16 cFirstPowerUp = 32;
    // How long it lasts, once started on the player.
    s32 powerUpStart(u16 mode, LiveActor* pPlayer);
    void powerUpUpdate(u16 mode);
    void powerUpEnd(u16 mode, LiveActor* pPlayer);
    // A mod's words for a message: nullptr if none has any.
    const char16_t* text(const char* pId);
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
    // What a mod has made of an object of the game's, where `where` (NEBULA_OBJECTS_*) is: its own name unchanged, another's, or null: gone.
    const char* objectBecomes(s32 where, const char* pName);
    // Frames, from what mods say of a comet mission; 0: they say nothing.
    u32 cometTimeLimit(const char* pGalaxy, s32 mission);
    void addArchives(NameObjArchiveListCollector* pCollector, const char* pName);
}  // namespace NebulaMods
