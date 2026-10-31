/* The Wii Remote speaker's entry points (SpkSpeakerCtrl): the game's encoded sound goes to the host. The controller
   itself reaches the game as native input (input_read, Game/System/WPadHolder.cpp). Results are given at once. */
#include <revolution/wpad.h>

#include <nebula_game_api.h>

#include "nebula_sdk.h"

const NebulaHostServices* nebula_host(void);

static BOOL sSpeakerEnabled[WPAD_MAX_CONTROLLERS];

static BOOL connected(s32 chan) {
    NebulaInputDevice device = {0};
    nebula_host()->input_read(nebula_host()->user, (uint32_t)chan, &device, NULL, 0);
    return device.connected != 0;
}

s32 WPADControlSpeaker(s32 chan, u32 command, WPADCallback callback) {
    sSpeakerEnabled[chan] = command != 0;
    if (callback != NULL) {
        callback(chan, connected(chan) ? WPAD_ERR_NONE : WPAD_ERR_NO_CONTROLLER);
    }
    return WPAD_ERR_NONE;
}

BOOL WPADIsSpeakerEnabled(s32 chan) {
    return sSpeakerEnabled[chan];
}

BOOL WPADCanSendStreamData(s32 chan) {
    return sSpeakerEnabled[chan] && connected(chan);
}

s32 WPADSendStreamData(s32 chan, void* data, u16 length) {
    nebula_host()->wpad_speaker_data(nebula_host()->user, (uint32_t)chan, (const uint8_t*)data, length);
    return WPAD_ERR_NONE;
}

u8 WPADGetSpeakerVolume(void) {
    return (u8)nebula_host()->sc_get(nebula_host()->user, NEBULA_SC_SPEAKER_VOLUME);
}
