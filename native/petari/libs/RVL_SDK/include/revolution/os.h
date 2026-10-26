#ifndef OS_H
#define OS_H

#include <cstdarg>
#include "revolution/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OSHalt(msg) OSPanic(__FILE__, __LINE__, msg)

#ifdef __MWERKS__
u32 __OSBusClock : (0x8000 << 16 | 0x00F8);
u32 __MEM2End : (0x8000 << 16 | 0x3128);
#elif !defined(TARGET_PC)
u32 __OSBusClock = 0x800000F8;
u32 __MEM2End = 0x80003128;
#endif

// The console reads its bus clock from low memory; natively it is the Wii's constant (243 MHz).
#ifndef TARGET_PC
#define OS_BUS_CLOCK __OSBusClock
#endif
#ifndef OS_CORE_CLOCK
#define OS_CORE_CLOCK 729000000u
#endif
#ifndef OS_BUS_CLOCK
#define OS_BUS_CLOCK 243000000u
#endif
#ifndef OS_TIMER_CLOCK
#define OS_TIMER_CLOCK (OS_BUS_CLOCK / 4)
#endif

#define OSNanosecondsToTicks(nsec) (((nsec) * (OS_TIMER_CLOCK / 125000)) / 8000)

#define OSTicksToSeconds(ticks) ((ticks) / OS_TIMER_CLOCK)
#define OSTicksToMilliseconds(ticks) ((ticks) / (OS_TIMER_CLOCK / 1000))
#define OSTicksToMicroseconds(ticks) (((ticks)*8) / (OS_TIMER_CLOCK / 125000))
#define OSSecondsToTicks(sec) ((sec)*OS_TIMER_CLOCK)
#define OSMillisecondsToTicks(msec) ((msec) * (OS_TIMER_CLOCK / 1000))
#define OSMicrosecondsToTicks(usec) (((usec) * (OS_TIMER_CLOCK / 125000)) / 8)

void* OSPhysicalToUncached(u32);

#define OS_CACHED_REGION_PREFIX 0x8000
#define OS_UNCACHED_REGION_PREFIX 0xC000

#define OS_BASE_CACHED (OS_CACHED_REGION_PREFIX << 16)
#define OS_BASE_UNCACHED (OS_UNCACHED_REGION_PREFIX << 16)

#ifdef TARGET_PC
// Natively a physical address is the 32-bit address the GX FIFO and DMA see: an offset in the game's memory
// arena (the platform layer maps it both ways).
#ifdef __cplusplus
extern "C" {
#endif
u32 __NebulaToPhysical(const void* addr);
// Game state the host's controls follow (NEBULA_CONTEXT_* in nebula_game_api.h).
void __NebulaGameContext(u32 what, s32 value);
// Tests: what a hidden run asks of the game this scene frame (NebulaTestAction in nebula_game_api.h).
int __NebulaTestSceneFrame(void* action);
// The picture's shape the game draws for (NebulaDisplay in nebula_game_api.h), latched at each retrace.
const struct NebulaDisplay* __NebulaDisplay(void);
// The grey of the borders around the picture for the scene being drawn (0 black, 1 white).
void __NebulaSceneSurround(f32 intensity);
// A Nebula option (NEBULA_OPTION_* in nebula_game_api.h).
u32 __NebulaOption(u32 what);
// A light of the game for the renderer (NEBULA_LIGHT_* and game_light in nebula_game_api.h).
void __NebulaGameLight(u32 kind, u32 id, const f32* values);
// The stage being played, before each 3D scene (game_stage in nebula_game_api.h).
void __NebulaGameStage(const char* name);
// What the player is doing, for Discord (NebulaHostServices::presence).
void __NebulaPresence(const u16* galaxy, const u16* star, s32 power_stars);
// An achievement the save data meets: 1 when newly unlocked, its name in `name` (NebulaHostServices::achievement).
s32 __NebulaAchievement(const char* id, u16* name, u32 capacity);
s32 __NebulaAchievementDate(const char* id);
void __NebulaStatAdd(const char* name, u64 delta);
u64 __NebulaStatGet(const char* name);
// Nebula's settings menu (NebulaHostServices::settings_view and settings_input).
struct NebulaSettingsView;
void __NebulaSettingsView(struct NebulaSettingsView* view);
void __NebulaSettingsInput(u32 action, s32 arg);
// The 2D layer at the window's resolution while a Nebula screen with small text shows (sharp_2d).
void __NebulaSharp2D(u32 on);
// The button icons the game shows now (button_icons in nebula_game_api.h).
struct NebulaButtonIcons;
void __NebulaButtonIcons(struct NebulaButtonIcons* icons);
// Nebula's own pictures for GALAXY_EXPORT_ART (art_export in nebula_game_api.h); NULL asks whether one is on.
struct NebulaArtExport;
u32 __NebulaArtExport(const struct NebulaArtExport* art);
// The 2D layer's scale over the Wii's 640 x 456 (layer_2d_scale in nebula_game_api.h); 0 before the first frame.
u32 __NebulaLayer2DScale(void);
// The mouse wheel's turn since the last call, in 120ths of a notch, up positive (pointer_wheel in nebula_game_api.h).
s32 __NebulaPointerWheel(void);
// The host's HOME Menu as the game draws it (NEBULA_HBM_VIEW, NEBULA_HBM_POINT, NEBULA_HBM_CHOOSE).
s32 __NebulaHomeMenu(u32 what, u32 value);
// A game object's name for the renderer (object_name in nebula_game_api.h).
void __NebulaObjectName(const void* pObject, const char* pName);
// How far a model's traced light reaches (light_reach in nebula_game_api.h).
f32 __NebulaLightReach(const char* pModel, f32 radius);
// The traced sun's world direction (traced_sun in nebula_game_api.h): 1, or 0 without one.
int __NebulaTracedSun(f32* pDirection);
// The settings' galaxy select (galaxy_select_stage, galaxy_select_names_wanted, galaxy_select_name in
// nebula_game_api.h): its galaxy `index` (1, or 0 past the last), whether it waits for names, a name in the game's words.
int __NebulaGalaxySelectStage(u32 index, char* pId, u32 capacity, u32* pStars);
int __NebulaGalaxySelectNamesWanted(void);
void __NebulaGalaxySelectName(u32 index, u32 star, const u16* pName);
// A loaded model's triangles for its curved surfaces (curved_surface in nebula_game_api.h).
void __NebulaCurvedSurface(u32 arrayAddress, const void* pArray, u32 arrayBytes, const f32* pPositions,
                           u32 positionCount, const f32* pNormals, u32 normalCount, const u16* pCorners,
                           u32 triangleCount);
// A native equivalent of a mod's code patch (NEBULA_PATCH_* in nebula_game_api.h): 1 with `pValue` set (when not
// null) while an enabled mod carries it, else 0.
int __NebulaCodePatch(u32 id, u32* pValue);
// main.dol's version of a table the game compiles in (comet schedules, event flags): `pCompiled` while they match,
// else the DOL's, its strings read from main.dol (a mod's memory patches over main.dol's data change it).
const void* __NebulaDolTable(u32 kind, const void* pCompiled);
// A THP movie's opening (frame -2), each frame it draws and its close (-1) (movie_frame in nebula_game_api.h).
void __NebulaMovieFrame(const char* pName, s32 frame, f32 frameRate, const void* pY, const void* pU, const void* pV,
                        u32 width, u32 height);
// A wait on another thread's work (a busy loop the console's interrupts preempt): runs what is pending, or sleeps until
// a host thread raises something.
void __NebulaPoll(void);
void* __NebulaFromPhysical(u32 paddr);
// A scene bound in the GX stream (NEBULA_SCENE_* in nebula_game_api.h; the 3D scene's begin with the camera's
// world-to-eye matrix and the player's up as a fourth row). The begins ask whether the renderer wants host markers
// (__NebulaGXHostMarker).
void __NebulaGXSceneMarker(u32 marker, const f32 (*view)[4]);
// A JPA stripe particle whose two vertices are written next (gx_stripe_particle in nebula_game_api.h).
void __NebulaGXStripeParticle(u32 particle, u32 ageLifetime, u8 strip);
// A stable 32-bit name for a game object: its physical address (OSCachedToPhysical) when in the game's memory.
u32 __NebulaObjectId(const void* pObject);
// The draw-sync token whose callback runs (0 after it): GXPeek reads the EFB as the stream reached it.
void __NebulaGXPeekToken(u16 token);
#ifdef __cplusplus
}
#endif
#define OSPhysicalToCached(paddr) __NebulaFromPhysical((u32)(paddr))
#define OSCachedToPhysical(caddr) __NebulaToPhysical((const void*)(caddr))
#define OSUncachedToPhysical(ucaddr) __NebulaToPhysical((const void*)(ucaddr))
#define OSPhysicalToUncached(paddr) __NebulaFromPhysical((u32)(paddr))
// The address the console's game saw for a pointer into game memory (its cached-region address), where the game derives
// a value from an address.
#define NEBULA_CONSOLE_ADDRESS(caddr) (__NebulaToPhysical((const void*)(caddr)) + OS_BASE_CACHED)
#else
#define OSPhysicalToCached(paddr) ((void*)((u32)(paddr) + OS_BASE_CACHED))
#define OSCachedToPhysical(caddr) ((u32)((u8*)(caddr)-OS_BASE_CACHED))
#define OSUncachedToPhysical(ucaddr) ((u32)((u32)(ucaddr) & (~OS_BASE_UNCACHED)))
#define OSPhysicalToUncached(paddr) ((void*)((u32)(paddr) + OS_BASE_UNCACHED))
#endif

#ifndef TARGET_PC
// Console address tests; host addresses carry no MEM1/MEM2 bits (native/tools/console_address_audit.py).
#define OSIsMEM1Region(addr) (((u32)(addr)&0x30000000) == 0x00000000)
#define OSIsMEM2Region(addr) (((u32)(addr)&0x30000000) == 0x10000000)
#endif

#ifdef TARGET_PC
#define OSRoundUp32B(x) (((uintptr_t)(x) + 32 - 1) & ~(uintptr_t)(32 - 1))
#define OSRoundDown32B(x) (((uintptr_t)(x)) & ~(uintptr_t)(32 - 1))
#else
#define OSRoundUp32B(x) (((u32)(x) + 32 - 1) & ~(32 - 1))
#define OSRoundDown32B(x) (((u32)(x)) & ~(32 - 1))
#endif
#define OSDiffTick(tick1, tick0) ((s32)(tick1) - (s32)(tick0))

#define OS_TICKS_DELTA(x, y) ((s32)x - (s32)y)

u32 OSGetSoundMode(void);
void OSSetSoundMode(u32 mode);

u32 OSGetConsoleType(void);

void OSRegisterVersion(const char*);

void OSReport(const char*, ...) ATTRIBUTE_PRINTF(1, 2);
void OSVReport(const char*, va_list);
void OSPanic(const char*, int, const char*, ...) ATTRIBUTE_PRINTF(3, 4);

void* OSGetArenaHi(void);
void* OSGetArenaLo(void);

void OSSetArenaHi(void*);
void OSSetArenaLo(void*);

void OSSetMEM2ArenaHi(void*);

void* OSGetMEM1ArenaLo(void);
void* OSGetMEM1ArenaHi(void);
void* OSGetMEM2ArenaLo(void);
void* OSGetMEM2ArenaHi(void);

void* OSAllocFromMEM1ArenaLo(u32, u32);

u32 OSGetPhysicalMem2Size(void);

void OSProtectRange(u32, void*, u32, u32);

void* OSAllocFromHeap(int, u32);

typedef struct OSIOSRev {
    u8 reserved;
    u8 major;
    u8 minor;
    u8 micro;
    u8 month;
    u8 date;
    u16 year;
} OSIOSRev;

const u8 OSGetAppType(void);

char* OSUTF8to32(const char*, u32*);
u16* OSUTF16to32(const u16*, u32*);
u8 OSUTF32toANSI(u32);
u16 OSUTF32toSJIS(u32);

extern void __RAS_OSDisableInterrupts_begin(void);
extern void __RAS_OSDisableInterrupts_end(void);

extern s64 __OSStartTime;

const char* OSGetAppGamename(void);

void* OSAllocFromArenaLo(u32 size, u32 align);
void* OSAllocFromArenaHi(u32 size, u32 align);

#define OSAssert_Line(line_, exp_)

#ifndef ASSERT
#define ASSERT(exp) ((void)0)
#endif

#ifndef ASSERTMSG
#define ASSERTMSG(exp, msg) ((void)0)
#endif

inline s16 __OSf32tos16(__REGISTER f32 inF) {
    __REGISTER s16 out;
    u32 tmp;
    __REGISTER u32* tmpPtr = &tmp;
    // clang-format off
#ifdef TARGET_PC
    if (inF != inF || inF >= 32767.0f) {
        out = 32767;
    } else if (inF <= -32768.0f) {
        out = -32768;
    } else {
        out = (s16)inF;
    }
#elif defined(__MWERKS__)
    asm {
        psq_st inF, 0(tmpPtr), 0x1, 5
        lha out, 0(tmpPtr)
    }
#endif
    // clang-format on

    return out;
}

inline void OSf32tos16(f32* f, s16* out) {
    *out = __OSf32tos16(*f);
}

inline u8 __OSf32tou8(__REGISTER f32 inF) {
    __REGISTER u8 out;
    u32 tmp;
    __REGISTER u32* tmpPtr = &tmp;
    // clang-format off
#ifdef TARGET_PC
    if (inF != inF || inF >= 255.0f) {
        out = 255;
    } else if (inF > 0.0f) {
        out = (u8)inF;
    } else {
        out = 0;
    }
#elif defined(__MWERKS__)
    asm {
        psq_st inF, 0(tmpPtr), 0x1, 2
        lbz out, 0(tmpPtr)
    }
#endif
    // clang-format on

    return out;
}

#ifdef OS_F32_TO_U8_DEFER_INLINE
static void OSf32tou8(f32* f, u8* out);
#else
inline void OSf32tou8(f32* f, u8* out) {
    *out = __OSf32tou8(*f);
}
#endif

#include "revolution/base/PPCArch.h"
#include "revolution/gx.h"
#include "revolution/os/OSAlarm.h"
#include "revolution/os/OSAlloc.h"
#include "revolution/os/OSCache.h"
#include "revolution/os/OSContext.h"
#include "revolution/os/OSException.h"
#include "revolution/os/OSExecParams.h"
#include "revolution/os/OSFastCast.h"
#include "revolution/os/OSFont.h"
#include "revolution/os/OSInterrupt.h"
#include "revolution/os/OSMessage.h"
#include "revolution/os/OSMutex.h"
#include "revolution/os/OSPlayRecord.h"
#include "revolution/os/OSPlayTime.h"
#include "revolution/os/OSReset.h"
#include "revolution/os/OSResetSW.h"
#include "revolution/os/OSRtc.h"
#include "revolution/os/OSStateFlags.h"
#include "revolution/os/OSStateTM.h"
#include "revolution/os/OSThread.h"
#include "revolution/os/OSTime.h"

/* PRIVATE OS FUNCTIONS */
void __OSInitSystemCall(void);
void __OSUnhandledException(__OSException, OSContext*, u32, u32);
void __OSReschedule(void);
void __OSInitAlarm(void);
void __OSModuleInit(void);
void __OSInterruptInit(void);
void __OSContextInit(void);
void __OSInitAudioSystem(void);
void __OSInitMemoryProtection(void);
void __OSGetIOSRev(OSIOSRev*);
int __OSInitSTM(void);
void __OSInitNet(void);
void __OSPromoteThread(OSThread*, OSPriority);

void OSFatal(GXColor, GXColor, const char*);

#ifdef __cplusplus
}
#endif

#endif  // OS_H
