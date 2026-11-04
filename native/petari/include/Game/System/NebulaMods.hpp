#pragma once

#include <nebula_mod_api.h>
#include <nebula_game_api.h>

class NameObj;
class NameObjArchiveListCollector;
class LiveActor;

// The game's side of plugins (nebula_mod_api.h). An event no plugin wants is one load and one branch.
namespace NebulaMods {
    extern bool sWanted[NEBULA_MOD_EVENT_COUNT];

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

    typedef NameObj* (*CreatorFuncPtr)(const char*);
    // A plugin's object by its placed name: how to make it. nullptr: not a plugin's.
    CreatorFuncPtr findCreator(const char* pName);
    void addArchives(NameObjArchiveListCollector* pCollector, const char* pName);
}  // namespace NebulaMods
