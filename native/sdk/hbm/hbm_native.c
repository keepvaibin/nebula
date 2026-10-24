/* The HOME Menu natively. On the console it is a separate PowerPC module on the disc (HomeButtonMenuWrapperRSO.rso,
   Nintendo's HOME button library), linked at run time; natively the game's seven entry points
   (HomeButtonMenuWrapper.cpp) are these, and Nebula's host keeps a HOME Menu with the same choices: close, reset,
   and leave the game ("Wii Menu"), the last two after "are you sure?". The game draws it (NebulaSettingsMenu). The
   game drives it as it drove the library: HBMInit opens it, HBMCalc each frame polls for the choice,
   HBMStartBlackOut closes it at once; and the library's sounds play as it played them, through the game's
   callback. */
#include <revolution/hbm/HBMBase.h>

#include <nebula_game_api.h>

const NebulaHostServices* nebula_host(void);

static const HBMDataInfo* sInfo;
static HBMSelectBtnNum sSelected = HBM_SELECT_NULL;
static int sAdjust;
static int sView = -1; /* NEBULA_HBM_VIEW at the last HBMCalc, for the sounds of what changed */

static void play(int sound) {
    if (sInfo != NULL && sInfo->sound_callback != NULL) {
        sInfo->sound_callback(HBMSEV_PLAY_SOUND, sound);
    }
}

void NebulaHBMCreate(const HBMDataInfo* info) {
    sInfo = info;
}

void NebulaHBMInit(void) {
    sSelected = HBM_SELECT_NULL;
    nebula_host()->hbm(nebula_host()->user, NEBULA_HBM_OPEN, (uint32_t)sAdjust);
    sView = nebula_host()->hbm(nebula_host()->user, NEBULA_HBM_VIEW, 0);
    play(HBMSE_HOME_BUTTON);
}

void NebulaHBMCalc(const HBMControllerData* controllers) {
    (void)controllers;
    if (sSelected != HBM_SELECT_NULL) {
        return;
    }
    /* Another answer selected; "are you sure?" asked, or left for the list. */
    const int view = nebula_host()->hbm(nebula_host()->user, NEBULA_HBM_VIEW, 0);
    if (view >= 0 && sView >= 0 && view != sView) {
        if ((view >> 8) != (sView >> 8)) {
            play((view >> 8) != 0 ? HBMSE_SELECT : HBMSE_CANCEL);
        } else {
            play(HBMSE_FOCUS);
        }
    }
    sView = view;
    sSelected = (HBMSelectBtnNum)nebula_host()->hbm(nebula_host()->user, NEBULA_HBM_POLL, 0);
    switch (sSelected) {
    case HBM_SELECT_HOMEBTN:
        play(HBMSE_RETURN_APP);
        break;
    case HBM_SELECT_BTN1:
        play(HBMSE_GOTO_MENU);
        break;
    case HBM_SELECT_BTN2:
        play(HBMSE_RESET_APP);
        break;
    default:
        break;
    }
}

/* The host draws its menu over the game's frame. */
void NebulaHBMDraw(void) {
}

HBMSelectBtnNum NebulaHBMGetSelectBtnNum(void) {
    return sSelected;
}

void NebulaHBMSetAdjustFlag(int adjust) {
    sAdjust = adjust;
}

void NebulaHBMStartBlackOut(void) {
    nebula_host()->hbm(nebula_host()->user, NEBULA_HBM_CLOSE, 0);
    sSelected = HBM_SELECT_HOMEBTN;
}
