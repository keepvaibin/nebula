#pragma once

// LumaCore's `save` service (docs/modding/services/save.md): a mod's values in the game file loaded now, kept in
// its game data's LUMA chunk (LumaSaveChunk) and written by the game's own saves.

#include <luma_service_provider.h>
#include <nebula_types.h>

struct LumaServicesFacts;
class GameDataHolder;
class SaveDataHandler;

namespace luma {
    namespace save {
        // SaveDataHandleSequence::startLoad: the player picked a file on the file-select screen; values are its from now
        // until the title (the FileSelect stage) comes back.
        void fileLoaded();

        // The service's provider, registered by luma::services::provideOwn.
        const LumaServiceProvider* provider();
        // save.global's (item 7): each mod's values across files in <save root>/<code>/luma/<id>.bin.
        const LumaServiceProvider* globalProvider();
        // save.game's (item 8, permission save.game): the game's own file out (export) and in (offer).
        const LumaServiceProvider* gameProvider();

        // The REMOVED MODS line's half (item 5), game thread. SaveDataHandler::setFile: game-data file `index` (user - 1)
        // * 2 + kind holds these bytes now. Before each save: the forgotten ids out of the files in memory and out of
        // `pHolder`'s chunk (the file being stored; null when none).
        void fileStored(u32 index, const u8* pFile, u32 size);
        void applyForgets(SaveDataHandler* pHandler, GameDataHolder* pHolder);

        // The host's facts (luma_services_facts.h, the export in LumaTime.cpp), once before the first mod: save.game's
        // offer reads the offered file and opens the offer screen through them (null or older: offer is LUMA_E_ABSENT).
        void setFacts(const LumaServicesFacts* pFacts);
    }
}
