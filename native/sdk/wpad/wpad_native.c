/* Wii Remotes natively (in place of WPAD, WUD and the Bluetooth stack). The host supplies each remote's input
   reports as the remote sends them (report id 0x30/0x31/0x33/0x37 with the extension bytes decrypted), and at
   connection the remote's EEPROM calibration and the extension's calibration block. This layer applies the SDK's own
   calibration (WPADHIDParser.c's getDevConfig/getExtConfig) and decodes reports exactly as the SDK's parser does,
   so KPAD and the game see the console's data. Reports arrive from host threads and are decoded on the simulation
   thread in the controller interrupt (SI), which then runs the sampling callback per report, as WPAD's receive path
   does. Results of asynchronous calls (DPD, speaker, info) are delivered from the same interrupt. */
#include <math.h>
#include <stdatomic.h>
#include <string.h>

#include <revolution/kpad.h>
#include <revolution/os.h>
#include <revolution/vi.h>
#include <revolution/wpad.h>

#include <nebula_game_api.h>

#include "nebula_sdk.h"

const NebulaHostServices* nebula_host(void);

enum { kChannels = WPAD_MAX_CONTROLLERS, kReportBytes = 22, kQueue = 32, kDeferred = 32 };

typedef struct {
    u8 bytes[kReportBytes];
    BOOL pointerExact;
    f32 pointerX, pointerY;
} Report;

typedef struct {
    /* Host side (any thread) to simulation thread. */
    Report queue[kQueue];
    _Atomic u32 head; /* next write (host) */
    _Atomic u32 tail; /* next read (simulation) */
    _Atomic int connectEvent; /* 1 connect, 2 disconnect, 0 none */
    _Atomic u32 shakeHeld;        /* nebula_game_wpad_shake's bits */
    _Atomic u32 shakePresses[2];  /* presses not yet taken: the remote's, the Nunchuk's */
    _Atomic u32 pendingBattery;   /* nebula_game_wpad_battery's level plus one; 0 none */
    u8 eeprom[0x2A];
    u8 extCalibration[32];
    u32 pendingDevType;
    WPADInfo pendingInfo;

    /* Simulation thread. */
    BOOL connected;
    u8 devType;
    u32 dataFormat;
    BOOL dpdEnabled;
    BOOL speakerEnabled;
    WPADAcc acc0g, acc1g, fsAcc0g, fsAcc1g;
    s8 stickX, stickY;
    BOOL stickOriginSet;
    f32 calibX, calibY, centerX, centerY, rolag;
    WPADInfo info;
    WPADCLStatus status; /* the largest status: WPADRead copies the part its format uses */
    u16 accSettle, stickSettle; /* reports an analog change has persisted (IsControllerDataChanged) */
    BOOL pointerExact; /* the sample being delivered carries the host's exact pointer */
    f32 pointerX, pointerY;
    WPADSamplingCallback samplingCallback;
    WPADConnectCallback connectCallback;
    WPADExtensionCallback extensionCallback;
} Channel;

static Channel sChannels[kChannels];

typedef struct {
    WPADCallback callback;
    s32 chan;
    s32 result;
} Deferred;

static Deferred sDeferred[kDeferred];
static u32 sDeferredCount;
static BOOL sInitialized;

/* The host's entry points, called from any thread. */
NEBULA_GAME_API void nebula_game_wpad_connect(uint32_t chan, int connected, const NebulaWiimoteConnection* connection) {
    if (chan >= kChannels) {
        return;
    }
    Channel* c = &sChannels[chan];
    if (connected && connection != NULL) {
        memcpy(c->eeprom, connection->eeprom, sizeof(c->eeprom));
        memcpy(c->extCalibration, connection->extension_calibration, sizeof(c->extCalibration));
        c->pendingDevType = connection->extension;
        memset(&c->pendingInfo, 0, sizeof(c->pendingInfo));
        c->pendingInfo.battery = connection->battery;
        c->pendingInfo.attach = connection->extension != WPAD_DEV_CORE;
    }
    atomic_store(&c->connectEvent, connected ? 1 : 2);
    nebula_raise_interrupt(__OS_INTERRUPT_PI_SI);
}

NEBULA_GAME_API void nebula_game_wpad_report(uint32_t chan, const uint8_t* report, size_t size,
                                             const NebulaWpadPointer* pointer) {
    if (chan >= kChannels || size == 0) {
        return;
    }
    Channel* c = &sChannels[chan];
    const u32 head = atomic_load_explicit(&c->head, memory_order_relaxed);
    if (head - atomic_load_explicit(&c->tail, memory_order_acquire) >= kQueue) {
        return; /* the simulation thread is a whole queue behind: the newest reports wait for room */
    }
    Report* r = &c->queue[head % kQueue];
    memset(r->bytes, 0, sizeof(r->bytes));
    memcpy(r->bytes, report, size < kReportBytes ? size : kReportBytes);
    r->pointerExact = pointer != NULL;
    r->pointerX = pointer != NULL ? pointer->x : 0.0f;
    r->pointerY = pointer != NULL ? pointer->y : 0.0f;
    atomic_store_explicit(&c->head, head + 1, memory_order_release);
    nebula_raise_interrupt(__OS_INTERRUPT_PI_SI);
}

NEBULA_GAME_API void nebula_game_wpad_shake(uint32_t chan, uint32_t held) {
    if (chan >= kChannels) {
        return;
    }
    Channel* c = &sChannels[chan];
    const u32 previous = atomic_exchange(&c->shakeHeld, held);
    for (u32 device = 0; device < 2; device++) {
        if ((held & ~previous) & (1u << device)) {
            atomic_fetch_add(&c->shakePresses[device], 1);
        }
    }
}

NEBULA_GAME_API void nebula_game_wpad_battery(uint32_t chan, uint32_t level) {
    if (chan >= kChannels) {
        return;
    }
    atomic_store(&sChannels[chan].pendingBattery, (level > 4u ? 4u : level) + 1u);
    nebula_raise_interrupt(__OS_INTERRUPT_PI_SI);
}

/* Before the game reads its remotes each frame (WPadHolder): the host's newest input in a report now, decoded here
   (the controller interrupt runs as interrupts are enabled again) so KPADRead has it this frame. */
void __NebulaWPADSample(void) {
    nebula_host()->wpad_sample(nebula_host()->user, 0);
    OSRestoreInterrupts(OSDisableInterrupts());
}

/* The shake presses on the remote (device 0) or its Nunchuk (1) since the last call (WPadHVSwing). */
u32 __NebulaWPADTakeShakePresses(s32 chan, s32 device) {
    if (chan < 0 || chan >= kChannels || device < 0 || device > 1) {
        return 0;
    }
    return atomic_exchange(&sChannels[chan].shakePresses[device], 0);
}

/* Calibration as WPADHIDParser.c's getDevConfig and getExtConfig apply it. */
static const s16 kDummyObjX[] = {127, 896, 896, 127};
static const s16 kDummyObjY[] = {93, 93, 674, 674};

static void apply_device_calibration(Channel* c) {
    const u8* p = c->eeprom;
    DPDObject conf[WPAD_DPD_MAX_OBJECTS];
    for (int i = 0; i < WPAD_DPD_MAX_OBJECTS; i++) {
        conf[i].x = kDummyObjX[i];
        conf[i].y = kDummyObjY[i];
    }
    c->acc0g.x = c->acc0g.y = c->acc0g.z = 530;
    c->acc1g.x = c->acc1g.y = c->acc1g.z = 636;

    for (int block = 0; block < 2; block++) {
        const int index = block * 11;
        u8 sum = 0;
        for (int j = index; j < index + 10; j++) {
            sum = (u8)(sum + p[j]);
        }
        if ((u8)(sum + 0x55) != p[index + 10]) {
            continue;
        }
        DPDObject obj[WPAD_DPD_MAX_OBJECTS];
        obj[0].x = (s16)(p[index] | ((p[index + 2] & 0x30) << 4));
        obj[0].y = (s16)(WPAD_DPD_IMG_RESO_WY - 1 - (p[index + 1] | ((p[index + 2] & 0xC0) << 2)));
        obj[1].x = (s16)(p[index + 3] | ((p[index + 2] & 0x03) << 8));
        obj[1].y = (s16)(WPAD_DPD_IMG_RESO_WY - 1 - (p[index + 4] | ((p[index + 2] & 0x0C) << 6)));
        obj[2].x = (s16)(p[index + 5] | ((p[index + 7] & 0x30) << 4));
        obj[2].y = (s16)(WPAD_DPD_IMG_RESO_WY - 1 - (p[index + 6] | ((p[index + 7] & 0xC0) << 2)));
        obj[3].x = (s16)(p[index + 8] | ((p[index + 7] & 0x03) << 8));
        obj[3].y = (s16)(WPAD_DPD_IMG_RESO_WY - 1 - (p[index + 9] | ((p[index + 7] & 0x0C) << 6)));
        for (int j = 0; j < WPAD_DPD_MAX_OBJECTS; j++) {
            const BOOL left = obj[j].x < WPAD_DPD_IMG_RESO_WX / 2, top = obj[j].y < WPAD_DPD_IMG_RESO_WY / 2;
            const BOOL right = obj[j].x > WPAD_DPD_IMG_RESO_WX / 2, bottom = obj[j].y > WPAD_DPD_IMG_RESO_WY / 2;
            if (left && top) {
                conf[0] = obj[j];
            }
            if (right && top) {
                conf[1] = obj[j];
            }
            if (right && bottom) {
                conf[2] = obj[j];
            }
            if (left && bottom) {
                conf[3] = obj[j];
            }
        }
        break;
    }

    f32 x[WPAD_DPD_MAX_OBJECTS], y[WPAD_DPD_MAX_OBJECTS];
    f32 difaveX = 0.0f, difaveY = 0.0f;
    for (int i = 0; i < WPAD_DPD_MAX_OBJECTS; i++) {
        x[i] = (f32)conf[i].x;
        y[i] = (f32)conf[i].y;
        difaveX += conf[i].x - kDummyObjX[i];
        difaveY += conf[i].y - kDummyObjY[i];
    }
    difaveX /= 4.0f;
    difaveY /= 4.0f;
    const f32 deltaX = (f32)atan((126.5f + difaveX) / 1332.5f) - (f32)atan(126.5f / 1332.5f);
    const f32 deltaY = (f32)atan((93.0f + difaveY) / 1337.4f) - (f32)atan(93.0f / 1337.4f);
    c->calibX = (f32)(-1.0f * (f32)tan(deltaX)) * 1332.5f;
    c->calibY = (f32)(-1.0f * (f32)tan(deltaY)) * 1337.4f;
    c->centerX = c->centerY = 0.0f;
    for (int i = 0; i < WPAD_DPD_MAX_OBJECTS; i++) {
        x[i] += c->calibX;
        y[i] += c->calibY;
        c->centerX += x[i];
        c->centerY += y[i];
    }
    c->centerX /= 4.0f;
    c->centerY /= 4.0f;
    c->rolag = 0.0f;
    for (int i = 0; i < WPAD_DPD_MAX_OBJECTS; i++) {
        const f32 ag = (f32)atan((y[i] - c->centerY) / (x[i] - c->centerX));
        const f32 agOrg = (f32)atan(((f32)kDummyObjY[i] - 383.5f) / ((f32)kDummyObjX[i] - 511.5f));
        c->rolag += ag - agOrg;
    }
    c->rolag /= 4.0f;

    for (int block = 0; block < 2; block++) {
        const int index = 22 + block * 10;
        u8 sum = 0;
        for (int j = index; j < index + 9; j++) {
            sum = (u8)(sum + p[j]);
        }
        if ((u8)(sum + 0x55) != p[index + 9]) {
            continue;
        }
        c->acc0g.x = (s16)((p[index + 0] << 2) | ((p[index + 3] >> 4) & 3));
        c->acc0g.y = (s16)((p[index + 1] << 2) | ((p[index + 3] >> 2) & 3));
        c->acc0g.z = (s16)((p[index + 2] << 2) | (p[index + 3] & 3));
        c->acc1g.x = (s16)((p[index + 4] << 2) | ((p[index + 7] >> 4) & 3));
        c->acc1g.y = (s16)((p[index + 5] << 2) | ((p[index + 7] >> 2) & 3));
        c->acc1g.z = (s16)((p[index + 6] << 2) | (p[index + 7] & 3));
        break;
    }
}

static void apply_extension_calibration(Channel* c) {
    const u8* p = c->extCalibration;
    int index = -1;
    for (int block = 0; block < 2; block++) {
        const int k = block * 16;
        int sum = 0;
        for (int j = k; j < k + 14; j++) {
            sum += p[j];
        }
        if ((u8)(sum + 0x55) == p[k + 14] && (u8)(sum + 0xAA) == p[k + 15]) {
            index = k;
        }
    }
    if (index < 0) {
        c->fsAcc0g.x = c->fsAcc0g.y = c->fsAcc0g.z = 512;
        c->fsAcc1g.x = c->fsAcc1g.y = c->fsAcc1g.z = 716;
    } else {
        c->fsAcc0g.x = (s16)((p[index + 0] << 2) | ((p[index + 3] >> 4) & 3));
        c->fsAcc0g.y = (s16)((p[index + 1] << 2) | ((p[index + 3] >> 2) & 3));
        c->fsAcc0g.z = (s16)((p[index + 2] << 2) | (p[index + 3] & 3));
        c->fsAcc1g.x = (s16)((p[index + 4] << 2) | ((p[index + 7] >> 4) & 3));
        c->fsAcc1g.y = (s16)((p[index + 5] << 2) | ((p[index + 7] >> 2) & 3));
        c->fsAcc1g.z = (s16)((p[index + 6] << 2) | (p[index + 7] & 3));
    }
    c->stickOriginSet = FALSE;
}

/* Report decoding as WPADHIDParser.c's __a1_3x_data_type and __parse_dpd_data do it. */
static void parse_dpd(Channel* c, WPADStatus* s, int fmt, const u8* d, int len) {
    if (fmt == 3) {
        for (int i = 0; i < WPAD_DPD_MAX_OBJECTS; i++) {
            DPDObject* o = &s->obj[i];
            if (i * 3 + 2 < len) {
                const u8 x = d[i * 3], y = d[i * 3 + 1], xy = d[i * 3 + 2];
                o->x = (s16)(x | ((xy & 0x30) << 4));
                o->y = (s16)(WPAD_DPD_IMG_RESO_WY - 1 - (y | ((xy & 0xC0) << 2)));
                o->size = (u16)(xy & 0xF);
                o->size = (u8)((f32)o->size * (f32)o->size * 3.141592653589793f);
                if (o->size == 0 || o->x == WPAD_DPD_IMG_RESO_WX - 1 || o->y == WPAD_DPD_IMG_RESO_WY - 1) {
                    o->x = 0;
                    o->y = WPAD_DPD_IMG_RESO_WY - 1;
                    o->size = 0;
                }
            } else {
                o->x = 0;
                o->y = WPAD_DPD_IMG_RESO_WY - 1;
                o->size = 0;
            }
            o->traceId = (u8)i;
        }
    } else {
        for (int i = 0; i < WPAD_DPD_MAX_OBJECTS; i++) {
            DPDObject* o = &s->obj[i];
            const u8 x = d[i * 3 - (i / 2)], y = d[i * 3 - (i / 2) + 1], xy = d[(i / 2) * 5 + 2];
            if ((i % 2) == 0) {
                o->x = (s16)(x | ((xy & 0x30) << 4));
                o->y = (s16)(WPAD_DPD_IMG_RESO_WY - 1 - (y | ((xy & 0xC0) << 2)));
            } else {
                o->x = (s16)(x | ((xy & 0x03) << 8));
                o->y = (s16)(WPAD_DPD_IMG_RESO_WY - 1 - (y | ((xy & 0x0C) << 6)));
            }
            if (o->x == WPAD_DPD_IMG_RESO_WX - 1 || o->y == WPAD_DPD_IMG_RESO_WY - 1) {
                o->x = 0;
                o->y = WPAD_DPD_IMG_RESO_WY - 1;
                o->size = 0;
            } else {
                o->size = 12;
            }
            o->traceId = (u8)i;
        }
    }
    for (int i = 0; i < WPAD_DPD_MAX_OBJECTS; i++) {
        DPDObject* o = &s->obj[i];
        if (o->x != 0 || o->y != WPAD_DPD_IMG_RESO_WY - 1) {
            const f32 a = (f32)o->x + c->calibX - c->centerX;
            const f32 b = (f32)o->y + c->calibY - c->centerY;
            o->x = (s16)((f32)(a * (f32)cos(-1.0f * c->rolag)) - (f32)(b * (f32)sin(-1.0f * c->rolag)) + c->centerX);
            o->y = (s16)((f32)(a * (f32)sin(-1.0f * c->rolag)) + (f32)(b * (f32)cos(-1.0f * c->rolag)) + c->centerY);
        }
    }
}

/* d is the report from its id: d[1..2] buttons, d[3..5] accelerometer, then IR and extension bytes. */
static void decode(Channel* c, const u8* d) {
    WPADCLStatus full;
    memset(&full, 0, sizeof(full));
    WPADStatus* s = (WPADStatus*)&full;
    WPADFSStatus* fs = (WPADFSStatus*)&full;
    const u8 id = d[0];
    s->button = (u16)((d[1] | (d[2] << 8)) & 0x9F1F);
    s->err = WPAD_ERR_NONE;
    s->dev = c->devType;
    if (id != 0x30) {
        s->accX = (s16)(((d[3] << 2) | ((d[1] >> 5) & 3)) - c->acc0g.x);
        s->accY = (s16)(((d[4] << 2) | ((d[2] >> 4) & 2)) - c->acc0g.y);
        s->accZ = (s16)(((d[5] << 2) | ((d[2] >> 5) & 2)) - c->acc0g.z);
    }
    if (id == 0x33) {
        parse_dpd(c, s, 3, d + 6, 12);
    } else if (id == 0x37) {
        parse_dpd(c, s, 1, d + 6, 10);
        const u8* e = d + 16;
        if (c->info.attach && c->devType == WPAD_DEV_FREESTYLE) {
            fs->fsStickX = (s8)e[0];
            fs->fsStickY = (s8)e[1];
            fs->fsAccX = (s16)(((e[2] << 2) | ((e[5] >> 2) & 3)) - c->fsAcc0g.x);
            fs->fsAccY = (s16)(((e[3] << 2) | ((e[5] >> 4) & 3)) - c->fsAcc0g.y);
            fs->fsAccZ = (s16)(((e[4] << 2) | ((e[5] >> 6) & 3)) - c->fsAcc0g.z);
            fs->button |= (u16)(((u16)(~e[5]) << 13) & 0x6000);
            if (!c->stickOriginSet) {
                c->stickOriginSet = TRUE;
                c->stickX = fs->fsStickX;
                c->stickY = fs->fsStickY;
            }
            const s16 x = (s16)((u8)fs->fsStickX - (u8)c->stickX);
            const s16 y = (s16)((u8)fs->fsStickY - (u8)c->stickY);
            fs->fsStickX = (s8)(x < -128 ? -128 : x > 127 ? 127 : x);
            fs->fsStickY = (s8)(y < -128 ? -128 : y > 127 ? 127 : y);
        }
    } else {
        for (int i = 0; i < WPAD_DPD_MAX_OBJECTS; i++) {
            s->obj[i].x = 0;
            s->obj[i].y = WPAD_DPD_IMG_RESO_WY - 1;
            s->obj[i].traceId = (u8)i;
        }
    }
    /* Input keeps the screen from dimming (WPADiCheckContInputs' IsControllerDataChanged): a button at once, the
       accelerometer moving more than 12 and the Nunchuk stick more than 2 once the change has held for a few
       reports. */
    const WPADStatus* old = (const WPADStatus*)&c->status;
    BOOL changed = s->button != old->button;
    const int ax = s->accX - old->accX, ay = s->accY - old->accY, az = s->accZ - old->accZ;
    if (ax > 12 || ax < -12 || ay > 12 || ay < -12 || az > 12 || az < -12) {
        changed |= ++c->accSettle > 3;
    } else {
        c->accSettle = 0;
    }
    if (id == 0x37 && c->devType == WPAD_DEV_FREESTYLE) {
        const WPADFSStatus* oldFs = (const WPADFSStatus*)&c->status;
        const int sx = fs->fsStickX - oldFs->fsStickX, sy = fs->fsStickY - oldFs->fsStickY;
        if (sx > 2 || sx < -2 || sy > 2 || sy < -2) {
            changed |= ++c->stickSettle > 3;
        } else {
            c->stickSettle = 0;
        }
    }
    if (changed) {
        c->accSettle = 0;
        c->stickSettle = 0;
        __VIResetRFIdle();
    }
    c->status = full;
}

static void defer(WPADCallback callback, s32 chan, s32 result) {
    if (callback == NULL) {
        return;
    }
    const BOOL enabled = OSDisableInterrupts();
    if (sDeferredCount < kDeferred) {
        sDeferred[sDeferredCount++] = (Deferred){callback, chan, result};
    }
    OSRestoreInterrupts(enabled);
    nebula_raise_interrupt(__OS_INTERRUPT_PI_SI);
}

static void controller_interrupt(__OSInterrupt interrupt, OSContext* context) {
    (void)interrupt;
    (void)context;
    for (s32 chan = 0; chan < kChannels; chan++) {
        Channel* c = &sChannels[chan];
        const int event = atomic_exchange(&c->connectEvent, 0);
        if (event == 1) {
            const BOOL wasConnected = c->connected;
            c->connected = TRUE;
            c->info = c->pendingInfo;
            apply_device_calibration(c);
            c->devType = WPAD_DEV_CORE;
            __VIResetRFIdle();
            if (!wasConnected && c->connectCallback != NULL) {
                c->connectCallback(chan, WPAD_ERR_NONE);
            }
            if (c->pendingDevType != WPAD_DEV_CORE) {
                apply_extension_calibration(c);
                if (c->extensionCallback != NULL) {
                    c->extensionCallback(chan, WPAD_DEV_UNKNOWN);
                }
                c->devType = (u8)c->pendingDevType;
            }
            if (c->extensionCallback != NULL) {
                c->extensionCallback(chan, c->devType);
            }
        } else if (event == 2 && c->connected) {
            c->connected = FALSE;
            c->devType = WPAD_DEV_NOT_FOUND;
            if (c->connectCallback != NULL) {
                c->connectCallback(chan, WPAD_ERR_NO_CONTROLLER);
            }
        }
        const u32 battery = atomic_exchange(&c->pendingBattery, 0);
        if (battery != 0 && c->connected) {
            c->info.battery = (u8)(battery - 1u);
        }
        if (c->connected) {
            u32 tail = atomic_load_explicit(&c->tail, memory_order_relaxed);
            const u32 head = atomic_load_explicit(&c->head, memory_order_acquire);
            for (; tail != head; tail++) {
                const Report* r = &c->queue[tail % kQueue];
                decode(c, r->bytes);
                c->pointerExact = r->pointerExact;
                c->pointerX = r->pointerX;
                c->pointerY = r->pointerY;
                atomic_store_explicit(&c->tail, tail + 1, memory_order_release);
                if (c->samplingCallback != NULL) {
                    c->samplingCallback(chan);
                }
            }
        }
    }
    while (sDeferredCount != 0) {
        const Deferred d = sDeferred[0];
        sDeferredCount--;
        memmove(&sDeferred[0], &sDeferred[1], sDeferredCount * sizeof(Deferred));
        d.callback(d.chan, d.result);
    }
}

/* KPAD's sampling callback, per sample: the host's exact pointer for it (KPAD.c). */
BOOL __NebulaWPADExactPointer(s32 chan, Vec2* pos) {
    const Channel* c = &sChannels[chan];
    if (!c->pointerExact) {
        return FALSE;
    }
    pos->x = c->pointerX;
    pos->y = c->pointerY;
    return TRUE;
}

void WPADInit(void) {
    if (sInitialized) {
        return;
    }
    sInitialized = TRUE;
    for (int chan = 0; chan < kChannels; chan++) {
        sChannels[chan].devType = WPAD_DEV_NOT_FOUND;
        sChannels[chan].dataFormat = WPAD_FMT_CORE;
    }
    __OSSetInterruptHandler(__OS_INTERRUPT_PI_SI, controller_interrupt);
    __OSUnmaskInterrupts(OS_INTERRUPTMASK_PI_SI);
    nebula_host()->wpad_control(nebula_host()->user, 0, NEBULA_WPAD_INIT, 0);
}

s32 WPADGetStatus(void) {
    return sInitialized ? WPAD_STATE_SETUP : WPAD_STATE_DISABLED;
}

s32 WPADProbe(s32 chan, u32* type) {
    const Channel* c = &sChannels[chan];
    if (!c->connected) {
        if (type != NULL) {
            *type = WPAD_DEV_NOT_FOUND;
        }
        return WPAD_ERR_NO_CONTROLLER;
    }
    if (type != NULL) {
        *type = c->devType;
    }
    return WPAD_ERR_NONE;
}

void WPADRead(s32 chan, void* status) {
    const Channel* c = &sChannels[chan];
    size_t size = sizeof(WPADStatus);
    switch (c->dataFormat) {
    case WPAD_FMT_FREESTYLE:
    case WPAD_FMT_FREESTYLE_ACC:
    case WPAD_FMT_FREESTYLE_ACC_DPD:
        size = sizeof(WPADFSStatus);
        break;
    case WPAD_FMT_CLASSIC:
    case WPAD_FMT_CLASSIC_ACC:
    case WPAD_FMT_CLASSIC_ACC_DPD:
        size = sizeof(WPADCLStatus);
        break;
    default:
        break;
    }
    const BOOL enabled = OSDisableInterrupts();
    memcpy(status, &c->status, size);
    if (!c->connected) {
        ((WPADStatus*)status)->err = WPAD_ERR_NO_CONTROLLER;
    }
    OSRestoreInterrupts(enabled);
}

u32 WPADGetDataFormat(s32 chan) {
    return sChannels[chan].dataFormat;
}

s32 WPADSetDataFormat(s32 chan, u32 format) {
    sChannels[chan].dataFormat = format;
    nebula_host()->wpad_control(nebula_host()->user, (uint32_t)chan, NEBULA_WPAD_FORMAT, format);
    return WPAD_ERR_NONE;
}

WPADSamplingCallback WPADSetSamplingCallback(s32 chan, WPADSamplingCallback callback) {
    const WPADSamplingCallback previous = sChannels[chan].samplingCallback;
    sChannels[chan].samplingCallback = callback;
    return previous;
}

WPADConnectCallback WPADSetConnectCallback(s32 chan, WPADConnectCallback callback) {
    const WPADConnectCallback previous = sChannels[chan].connectCallback;
    sChannels[chan].connectCallback = callback;
    return previous;
}

WPADExtensionCallback WPADSetExtensionCallback(s32 chan, WPADExtensionCallback callback) {
    const WPADExtensionCallback previous = sChannels[chan].extensionCallback;
    sChannels[chan].extensionCallback = callback;
    return previous;
}

void WPADGetAccGravityUnit(s32 chan, u32 type, WPADAcc* acc) {
    const Channel* c = &sChannels[chan];
    if (acc == NULL) {
        return;
    }
    if (type == WPAD_DEV_CORE) {
        acc->x = (s16)(c->acc1g.x - c->acc0g.x);
        acc->y = (s16)(c->acc1g.y - c->acc0g.y);
        acc->z = (s16)(c->acc1g.z - c->acc0g.z);
    } else if (type == WPAD_DEV_FREESTYLE) {
        acc->x = (s16)(c->fsAcc1g.x - c->fsAcc0g.x);
        acc->y = (s16)(c->fsAcc1g.y - c->fsAcc0g.y);
        acc->z = (s16)(c->fsAcc1g.z - c->fsAcc0g.z);
    }
}

s32 WPADControlDpd(s32 chan, u32 command, WPADCallback callback) {
    sChannels[chan].dpdEnabled = command != 0;
    nebula_host()->wpad_control(nebula_host()->user, (uint32_t)chan, NEBULA_WPAD_DPD, command);
    defer(callback, chan, sChannels[chan].connected ? WPAD_ERR_NONE : WPAD_ERR_NO_CONTROLLER);
    return WPAD_ERR_NONE;
}

BOOL WPADIsDpdEnabled(s32 chan) {
    return sChannels[chan].dpdEnabled;
}

void WPADControlMotor(s32 chan, u32 command) {
    nebula_host()->wpad_control(nebula_host()->user, (uint32_t)chan, NEBULA_WPAD_MOTOR, command);
}

s32 WPADControlSpeaker(s32 chan, u32 command, WPADCallback callback) {
    sChannels[chan].speakerEnabled = command != 0;
    nebula_host()->wpad_control(nebula_host()->user, (uint32_t)chan, NEBULA_WPAD_SPEAKER, command);
    defer(callback, chan, sChannels[chan].connected ? WPAD_ERR_NONE : WPAD_ERR_NO_CONTROLLER);
    return WPAD_ERR_NONE;
}

BOOL WPADIsSpeakerEnabled(s32 chan) {
    return sChannels[chan].speakerEnabled;
}

BOOL WPADCanSendStreamData(s32 chan) {
    return sChannels[chan].connected && sChannels[chan].speakerEnabled;
}

s32 WPADSendStreamData(s32 chan, void* data, u16 length) {
    nebula_host()->wpad_speaker_data(nebula_host()->user, (uint32_t)chan, (const uint8_t*)data, length);
    return WPAD_ERR_NONE;
}

u8 WPADGetSpeakerVolume(void) {
    return (u8)nebula_host()->sc_get(nebula_host()->user, NEBULA_SC_SPEAKER_VOLUME);
}

u8 WPADGetSensorBarPosition(void) {
    return (u8)nebula_host()->sc_get(nebula_host()->user, NEBULA_SC_SENSOR_BAR_POSITION);
}

u8 WPADGetDpdSensitivity(void) {
    return (u8)nebula_host()->sc_get(nebula_host()->user, NEBULA_SC_DPD_SENSITIVITY);
}

s32 WPADGetInfoAsync(s32 chan, WPADInfo* info, WPADCallback callback) {
    const Channel* c = &sChannels[chan];
    if (!c->connected) {
        return WPAD_ERR_NO_CONTROLLER;
    }
    WPADInfo current = c->info;
    current.dpd = c->dpdEnabled;
    current.speaker = c->speakerEnabled;
    *info = current;
    defer(callback, chan, WPAD_ERR_NONE);
    return WPAD_ERR_NONE;
}

/* A virtual remote's Mii area is blank. */
s32 WPADReadFaceData(s32 chan, void* buffer, u32 length, u32 address, WPADCallback callback) {
    (void)address;
    if (!sChannels[chan].connected) {
        return WPAD_ERR_NO_CONTROLLER;
    }
    memset(buffer, 0, length);
    defer(callback, chan, WPAD_ERR_NONE);
    return WPAD_ERR_NONE;
}

void WPADDisconnect(s32 chan) {
    nebula_host()->wpad_control(nebula_host()->user, (uint32_t)chan, NEBULA_WPAD_DISCONNECT, 0);
}

void WPADSetAutoSleepTime(u8 minutes) {
    (void)minutes;
}

u32 WPADGetWorkMemorySize(void) {
    return 32;
}

void WPADRegisterAllocator(WPADAlloc alloc, WPADFree free) {
    (void)alloc;
    (void)free;
}
