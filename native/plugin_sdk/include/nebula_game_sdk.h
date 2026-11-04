/* SPDX-License-Identifier: GPL-3.0-only */
/* For a plugin built with the game's own headers: it can subclass the game's objects and call its functions directly.
   It runs on a game built from exactly these headers; on any other the game leaves it out and says why. */
#ifndef NEBULA_GAME_SDK_H
#define NEBULA_GAME_SDK_H

#include <nebula_mod_api.h>
#include <nebula_game_abi.h>

#define NEBULA_GAME_PLUGIN_ENTRY(api, self)                                                             \
    NEBULA_PLUGIN_FLOATS                                                                                \
    extern "C" NEBULA_PLUGIN_EXPORT uint64_t nebula_plugin_game_abi(void) { return NEBULA_GAME_ABI; }   \
    extern "C" NEBULA_PLUGIN_EXPORT uint32_t nebula_plugin_api(void) { return NEBULA_MOD_API_VERSION; } \
    extern "C" NEBULA_PLUGIN_EXPORT void nebula_plugin_start(const NebulaModApi* api, const NebulaPlugin* self)

#endif
