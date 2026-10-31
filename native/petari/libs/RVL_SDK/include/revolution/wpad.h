#ifndef WPAD_H
#define WPAD_H

#include "revolution/os.h"
#include "revolution/sc.h"
#include "revolution/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*WPADExtensionCallback)(s32 chan, s32 result);
typedef void (*WPADConnectCallback)(s32 chan, s32 reason);
typedef void (*WPADCallback)(s32 chan, s32 result);
typedef void* (*WPADAlloc)(u32 size);
typedef u8 (*WPADFree)(void* ptr);
typedef void (*WPADSamplingCallback)(s32 chan);

#define WPAD_CHAN0 0
#define WPAD_CHAN1 1
#define WPAD_CHAN2 2

#define WPAD_MAX_CONTROLLERS 4

#define WPAD_MOTOR_STOP 0
#define WPAD_MOTOR_RUMBLE 1

#define WPAD_BUTTON_LEFT 0x0001
#define WPAD_BUTTON_RIGHT 0x0002
#define WPAD_BUTTON_DOWN 0x0004
#define WPAD_BUTTON_UP 0x0008
#define WPAD_BUTTON_PLUS 0x0010
#define WPAD_BUTTON_2 0x0100
#define WPAD_BUTTON_1 0x0200
#define WPAD_BUTTON_B 0x0400
#define WPAD_BUTTON_A 0x0800
#define WPAD_BUTTON_MINUS 0x1000
#define WPAD_BUTTON_Z 0x2000
#define WPAD_BUTTON_C 0x4000
#define WPAD_BUTTON_HOME 0x8000

#define WPAD_DPD_MAX_OBJECTS 4
#define WPAD_DPD_IMG_RESO_WX 1024
#define WPAD_DPD_IMG_RESO_WY 768

#define WPAD_DEV_CORE 0
#define WPAD_DEV_FREESTYLE 1
#define WPAD_DEV_CLASSIC 2
#define WPAD_DEV_FUTURE 251
#define WPAD_DEV_NOT_SUPPORTED 252
#define WPAD_DEV_NOT_FOUND 253
#define WPAD_DEV_UNKNOWN 255

#define WPAD_FMT_CORE 0
#define WPAD_FMT_CORE_ACC 1
#define WPAD_FMT_CORE_ACC_DPD 2
#define WPAD_FMT_FREESTYLE 3
#define WPAD_FMT_FREESTYLE_ACC 4
#define WPAD_FMT_FREESTYLE_ACC_DPD 5
#define WPAD_FMT_CLASSIC 6
#define WPAD_FMT_CLASSIC_ACC 7
#define WPAD_FMT_CLASSIC_ACC_DPD 8

#define WPAD_STATE_DISABLED 0
#define WPAD_STATE_SETUP 3

#define WPAD_BATTERY_LEVEL_CRITICAL 0
#define WPAD_BATTERY_LEVEL_LOW 1
#define WPAD_BATTERY_LEVEL_MAX 4

#define WPAD_SENSOR_BAR_POS_BOTTOM 0
#define WPAD_SENSOR_BAR_POS_TOP 1

#define WPAD_ERR_NONE 0
#define WPAD_ERR_NO_CONTROLLER -1
#define WPAD_ERR_BUSY -2
#define WPAD_ERR_TRANSFER -3
#define WPAD_ERR_CORRUPTED -7

#define WPAD_RX_DATASIZE 96
#define WPAD_COMMAND_CMD_MAX_LEN 24
#define WPAD_COMMAND_EXT_MAX_LEN 12

typedef struct DPDObject {
    s16 x;
    s16 y;
    u16 size;
    u8 traceId;
} DPDObject;

typedef struct WPADInfo {
    BOOL dpd;
    BOOL speaker;
    BOOL attach;
    BOOL lowBat;
    BOOL nearempty;
    u8 battery;
    u8 led;
    u8 protocol;
    u8 firmware;
} WPADInfo;

typedef struct WPADStatus {
    u16 button;
    s16 accX;
    s16 accY;
    s16 accZ;
    DPDObject obj[4];
    u8 dev;
    s8 err;
} WPADStatus;

typedef struct WPADFSStatus {
    u16 button;
    s16 accX;
    s16 accY;
    s16 accZ;
    DPDObject obj[4];
    u8 dev;
    s8 err;
    s16 fsAccX;
    s16 fsAccY;
    s16 fsAccZ;
    s8 fsStickX;
    s8 fsStickY;
} WPADFSStatus;

typedef struct WPADCLStatus {
    u16 button;
    s16 accX;
    s16 accY;
    s16 accZ;
    DPDObject obj[4];
    u8 dev;
    s8 err;
    u16 clButton;
    s16 clLStickX;
    s16 clLStickY;
    s16 clRStickX;
    s16 clRStickY;
    u8 clTriggerL;
    u8 clTriggerR;
} WPADCLStatus;

typedef struct WPADAcc {
    s16 x;
    s16 y;
    s16 z;
} WPADAcc;

typedef struct WPADMEMGameInfo {
    OSTime calendarTimeTick;
    u16 gameTitle[17];
    char gameId[4];
    u8 gameType;
    u8 checkSum;
    u8 reserved[8];
} WPADMEMGameInfo;

typedef struct WPADCommand {
    u32 command;
    u8 data[22];
    u16 len;
    void* readBuf;
    u16 readLen;
    u32 readAddr;
    WPADInfo* info;
    WPADCallback callback;
} WPADCommand;

typedef struct WPADCmdQueue {
    s8 head;
    s8 tail;
    WPADCommand* cmd;
    u32 cmdlen;
} WPADCmdQueue;

typedef struct WPADConfig {
    DPDObject obj[WPAD_DPD_MAX_OBJECTS];
    WPADAcc acc_0g;
    WPADAcc acc_1g;
    u8 motor;
    u8 volume;
} WPADConfig;

typedef struct WPADStick {
    s16 x;
    s16 x_min;
    s16 x_max;
    s16 y;
    s16 y_min;
    s16 y_max;
} WPADStick;

typedef struct WPADFsConfig {
    WPADStick stick;
    WPADAcc acc_0g;
    WPADAcc acc_1g;
} WPADFsConfig;

typedef struct WPADClConfig {
    WPADStick lstk;
    WPADStick rstk;
    u8 triggerL;
    u8 triggerR;
} WPADClConfig;

typedef struct WPADExtConfig {
    union {
        WPADFsConfig fs;
        WPADClConfig cl;
    };
} WPADExtConfig;

typedef struct WPADControlBlock {
    WPADMEMGameInfo gameInfo;
    s32 gameInfoErr[2];
    u8 prBuf[WPAD_RX_DATASIZE] ATTRIBUTE_ALIGN(32);
    u8 rxBuf[WPAD_RX_DATASIZE * 2] ATTRIBUTE_ALIGN(32);
    WPADCmdQueue cmdq;
    WPADCommand cmds[WPAD_COMMAND_CMD_MAX_LEN];
    WPADCmdQueue extq;
    WPADCommand exts[WPAD_COMMAND_EXT_MAX_LEN];
    WPADInfo infos;
    WPADInfo* infoBuf;
    WPADConfig conf;
    WPADExtConfig extConf;
    WPADCallback resultCallback;
    WPADExtensionCallback extensionCallback;
    WPADConnectCallback connectCallback;
    WPADSamplingCallback sampleCallback;
    void* samplingBufs_ptr;
    u32 bufLatestIndex;
    u32 bufLength;
    u32 dataFormat;
    s32 status;
    u8 getStatFlag;
    u8 devType;
    u8 devMode;
    s8 dev_handle;
    WPADCallback reqVolCb;
    u8 rxBufIndex;
    u8 reqVolume;
    u8 dpdDummyObjSize;
    u8 dpdFormat;
    u8 dpdCommand;
    u8 audioStop;
    u8 audioStopCnt;
    u8 audioFrames;
    BOOL rumble;
    BOOL motor;
    BOOL used;
    BOOL setup;
    BOOL oldFw;
    OSThreadQueue threadq;
    OSTime lastUpdateTime;
    u16 diffCountDpd;
    u16 hystCountDpd;
    u16 diffCountAcc;
    u16 hystCountAcc;
    u16 diffCountfsAcc;
    u16 hystCountfsAcc;
    OSTime cmdTimer;
    u8 cmdTimeoutAction;
    u8 isSetStickOrigin;
    u16 recalibrateCount;
    u8 key[16];
    u8 ft[8];
    u8 sb[8];
    u8 readBuf[64];
    void* readBufPtr;
    u32 readBaseAddr;
    s32 readError;
    u16 readLength;
    u8 keyIdx;
    u8 radioSense;
    u16 packetCnt;
    u8 disconnect;
    u8 cmdId;
    WPADCallback infoCallback;
    u8 infoLocked;
    u8 extEnc;
    u8 pwrSave;
    u8 padding2;
} WPADControlBlock;

typedef struct WPADMEMControlBlock {
    BOOL used;
    u8* p_buf;
    u16 len;
    u32 addr;
    WPADCallback callback;
} WPADMEMControlBlock;

/* The remote speaker's entry points (native/sdk/wpad/wpad_native.c); the controller is native input. */
BOOL WPADIsSpeakerEnabled(s32);
s32 WPADControlSpeaker(s32, u32, WPADCallback);
u8 WPADGetSpeakerVolume(void);
s32 WPADSendStreamData(s32, void*, u16);
BOOL WPADCanSendStreamData(s32);

#ifdef __cplusplus
}
#endif

#endif  // WPAD_H
