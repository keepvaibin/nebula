#include "Game/Screen/NebulaLauncher.hpp"
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/Sound/Sound.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/Util/SoundUtil.hpp"

#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // The HOME button library's sounds (NebulaHomeMenu.cpp), in the game's system sound bank from 0xD0000.
    enum Sound {
        cSoundReturnApp = 1,
        cSoundGotoMenu = 2,
        cSoundFocus = 4,
        cSoundSelect = 5,
        cSoundCancel = 6,
    };
    // The host's view (NEBULA_HOME_MENU_LAUNCHER_OPEN): bit 16 marks the home screen's, bits 8-15 its page.
    const s32 cLauncherView = 1 << 16;
    enum { cChoicePlay = 0, cChoiceQuit = 1, cChoiceBenchmark = 2 };

    bool sHolding = false;
    bool sLeaving = false;
    s32 sView = -1;  // the view at the last update, for the sounds of what changed

    s32 request(u32 what, u32 value) {
        const NebulaHostServices* const host = nebula_host();
        return host->home_menu(host->user, what, value);
    }

    void play(Sound sound) {
        MR::startSystemSE(SoundID(sound + 0xD0000));
    }
}  // namespace

namespace NebulaLauncher {
    void open() {
        sHolding = request(NEBULA_HOME_MENU_LAUNCHER_OPEN, 0) != 0;
        sView = sHolding ? request(NEBULA_HOME_MENU_VIEW, 0) : -1;
    }

    void update() {
        if (!sHolding || sLeaving) {
            return;
        }
        // Another answer selected, or the notice's page shown or left. The HOME Menu over it has its own sounds.
        const s32 current = request(NEBULA_HOME_MENU_VIEW, 0);
        if ((current & cLauncherView) != 0 && sView >= 0 && (sView & cLauncherView) != 0 && current != sView) {
            const s32 page = current >> 8 & 0xFF;
            if (page != (sView >> 8 & 0xFF)) {
                play(page != 0 ? cSoundSelect : cSoundCancel);
            } else if ((current & 0xFF) != (sView & 0xFF)) {
                play(cSoundFocus);
            }
        }
        sView = current;

        switch (request(NEBULA_HOME_MENU_LAUNCHER_POLL, 0)) {
        case cChoicePlay:
            play(cSoundReturnApp);
            sHolding = false;
            break;
        case cChoiceQuit:
        case cChoiceBenchmark: {  // the host starts again into the benchmark when the game has left
            play(cSoundGotoMenu);
            sLeaving = true;
            AudSystem* const pAudio = AudWrap::getSystem();
            if (pAudio != nullptr) {
                pAudio->preProcessToReset();
            }
            GameSystemFunction::requestQuitGame(true);
            break;
        }
        default:
            break;
        }
    }

    bool isHolding() {
        return sHolding;
    }

    s32 readText(u32 which, char16_t* pText, s32 capacity) {
        s32 length = 0;
        while (length + 1 < capacity) {
            const s32 unit = request(NEBULA_HOME_MENU_LAUNCHER_TEXT, which << 16 | static_cast< u32 >(length));
            if (unit == 0) {
                break;
            }
            pText[length++] = static_cast< char16_t >(unit);
        }
        if (capacity > 0) {
            pText[length] = 0;
        }
        return length;
    }
};  // namespace NebulaLauncher
