#pragma once

// LumaCore's `save` service (contract services-save, items 2 to 4): a mod's values in the game file loaded now, kept in
// its game data's LUMA chunk (LumaSaveChunk) and written by the game's own saves.

#include <luma_service_provider.h>
#include <nebula_types.h>

namespace luma {
    namespace save {
        // SaveDataHandleSequence::startLoad: the player picked a file on the file-select screen; values are its from now
        // until the title (the FileSelect stage) comes back.
        void fileLoaded();

        // The service's provider, registered by luma::services::provideOwn.
        const LumaServiceProvider* provider();
        // save.global's (item 7): each mod's values across files in <save root>/<code>/luma/<id>.bin.
        const LumaServiceProvider* globalProvider();
    }
}
