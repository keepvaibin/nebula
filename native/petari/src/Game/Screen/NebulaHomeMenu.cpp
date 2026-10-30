#include "Game/Screen/NebulaHomeMenu.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <JSystem/JAudio2/JAISound.hpp>

#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // The HOME button library's sounds, in the game's system sound bank from 0xD0000.
    enum Sound {
        cSoundHomeButton = 0,
        cSoundReturnApp,
        cSoundGotoMenu,
        cSoundResetApp,
        cSoundFocus,
        cSoundSelect,
        cSoundCancel,
    };

    NebulaHomeMenu::Choice sChoice = NebulaHomeMenu::cChoiceNone;
    s32 sView = -1;  // view() at the last update, for the sounds of what changed

    s32 request(u32 what, u32 value) {
        const NebulaHostServices* const host = nebula_host();
        return host->hbm(host->user, what, value);
    }

    void play(Sound sound) {
        MR::startSystemSE(JAISoundID(sound + 0xD0000));
    }
}  // namespace

namespace NebulaHomeMenu {
    void open(bool widescreen) {
        sChoice = cChoiceNone;
        request(NEBULA_HBM_OPEN, widescreen ? 1 : 0);
        sView = request(NEBULA_HBM_VIEW, 0);
        play(cSoundHomeButton);
    }

    Choice update() {
        if (sChoice != cChoiceNone) {
            return sChoice;
        }

        // Another answer selected; "are you sure?" asked, or left for the list.
        const s32 current = request(NEBULA_HBM_VIEW, 0);
        if (current >= 0 && sView >= 0 && current != sView) {
            if ((current >> 8) != (sView >> 8)) {
                play((current >> 8) != 0 ? cSoundSelect : cSoundCancel);
            } else {
                play(cSoundFocus);
            }
        }
        sView = current;

        sChoice = static_cast< Choice >(request(NEBULA_HBM_POLL, 0));
        switch (sChoice) {
        case cChoiceClose:
            play(cSoundReturnApp);
            break;
        case cChoiceLeaveGame:
            play(cSoundGotoMenu);
            break;
        case cChoiceReset:
            play(cSoundResetApp);
            break;
        default:
            break;
        }

        return sChoice;
    }

    void close() {
        request(NEBULA_HBM_CLOSE, 0);
        sChoice = cChoiceClose;
    }

    s32 view() {
        return request(NEBULA_HBM_VIEW, 1);
    }

    void point(s32 answer) {
        request(NEBULA_HBM_POINT, static_cast< u32 >(answer));
    }

    void choose(s32 answer) {
        request(NEBULA_HBM_CHOOSE, static_cast< u32 >(answer));
    }
}  // namespace NebulaHomeMenu
