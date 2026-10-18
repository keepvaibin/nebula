/* System settings and the Wii Message Board natively.
   - SC: the console's system settings, which the host answers from its Wii settings (sc_get).
   - NWC24: the game posts letters to the Wii Message Board (records, star totals). A PC has no message board, so
     every call succeeds and the letters go nowhere; the game's flow is the console's with WiiConnect24 on.
   - VF: the virtual file system WiiConnect24 works in; nothing to set up. */
#include <string.h>

#include <revolution/nwc24.h>
#include <revolution/sc.h>

#include <nebula_game_api.h>

#include "nebula_sdk.h"

const NebulaHostServices* nebula_host(void);

static u8 setting(u32 item) {
    return (u8)nebula_host()->sc_get(nebula_host()->user, item);
}

u8 SCGetLanguage(void) {
    return setting(NEBULA_SC_LANGUAGE);
}

u8 SCGetSoundMode(void) {
    return setting(NEBULA_SC_SOUND_MODE);
}

u8 SCGetAspectRatio(void) {
    return setting(NEBULA_SC_ASPECT_RATIO);
}

u8 SCGetProgressiveMode(void) {
    return setting(NEBULA_SC_PROGRESSIVE_MODE);
}

u8 SCGetEuRgb60Mode(void) {
    return setting(NEBULA_SC_EURGB60_MODE);
}

u8 SCGetScreenSaverMode(void) {
    return setting(NEBULA_SC_SCREEN_SAVER_MODE);
}

NWC24Err NWC24OpenLib(void* work) {
    (void)work;
    return NWC24_OK;
}

NWC24Err NWC24CloseLib(void) {
    return NWC24_OK;
}

s32 NWC24GetErrorCode(void) {
    return 0;
}

NWC24Err NWC24GetMyUserId(NWC24UserId* id) {
    *id = 0;
    return NWC24_OK;
}

NWC24Err NWC24InitMsgObj(NWC24MsgObj* msg, NWC24MsgType type) {
    (void)type;
    memset(msg, 0, sizeof(*msg));
    return NWC24_OK;
}

NWC24Err NWC24SetMsgToId(NWC24MsgObj* msg, NWC24UserId id) {
    (void)msg;
    (void)id;
    return NWC24_OK;
}

NWC24Err NWC24SetMsgText(NWC24MsgObj* msg, const char* text, u32 length, NWC24Charset charset, NWC24Encoding encoding) {
    (void)msg;
    (void)text;
    (void)length;
    (void)charset;
    (void)encoding;
    return NWC24_OK;
}

NWC24Err NWC24SetMsgAttached(NWC24MsgObj* msg, const char* data, u32 size, NWC24MIMEType type) {
    (void)msg;
    (void)data;
    (void)size;
    (void)type;
    return NWC24_OK;
}

NWC24Err NWC24SetMsgTag(NWC24MsgObj* msg, u16 tag) {
    (void)msg;
    (void)tag;
    return NWC24_OK;
}

NWC24Err NWC24SetMsgLedPattern(NWC24MsgObj* msg, u16 pattern) {
    (void)msg;
    (void)pattern;
    return NWC24_OK;
}

NWC24Err NWC24SetMsgMBDelay(NWC24MsgObj* msg, u8 delay) {
    (void)msg;
    (void)delay;
    return NWC24_OK;
}

NWC24Err NWC24SetMsgAltName(NWC24MsgObj* msg, const u16* name, u32 length) {
    (void)msg;
    (void)name;
    (void)length;
    return NWC24_OK;
}

NWC24Err NWC24SetMsgMBNoReply(NWC24MsgObj* msg, BOOL enable) {
    (void)msg;
    (void)enable;
    return NWC24_OK;
}

NWC24Err NWC24GetMsgSize(const NWC24MsgObj* msg, u32* size) {
    (void)msg;
    *size = 0;
    return NWC24_OK;
}

NWC24Err NWC24CommitMsg(NWC24MsgObj* msg) {
    (void)msg;
    return NWC24_OK;
}

void VFInitEx(void* work, u32 size) {
    (void)work;
    (void)size;
}
