#pragma once

// The service registry and the services' event queue (services-registry contract): the one place a mod's service
// import and LumaCore.service are answered, and the frame's start point where answers from the outside world reach the
// mods.

#include <luma_service_provider.h>
#include <nebula_types.h>

namespace luma {
    namespace services {
        // One row of the catalog: a service the game defines (services-registry 9). Null name: past the last.
        struct CatalogRow {
            const char* name;
            u16 major;
            u16 minor;
            const char* permission;  // null: none
            const char* contract;    // the page that defines it
        };
        // The catalog's row `index`, or null past the last (the self-description writes them in order).
        const CatalogRow* catalogAt(u32 index);

        // Checks the catalog and registers the module's own services, once (luma_services_open, before the host's).
        void provideOwn();

        // A service's table by name and major with a minor at least `minor`, and the permission it needs (null: none);
        // null when the registry has none. Any thread.
        const void* find(const char* pName, u32 major, u32 minor, const char** ppPermission);

        // A mod's life (services-registry 8), on the game thread, called by the loader: begin before the mod's begin and
        // start; gone after its stop (or its refused start); end after its grace and finish, before its memory goes.
        void modBegin(const LumaMod* pMod, const char* pId, s32 order, const char* const* ppGranted, u32 grantedCount);
        void modGone(const LumaMod* pMod);
        void modEnd(const LumaMod* pMod);

        // The row a report code from 100 to 899 says (the registry's own and the providers'): its id into pId and the
        // names of its args (4 entries, unused ones null). False: no row has the code.
        bool rowOf(u32 code, char* pId, u32 size, const char* const** pppNames);

        // The frame's start, on the game thread, right after luma::frame(): time's timers posted, each mod's events in
        // list order, then time's frame handlers. With nothing posted and no time hook, one atomic load and two plain.
        void frameStart();

        // The time service's two passes (docs/modding/services/time.md): `post` before the events (due timers), `after`
        // once they are given (frame handlers). Each null while no mod needs it, so a frame with no mod pays nothing
        // for them.
        void setTimeHooks(void (*pPost)(), void (*pAfter)());

        // The save's work at the frame's start, first (save.md: a change's write, a finished write
        // let go; 5: a FORGET's quiet save). Null while there is none.
        void setSaveHook(void (*pWork)());
        // Any thread: pWork becomes the save hook at the next frame's start (the host's FORGET, off the game thread).
        void wakeSaveHook(void (*pWork)());

        // The kernel's half of luma_service_provider.h, for the module's own providers.
        const LumaServiceKernel* kernel();

        // Game thread, before the first mod loads (luma_services_open): the module's own services registered (once),
        // and the kernel for the host's providers.
        const LumaServiceKernel* open();
    }
}
