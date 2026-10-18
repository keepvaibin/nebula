/* The HOME Menu natively. On the console it is a separate PowerPC module on the disc (HomeButtonMenuWrapperRSO.rso,
   Nintendo's HOME button library), linked at run time; natively the game's seven entry points
   (HomeButtonMenuWrapper.cpp) are these, and Nebula's host shows its own HOME Menu with the same choices: close,
   leave the game ("Wii Menu"), reset, and the remotes' volume and rumble. The game drives it as it drove the
   library: HBMInit opens it, HBMCalc each frame polls for the choice, HBMStartBlackOut closes it at once. */
#include <revolution/hbm/HBMBase.h>

#include <nebula_game_api.h>

const NebulaHostServices* nebula_host(void);

static const HBMDataInfo* sInfo;
static HBMSelectBtnNum sSelected = HBM_SELECT_NULL;
static int sAdjust;

void NebulaHBMCreate(const HBMDataInfo* info) {
    sInfo = info;
}

void NebulaHBMInit(void) {
    sSelected = HBM_SELECT_NULL;
    nebula_host()->hbm(nebula_host()->user, NEBULA_HBM_OPEN, (uint32_t)sAdjust);
}

void NebulaHBMCalc(const HBMControllerData* controllers) {
    (void)controllers;
    if (sSelected == HBM_SELECT_NULL) {
        sSelected = (HBMSelectBtnNum)nebula_host()->hbm(nebula_host()->user, NEBULA_HBM_POLL, 0);
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
