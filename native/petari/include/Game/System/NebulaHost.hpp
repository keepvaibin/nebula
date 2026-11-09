#pragma once

// Nebula's services, asked directly by the game (nebula_game_api.h): the host's options,
// its settings menu and button icons, achievements and play stats, presence, the galaxy select's
// names, scene and light signals for the renderer, art export, test requests.


#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace NebulaHost {
    inline u32 option(u32 what) {
        return nebula_host()->option(nebula_host()->user, what);
    }

    inline void gameContext(u32 what, s32 value) {
        nebula_host()->game_context(nebula_host()->user, what, value);
    }

    // The players the host's input serves, fixed for the session (a host without the service: one).
    inline u32 inputPlayers() {
        const NebulaHostServices* const host = nebula_host();
        return host->input_players != nullptr ? host->input_players(host->user) : 1;
    }

    inline int galaxyTravel(char* pStage, u32 capacity, s32* pStar) {
        return nebula_host()->galaxy_travel(nebula_host()->user, pStage, capacity, pStar);
    }

    // Nanoseconds on a steady clock: durations a person feels (a hold, a press), the same at any frame rate.
    inline u64 clockNs() {
        return nebula_host()->clock_ns(nebula_host()->user);
    }


    // An object as the host names it (markers, lights, names): its address.
    inline u64 objectId(const void* pObject) {
        return static_cast< u64 >(reinterpret_cast< uintptr_t >(pObject));
    }

    inline void gameStage(const char* pName) {
        nebula_host()->game_stage(nebula_host()->user, pName);
    }

    inline void presence(const u16* pGalaxy, const u16* pStar, s32 powerStars) {
        nebula_host()->presence(nebula_host()->user, pGalaxy, pStar, powerStars);
    }

    inline s32 achievement(const char* pId, u16* pName, u32 capacity) {
        return nebula_host()->achievement(nebula_host()->user, pId, pName, capacity);
    }

    inline s32 achievementDate(const char* pId) {
        return nebula_host()->achievement_date(nebula_host()->user, pId);
    }

    inline s32 modAchievementCount() {
        return nebula_host()->mod_achievement_count(nebula_host()->user);
    }

    inline bool modAchievement(s32 index, const char** ppKey, const char** ppName, const char** ppDescription, bool* pHidden) {
        s32 hidden = 0;
        const bool found = nebula_host()->mod_achievement(nebula_host()->user, index, ppKey, ppName, ppDescription, nullptr, &hidden) != 0;
        if (pHidden != nullptr) {
            *pHidden = hidden != 0;
        }
        return found;
    }

    inline s32 modAchievementUnlocked() {
        return nebula_host()->mod_achievement_unlocked(nebula_host()->user);
    }

    inline void statAdd(const char* pName, u64 delta) {
        nebula_host()->stat_add(nebula_host()->user, pName, delta);
    }

    inline u64 statGet(const char* pName) {
        return nebula_host()->stat_get(nebula_host()->user, pName);
    }

    inline void settingsView(NebulaSettingsView* pView) {
        nebula_host()->settings_view(nebula_host()->user, pView);
    }

    inline void settingsInput(u32 action, s32 arg) {
        nebula_host()->settings_input(nebula_host()->user, action, arg);
    }

    inline void sharp2D(u32 on) {
        nebula_host()->sharp_2d(nebula_host()->user, on);
    }

    inline void buttonIcons(NebulaButtonIcons* pIcons) {
        nebula_host()->button_icons(nebula_host()->user, pIcons);
    }

    inline void textureHd(u32 texture, const void* pHd) {
        nebula_host()->texture_hd(nebula_host()->user, texture, pHd);
    }

    inline u32 layer2DLines() {
        return nebula_host()->layer_2d_lines(nebula_host()->user);
    }

    inline s32 pointerWheel() {
        return nebula_host()->pointer_wheel(nebula_host()->user);
    }

    inline int galaxySelectStage(u32 index, char* pId, u32 capacity, u32* pStars) {
        return nebula_host()->galaxy_select_stage(nebula_host()->user, index, pId, capacity, pStars);
    }

    inline int galaxySelectNamesWanted() {
        return nebula_host()->galaxy_select_names_wanted(nebula_host()->user);
    }

    inline void galaxySelectName(u32 index, u32 star, const u16* pName) {
        nebula_host()->galaxy_select_name(nebula_host()->user, index, star, pName);
    }

    // 1 once when the window lost focus and the player chose to pause then (the game opens its pause menu as the button does).
    inline int takePauseRequest() {
        return nebula_host()->take_pause_request(nebula_host()->user);
    }
}  // namespace NebulaHost
