#include "Game/System/LumaTime.hpp"

#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/Scene/GameScene.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemRestartProcess.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/LumaServices.hpp"
#include "Game/System/NebulaCutsceneSkip.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include <luma.h>
#include <luma_service_provider.h>
#include <luma_services.h>
#include <luma_services_facts.h>
#include <luma_time.h>
#include <nebula_format.h>
#include <nebula_game_api.h>
#include <nebula_module.h>
#include <stddef.h>

extern "C" const NebulaHostServices* nebula_host(void);

// The time service (contract services-time). Everything here runs on the game thread except monotonic_ns, wall and
// soundRate, which read only the host's clocks and one atomic.
//
// No cost with no mod (services-time 8): beginFrame is an increment and one load (sAnyControl) while no mod has set a
// rate, a hold or steps; beginStep an increment and one load (sStepHandlers); the frame start's time hooks are set in
// LumaServices only while a mod has a timer or a frame handler; the sound engine reads one atomic.

namespace luma {
    namespace time {
        namespace {
            const u32 kMostClocks = LUMA_MAX_MODS * 2u;  // as the registry's records: mods in their grace keep theirs
            const u64 kNsPerSecond = 1000000000ull;
            // 2000-01-01 00:00 minus 1970-01-01 00:00, seconds: the calendar counts from 2000, UTC from 1970.
            const s64 kSeconds1970To2000 = 946684800ll;
            // The exact product of the mods' rates (services-time 4, G-T1): 32-bit limbs; each factor is below 2^14 and
            // there are at most kMostClocks of them.
            const u32 kRateLimbs = (kMostClocks * 14u) / 32u + 2u;

            struct Clock;

            // One timer: its node first, so the kernel's node is the timer.
            struct Timer {
                LumaEventNode node;
                LumaHandle handle;  // 0: a free slot
                Clock* owner;
                u32 clock;          // LUMA_TIME_FRAMES, STEPS, NANOSECONDS
                bool once;
                bool closed;        // cancelled, or given once: no call; the slot frees when the node is idle
                u64 every;
                u64 nextDue;        // on its clock
                u64 dueCount;       // times due since its last event
                LumaEventFn fn;
                void* user;
            };

            struct Clock {
                const LumaMod* mod;  // null: a free slot
                s32 order;           // its place in the Mods list (top 0)
                bool gone;           // after its mod_gone: nothing of it counts
                u32 rate;            // thousandths, 1000 at begin
                bool holds;
                u32 owed;            // steps asked for the next frame
                LumaTimeFn frameFn;
                void* frameUser;
                LumaTimeFn stepFn;
                void* stepUser;
                Timer* timers;       // LUMA_TIME_TIMERS_MOST of them, from the kernel's memory for the mod; null until the first
                u32 timersOpen;      // open (not closed) timers
            };

            const LumaServiceKernel* sKernel = nullptr;
            Clock sClocks[kMostClocks];
            u32 sClockHigh = 0;  // slots below it may be in use

            // Clocks with a frame or step handler, in list order (top first): the passes walk these only.
            Clock* sFrameOrder[kMostClocks];
            u32 sFrameOrderCount = 0;
            Clock* sStepOrder[kMostClocks];
            u32 sStepOrderCount = 0;
            u32 sTimerClocks = 0;  // clocks with an open timer or a timer whose node is queued

            // The world clock.
            bool sAnyControl = false;  // some mod has a rate other than 1000, a hold or owed steps
            u32 sWorldRate = LUMA_TIME_REAL;  // the clamped product (services-time 4)
            u32 sHolders = 0;
            u32 sAcc = 0;                    // thousandths of a step carried to the next frame
            u32 sStepsThisFrame = 1;
            u32 sStepInFrame = 0;
            u32 sSoundRate = LUMA_TIME_REAL;  // atomic: the audio clock's thread reads it
            bool sWorldSoundHeld = false;

            // The counts and the frame's clock readings.
            u64 sFrame = 0;
            u64 sStep = 0;
            bool sInUse = false;  // a mod has called time: from then on each frame reads the clock
            s64 sFrameStartNs = 0;
            s64 sFrameNs = 0;
            u64 sStateFrame = 0;  // the frame sState was found for (0: none)
            u32 sState = LUMA_TIME_BOOT;
            LumaHandle sNextHandle = 0;

            int64_t (*sUtcNs)() = nullptr;

            // ---- The table's functions' common checks (services-registry 4) ----

            Clock* findClock(const LumaMod* pMod) {
                for (u32 i = 0; i < sClockHigh; i++) {
                    if (sClocks[i].mod == pMod && pMod != nullptr) {
                        return &sClocks[i];
                    }
                }
                return nullptr;
            }

            void report(const LumaMod* pMod, const char* pWhere, u32 code, const char* pArg0, const char* pArg1,
                        const char* pArg2) {
                const char* args[3] = {pArg0, pArg1, pArg2};
                u32 count = 0;
                while (count < 3 && args[count] != nullptr) {
                    count++;
                }
                sKernel->report(pMod, pWhere, code, args, count);
            }

            // The mod known and live, then the game thread. LUMA_OK, or the status to return (reported as the rules say).
            s32 enter(const LumaMod* pMod, const char* pWhere, bool anyThread) {
                if (pMod == nullptr || sKernel->mod_id(pMod) == nullptr) {
                    sKernel->unknown_mod(pWhere);
                    return LUMA_E_ARGUMENT;
                }
                if (sKernel->mod_live(pMod) == 0) {
                    return LUMA_E_CANCELLED;
                }
                if (!anyThread && sKernel->game_thread() == 0) {
                    report(pMod, pWhere, 100u, nullptr, nullptr, nullptr);
                    return LUMA_E_THREAD;
                }
                return LUMA_OK;
            }

            // The least size of a struct (services-registry 3: the first minor's whole struct).
            s32 checkSize(const LumaMod* pMod, const char* pWhere, const char* pStruct, const void* p, u32 least) {
                if (p == nullptr) {
                    report(pMod, pWhere, 101u, "out", nullptr, nullptr);
                    return LUMA_E_ARGUMENT;
                }
                const u32 size = *static_cast<const u32*>(p);
                if (size < least) {
                    char found[16];
                    char expected[16];
                    nebula_snprintf(found, sizeof(found), "%u", size);
                    nebula_snprintf(expected, sizeof(expected), "%u", least);
                    report(pMod, pWhere, 103u, pStruct, found, expected);
                    return LUMA_E_ARGUMENT;
                }
                return LUMA_OK;
            }

            void countLimit(const LumaMod* pMod, const char* pWhere, u32 limit) {
                char text[16];
                nebula_snprintf(text, sizeof(text), "%u", limit);
                report(pMod, pWhere, 104u, text, nullptr, nullptr);
            }

            // ---- The world clock ----

            s64 monotonic() {
                const NebulaHostServices* const pHost = nebula_host();
                return static_cast<s64>(pHost->clock_ns(pHost->user));
            }

            // floor(r1 * ... * rk / 1000^(k-1)) over the mods whose rate is not 1000, clamped to 1..16000: the same in any
            // list order.
            u32 worldRate() {
                u32 limbs[kRateLimbs];
                u32 used = 1;
                limbs[0] = 1;
                u32 factors = 0;
                for (u32 i = 0; i < sClockHigh; i++) {
                    const Clock& rClock = sClocks[i];
                    if (rClock.mod == nullptr || rClock.gone || rClock.rate == LUMA_TIME_REAL) {
                        continue;
                    }
                    u64 carry = 0;
                    for (u32 l = 0; l < used; l++) {
                        const u64 value = static_cast<u64>(limbs[l]) * rClock.rate + carry;
                        limbs[l] = static_cast<u32>(value);
                        carry = value >> 32;
                    }
                    if (carry != 0) {
                        limbs[used++] = static_cast<u32>(carry);
                    }
                    factors++;
                }
                if (factors == 0) {
                    return LUMA_TIME_REAL;
                }
                for (u32 d = 1; d < factors; d++) {
                    u64 remainder = 0;
                    for (u32 l = used; l-- > 0;) {
                        const u64 value = (remainder << 32) | limbs[l];
                        limbs[l] = static_cast<u32>(value / LUMA_TIME_REAL);
                        remainder = value % LUMA_TIME_REAL;
                    }
                    while (used > 1 && limbs[used - 1] == 0) {
                        used--;
                    }
                }
                if (used > 1 || limbs[0] > LUMA_TIME_RATE_MOST) {
                    return LUMA_TIME_RATE_MOST;
                }
                return limbs[0] < LUMA_TIME_RATE_LEAST ? LUMA_TIME_RATE_LEAST : limbs[0];
            }

            // After any change of a rate, a hold or owed steps.
            void clockChanged() {
                sWorldRate = worldRate();
                u32 holders = 0;
                bool owed = false;
                for (u32 i = 0; i < sClockHigh; i++) {
                    const Clock& rClock = sClocks[i];
                    if (rClock.mod == nullptr || rClock.gone) {
                        continue;
                    }
                    holders += rClock.holds ? 1u : 0u;
                    owed = owed || rClock.owed != 0;
                }
                sHolders = holders;
                sAnyControl = sWorldRate != LUMA_TIME_REAL || holders != 0 || owed;
            }

            // What the game is doing (services-time 5), found once a frame when asked.
            u32 findState() {
                GameSystem* const pSystem = SingletonHolder< GameSystem >::get();
                GameSystemSceneController* const pController = pSystem != nullptr ? pSystem->mSceneController : nullptr;
                if (pController == nullptr || pController->mCurrSceneControlInfo.mScene == SceneKind_Boot ||
                    pController->mCurrSceneControlInfo.mScene == SceneKind_None) {
                    return LUMA_TIME_BOOT;
                }
                if (!pController->isNerveNormal()) {
                    return LUMA_TIME_LOADING;
                }
                if (MR::isActiveMoviePlayer()) {
                    return LUMA_TIME_MOVIE;
                }
                if (NebulaCutsceneSkip::isSkipping()) {
                    return LUMA_TIME_SKIP;
                }
                if (pSystem->mWasSceneHeld) {
                    return LUMA_TIME_MENU;
                }
                if (pController->mCurrSceneControlInfo.mScene == SceneKind_Game && pController->mScene != nullptr &&
                    static_cast< GameScene* >(pController->mScene)->isExecPauseMenuOrGalaxyMap()) {
                    return LUMA_TIME_MENU;
                }
                return LUMA_TIME_PLAY;
            }

            u32 state() {
                if (sStateFrame != sFrame) {
                    sState = findState();
                    sStateFrame = sFrame;
                }
                return sState;
            }

            // Before the first scene, loading, a movie, the skip: the world clock is 1000 (services-time 4, F-M2).
            bool clockSuspended(u32 s) {
                return s == LUMA_TIME_BOOT || s == LUMA_TIME_LOADING || s == LUMA_TIME_MOVIE || s == LUMA_TIME_SKIP;
            }

            // The game holds the scene itself (HOME Menu, settings menu, warning, skip question): as of the last update.
            bool gameHoldsScene() {
                GameSystem* const pSystem = SingletonHolder< GameSystem >::get();
                return pSystem != nullptr && pSystem->mWasSceneHeld;
            }

            void setSoundRate(u32 rate) {
                if (__atomic_load_n(&sSoundRate, __ATOMIC_RELAXED) != rate) {
                    __atomic_store_n(&sSoundRate, rate, __ATOMIC_RELAXED);
                }
                // The world's sound held (services-time 7): through AudSystem, composed with the menus' own holds.
                const bool held = rate == 0;
                if (held != sWorldSoundHeld) {
                    sWorldSoundHeld = held;
                    if (AudSystem* const pAudio = AudWrap::getSystem()) {
                        pAudio->holdForWorld(held);
                    }
                }
            }

            void fill(LumaTimeNow* pNow, u32 stepInFrame) {
                pNow->size = sizeof(LumaTimeNow);
                pNow->state = state();
                pNow->frame = sFrame;
                pNow->step = sStep;
                pNow->steps = sStepsThisFrame;
                pNow->step_in_frame = stepInFrame;
                pNow->rate = clockSuspended(pNow->state) ? LUMA_TIME_REAL : sWorldRate;
                pNow->holders = sHolders;
                pNow->sound_rate = __atomic_load_n(&sSoundRate, __ATOMIC_RELAXED);
                pNow->reserved = 0;
                pNow->frame_start_ns = sFrameStartNs;
                pNow->frame_ns = sFrameNs;
            }

            void markInUse() {
                if (!sInUse) {
                    sInUse = true;
                    sFrameStartNs = monotonic();
                    sFrameNs = 0;
                }
            }

            // ---- Handlers through the kernel's call (the guard names "time event 2" or "time event 3") ----

            struct HandlerCall {
                LumaTimeFn fn;
                void* user;
                const LumaTimeNow* now;
            };

            void handlerTrampoline(void* pUser, const LumaEvent* pEvent) {
                (void)pEvent;
                const HandlerCall* const pCall = static_cast<const HandlerCall*>(pUser);
                pCall->fn(pCall->user, pCall->now);
            }

            void callHandler(Clock* pClock, LumaTimeFn fn, void* pUser, const LumaTimeNow* pNow, u32 kind) {
                HandlerCall call = {fn, pUser, pNow};
                LumaEvent event = {};
                event.size = sizeof(LumaEvent);
                event.kind = kind;
                event.status = LUMA_OK;
                (void)sKernel->call(pClock->mod, LUMA_TIME_NAME, handlerTrampoline, &call, &event);
            }

            // Inserts or removes `pClock` in a pass's list, kept in list order.
            void listSet(Clock** ppList, u32* pCount, Clock* pClock, bool in) {
                u32 at = *pCount;
                for (u32 i = 0; i < *pCount; i++) {
                    if (ppList[i] == pClock) {
                        at = i;
                        break;
                    }
                }
                const bool present = at != *pCount;
                if (in && !present) {
                    u32 place = *pCount;
                    while (place > 0 && ppList[place - 1]->order > pClock->order) {
                        ppList[place] = ppList[place - 1];
                        place--;
                    }
                    ppList[place] = pClock;
                    (*pCount)++;
                } else if (!in && present) {
                    for (u32 i = at + 1; i < *pCount; i++) {
                        ppList[i - 1] = ppList[i];
                    }
                    (*pCount)--;
                }
            }

            void frameHooksChanged() {
                luma::services::setTimeHooks(sTimerClocks != 0 ? framePost : nullptr,
                                             sFrameOrderCount != 0 ? frameHandlers : nullptr);
            }

            // ---- Timers ----

            u64 clockNow(u32 clock) {
                switch (clock) {
                case LUMA_TIME_FRAMES:
                    return sFrame;
                case LUMA_TIME_STEPS:
                    return sStep;
                default:
                    return static_cast<u64>(sInUse ? sFrameStartNs : monotonic());
                }
            }

            // a + b, or the clock's end (never due) when it would not fit: a far `after` or `every` never wraps to now.
            u64 addSaturated(u64 a, u64 b) { return b > ~0ull - a ? ~0ull : a + b; }

            bool timerBusy(const Timer& rTimer) { return __atomic_load_n(&rTimer.node.state, __ATOMIC_ACQUIRE) != 0; }

            void freeTimer(Timer* pTimer) {
                Clock* const pClock = pTimer->owner;
                pTimer->handle = 0;
                pTimer->fn = nullptr;
                pTimer->user = nullptr;
                bool any = false;
                for (u32 i = 0; i < LUMA_TIME_TIMERS_MOST; i++) {
                    any = any || pClock->timers[i].handle != 0;
                }
                if (!any) {
                    sTimerClocks--;
                    frameHooksChanged();
                }
            }

            void closeTimer(Timer* pTimer) {
                if (pTimer->closed) {
                    return;
                }
                pTimer->closed = true;
                pTimer->owner->timersOpen--;
                if (!timerBusy(*pTimer)) {
                    freeTimer(pTimer);
                }
                // Queued: its deliver sees it closed, calls nothing and frees it (services-registry 4.8).
            }

            void timerDeliver(LumaEventNode* pNode) {
                Timer* const pTimer = reinterpret_cast<Timer*>(pNode);
                if (pTimer->closed) {
                    freeTimer(pTimer);
                    return;
                }
                LumaEvent event = {};
                event.size = sizeof(LumaEvent);
                event.kind = LUMA_TIME_EVENT_TIMER;
                event.status = LUMA_OK;
                event.handle = pTimer->handle;
                event.value = pTimer->dueCount;
                pTimer->dueCount = 0;
                const LumaMod* const pMod = pTimer->owner->mod;
                const LumaEventFn fn = pTimer->fn;
                void* const pUser = pTimer->user;
                if (pTimer->once) {
                    // Given once: closed before the call, so a cancel from inside the call finds no timer.
                    closeTimer(pTimer);
                }
                (void)sKernel->call(pMod, LUMA_TIME_NAME, fn, pUser, &event);
            }

            void timerDrop(LumaEventNode* pNode) {
                Timer* const pTimer = reinterpret_cast<Timer*>(pNode);
                pTimer->dueCount = 0;
                if (pTimer->closed) {
                    freeTimer(pTimer);
                }
            }

            Timer* timerOf(Clock* pClock, LumaHandle handle) {
                if (pClock->timers == nullptr || handle == 0) {
                    return nullptr;
                }
                for (u32 i = 0; i < LUMA_TIME_TIMERS_MOST; i++) {
                    Timer& rTimer = pClock->timers[i];
                    if (rTimer.handle == handle && !rTimer.closed) {
                        return &rTimer;
                    }
                }
                return nullptr;
            }

            // ---- The table ----

            int32_t svcNow(const LumaMod* pMod, LumaTimeNow* pOut) {
                const char* const pWhere = "time.now";
                if (const s32 status = enter(pMod, pWhere, false); status != LUMA_OK) {
                    return status;
                }
                if (const s32 status = checkSize(pMod, pWhere, "LumaTimeNow", pOut, sizeof(LumaTimeNow)); status != LUMA_OK) {
                    return status;
                }
                markInUse();
                const u32 size = pOut->size;
                fill(pOut, sStepsThisFrame);
                pOut->size = size;
                return LUMA_OK;
            }

            int64_t svcMonotonic(const LumaMod* pMod) {
                if (const s32 status = enter(pMod, "time.monotonic_ns", true); status != LUMA_OK) {
                    return status;
                }
                const s64 now = monotonic();
                return now < 1 ? 1 : now;
            }

            // Days since 1970-01-01 to a civil date (H. Hinnant's days_from_civil inverse).
            void civilOf(s64 days, s32* pYear, u32* pMonth, u32* pDay) {
                days += 719468;
                const s64 era = (days >= 0 ? days : days - 146096) / 146097;
                const u32 dayOfEra = static_cast<u32>(days - era * 146097);
                const u32 yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
                const u32 dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
                const u32 monthIndex = (5 * dayOfYear + 2) / 153;
                *pDay = dayOfYear - (153 * monthIndex + 2) / 5 + 1;
                *pMonth = monthIndex < 10 ? monthIndex + 3 : monthIndex - 9;
                *pYear = static_cast<s32>(static_cast<s64>(yearOfEra) + era * 400 + (*pMonth <= 2 ? 1 : 0));
            }

            int32_t svcWall(const LumaMod* pMod, LumaTimeWall* pOut) {
                const char* const pWhere = "time.wall";
                if (const s32 status = enter(pMod, pWhere, true); status != LUMA_OK) {
                    return status;
                }
                if (const s32 status = checkSize(pMod, pWhere, "LumaTimeWall", pOut, sizeof(LumaTimeWall)); status != LUMA_OK) {
                    return status;
                }
                const NebulaHostServices* const pHost = nebula_host();
                // The calendar: local time, nanoseconds from 2000-01-01 00:00 (the frames' clock in deterministic mode).
                const s64 localNs = static_cast<s64>(pHost->calendar_ns(pHost->user)) + kSeconds1970To2000 * static_cast<s64>(kNsPerSecond);
                s64 utcNs = localNs;
                s32 offset = 0;
                // Deterministic mode (test_frame_step set): UTC is the calendar, offset 0, so every run is the same.
                if (pHost->test_frame_step == nullptr && sUtcNs != nullptr) {
                    utcNs = sUtcNs();
                    // The local offset to the nearest minute (zones are whole quarters of an hour).
                    const s64 difference = localNs - utcNs;
                    const s64 minute = 60ll * static_cast<s64>(kNsPerSecond);
                    const s64 minutes = (difference >= 0 ? difference + minute / 2 : difference - minute / 2) / minute;
                    offset = static_cast<s32>(minutes * 60);
                }
                const s64 local = utcNs + static_cast<s64>(offset) * static_cast<s64>(kNsPerSecond);
                const s64 second = local >= 0 ? local / static_cast<s64>(kNsPerSecond) : (local + 1) / static_cast<s64>(kNsPerSecond) - 1;
                const s64 nanos = local - second * static_cast<s64>(kNsPerSecond);
                const s64 days = second >= 0 ? second / 86400 : (second + 1) / 86400 - 1;
                const s64 inDay = second - days * 86400;
                LumaTimeWall wall = {};
                wall.size = pOut->size;
                wall.utc_offset_s = offset;
                wall.utc_ns = utcNs;
                u32 month = 0;
                u32 day = 0;
                civilOf(days, &wall.year, &month, &day);
                wall.month = static_cast<u8>(month);
                wall.day = static_cast<u8>(day);
                wall.weekday = static_cast<u8>(days >= -4 ? (days + 4) % 7 : 6 - ((-days - 5) % 7));
                wall.hour = static_cast<u8>(inDay / 3600);
                wall.minute = static_cast<u8>(inDay / 60 % 60);
                wall.second = static_cast<u8>(inDay % 60);
                wall.millisecond = static_cast<u16>(nanos / 1000000);
                *pOut = wall;
                return LUMA_OK;
            }

            int32_t svcSetRate(const LumaMod* pMod, uint32_t rate) {
                const char* const pWhere = "time.set_rate";
                if (const s32 status = enter(pMod, pWhere, false); status != LUMA_OK) {
                    return status;
                }
                if (rate < LUMA_TIME_RATE_LEAST || rate > LUMA_TIME_RATE_MOST) {
                    report(pMod, pWhere, 101u, "rate", nullptr, nullptr);
                    return LUMA_E_ARGUMENT;
                }
                Clock* const pClock = findClock(pMod);
                if (pClock == nullptr || pClock->gone) {
                    return LUMA_E_CANCELLED;
                }
                markInUse();
                pClock->rate = rate;
                clockChanged();
                return LUMA_OK;
            }

            int32_t svcHold(const LumaMod* pMod) {
                if (const s32 status = enter(pMod, "time.hold", false); status != LUMA_OK) {
                    return status;
                }
                Clock* const pClock = findClock(pMod);
                if (pClock == nullptr || pClock->gone) {
                    return LUMA_E_CANCELLED;
                }
                markInUse();
                pClock->holds = true;
                clockChanged();
                return LUMA_OK;
            }

            int32_t svcRelease(const LumaMod* pMod) {
                if (const s32 status = enter(pMod, "time.release", false); status != LUMA_OK) {
                    return status;
                }
                Clock* const pClock = findClock(pMod);
                if (pClock == nullptr || pClock->gone) {
                    return LUMA_E_CANCELLED;
                }
                pClock->holds = false;
                pClock->owed = 0;
                clockChanged();
                return LUMA_OK;
            }

            int32_t svcStep(const LumaMod* pMod, uint32_t count) {
                const char* const pWhere = "time.step";
                if (const s32 status = enter(pMod, pWhere, false); status != LUMA_OK) {
                    return status;
                }
                Clock* const pClock = findClock(pMod);
                if (pClock == nullptr || pClock->gone) {
                    return LUMA_E_CANCELLED;
                }
                if (count == 0 || count > LUMA_TIME_STEPS_MOST) {
                    report(pMod, pWhere, 101u, "count", nullptr, nullptr);
                    return LUMA_E_ARGUMENT;
                }
                if (!pClock->holds) {
                    report(pMod, pWhere, 120u, nullptr, nullptr, nullptr);
                    return LUMA_E_ARGUMENT;
                }
                if (sHolders > 1 || clockSuspended(state())) {
                    return LUMA_E_BUSY;
                }
                if (pClock->owed + count > LUMA_TIME_STEPS_MOST) {
                    countLimit(pMod, pWhere, LUMA_TIME_STEPS_MOST);
                    return LUMA_E_LIMIT;
                }
                pClock->owed += count;
                clockChanged();
                return LUMA_OK;
            }

            int32_t svcTimer(const LumaMod* pMod, uint32_t clock, uint64_t after, uint64_t every, LumaEventFn fn, void* pUser,
                             LumaHandle* pOut) {
                const char* const pWhere = "time.timer";
                if (const s32 status = enter(pMod, pWhere, false); status != LUMA_OK) {
                    return status;
                }
                const char* pWrong = clock > LUMA_TIME_NANOSECONDS ? "clock"
                                     : after == 0                  ? "after"
                                     : fn == nullptr               ? "fn"
                                     : pOut == nullptr             ? "out"
                                                                   : nullptr;
                if (pWrong != nullptr) {
                    report(pMod, pWhere, 101u, pWrong, nullptr, nullptr);
                    return LUMA_E_ARGUMENT;
                }
                Clock* const pClock = findClock(pMod);
                if (pClock == nullptr || pClock->gone) {
                    return LUMA_E_CANCELLED;
                }
                if (pClock->timers == nullptr) {
                    pClock->timers = static_cast<Timer*>(sKernel->alloc(pMod, sizeof(Timer) * LUMA_TIME_TIMERS_MOST, alignof(Timer)));
                    if (pClock->timers == nullptr) {
                        return LUMA_E_MEMORY;
                    }
                    for (u32 i = 0; i < LUMA_TIME_TIMERS_MOST; i++) {
                        pClock->timers[i] = Timer();
                    }
                }
                Timer* pTimer = nullptr;
                bool any = false;
                for (u32 i = 0; i < LUMA_TIME_TIMERS_MOST; i++) {
                    Timer& rTimer = pClock->timers[i];
                    any = any || rTimer.handle != 0;
                    if (pTimer == nullptr && rTimer.handle == 0) {
                        pTimer = &rTimer;
                    }
                }
                if (pTimer == nullptr) {
                    // Every slot is open or closing (a closed timer's node still queued frees at its delivery).
                    countLimit(pMod, pWhere, LUMA_TIME_TIMERS_MOST);
                    return LUMA_E_LIMIT;
                }
                markInUse();
                *pTimer = Timer();
                pTimer->node.mod = pMod;
                pTimer->node.deliver = timerDeliver;
                pTimer->node.drop = timerDrop;
                pTimer->handle = ++sNextHandle;
                pTimer->owner = pClock;
                pTimer->clock = clock;
                pTimer->once = every == 0;
                pTimer->every = every;
                pTimer->nextDue = addSaturated(clockNow(clock), after);
                pTimer->fn = fn;
                pTimer->user = pUser;
                pClock->timersOpen++;
                if (!any) {
                    sTimerClocks++;
                    frameHooksChanged();
                }
                *pOut = pTimer->handle;
                return LUMA_PENDING;
            }

            int32_t svcCancel(const LumaMod* pMod, LumaHandle handle) {
                const char* const pWhere = "time.cancel";
                if (const s32 status = enter(pMod, pWhere, false); status != LUMA_OK) {
                    return status;
                }
                Clock* const pClock = findClock(pMod);
                Timer* const pTimer = pClock != nullptr ? timerOf(pClock, handle) : nullptr;
                if (pTimer == nullptr) {
                    report(pMod, pWhere, 102u, nullptr, nullptr, nullptr);
                    return LUMA_E_HANDLE;
                }
                closeTimer(pTimer);
                return LUMA_OK;
            }

            int32_t svcOnFrame(const LumaMod* pMod, LumaTimeFn fn, void* pUser) {
                if (const s32 status = enter(pMod, "time.on_frame", false); status != LUMA_OK) {
                    return status;
                }
                Clock* const pClock = findClock(pMod);
                if (pClock == nullptr || pClock->gone) {
                    return LUMA_E_CANCELLED;
                }
                markInUse();
                pClock->frameFn = fn;
                pClock->frameUser = pUser;
                listSet(sFrameOrder, &sFrameOrderCount, pClock, fn != nullptr);
                frameHooksChanged();
                return LUMA_OK;
            }

            int32_t svcOnStep(const LumaMod* pMod, LumaTimeFn fn, void* pUser) {
                if (const s32 status = enter(pMod, "time.on_step", false); status != LUMA_OK) {
                    return status;
                }
                Clock* const pClock = findClock(pMod);
                if (pClock == nullptr || pClock->gone) {
                    return LUMA_E_CANCELLED;
                }
                markInUse();
                pClock->stepFn = fn;
                pClock->stepUser = pUser;
                listSet(sStepOrder, &sStepOrderCount, pClock, fn != nullptr);
                return LUMA_OK;
            }

            const LumaTimeService cTable = {
                {sizeof(LumaTimeService), LUMA_TIME_MAJOR, LUMA_TIME_MINOR},
                svcNow,
                svcMonotonic,
                svcWall,
                svcSetRate,
                svcHold,
                svcRelease,
                svcStep,
                svcTimer,
                svcCancel,
                svcOnFrame,
                svcOnStep,
            };

            // ---- The provider ----

            void modBegin(void* pUser, const LumaMod* pMod) {
                (void)pUser;
                u32 slot = sClockHigh;
                for (u32 i = 0; i < sClockHigh; i++) {
                    if (sClocks[i].mod == nullptr) {
                        slot = i;
                        break;
                    }
                }
                if (slot == kMostClocks) {
                    NEBULA_PANIC("LumaCore: time has no clock left for the mod %s (%u)", sKernel->mod_id(pMod), kMostClocks);
                }
                if (slot == sClockHigh) {
                    sClockHigh++;
                }
                Clock& rClock = sClocks[slot];
                rClock = Clock();
                rClock.mod = pMod;
                rClock.order = sKernel->mod_order(pMod);
                rClock.rate = LUMA_TIME_REAL;
            }

            void clearControl(Clock* pClock) {
                pClock->rate = LUMA_TIME_REAL;
                pClock->holds = false;
                pClock->owed = 0;
            }

            void modGone(void* pUser, const LumaMod* pMod) {
                (void)pUser;
                Clock* const pClock = findClock(pMod);
                if (pClock == nullptr) {
                    return;
                }
                pClock->gone = true;
                clearControl(pClock);
                clockChanged();
                listSet(sFrameOrder, &sFrameOrderCount, pClock, false);
                listSet(sStepOrder, &sStepOrderCount, pClock, false);
                pClock->frameFn = nullptr;
                pClock->stepFn = nullptr;
                if (pClock->timers != nullptr) {
                    for (u32 i = 0; i < LUMA_TIME_TIMERS_MOST; i++) {
                        if (pClock->timers[i].handle != 0) {
                            closeTimer(&pClock->timers[i]);
                        }
                    }
                }
                frameHooksChanged();
            }

            void modEnd(void* pUser, const LumaMod* pMod) {
                (void)pUser;
                Clock* const pClock = findClock(pMod);
                if (pClock == nullptr) {
                    return;
                }
                // The registry dropped every queued node (their drop freed the closed timers) before mod_end.
                if (pClock->timers != nullptr) {
                    for (u32 i = 0; i < LUMA_TIME_TIMERS_MOST; i++) {
                        if (pClock->timers[i].handle != 0) {
                            freeTimer(&pClock->timers[i]);
                        }
                    }
                    sKernel->free(pMod, pClock->timers);
                }
                *pClock = Clock();
                while (sClockHigh > 0 && sClocks[sClockHigh - 1].mod == nullptr) {
                    sClockHigh--;
                }
            }

            void registered(void* pUser, const LumaServiceKernel* pKernel) {
                (void)pUser;
                sKernel = pKernel;
            }

            const LumaServiceRow cRows[] = {
                {120, "Time.NotHeld", {"where", nullptr, nullptr, nullptr}},
                {121, "Time.LetGo", {nullptr, nullptr, nullptr, nullptr}},
            };

            const LumaServiceProvider cProvider = {
                sizeof(LumaServiceProvider),
                LUMA_TIME_NAME,
                &cTable.base,
                nullptr,
                cRows,
                sizeof(cRows) / sizeof(cRows[0]),
                nullptr,
                modBegin,
                modGone,
                modEnd,
                registered,
            };

            Clock* clockOfId(const char* pId) {
                if (pId == nullptr || sKernel == nullptr) {
                    return nullptr;
                }
                for (u32 i = 0; i < sClockHigh; i++) {
                    Clock& rClock = sClocks[i];
                    if (rClock.mod == nullptr || rClock.gone) {
                        continue;
                    }
                    const char* pOwn = sKernel->mod_id(rClock.mod);
                    const char* pAsked = pId;
                    while (pOwn != nullptr && *pOwn != 0 && *pOwn == *pAsked) {
                        pOwn++;
                        pAsked++;
                    }
                    if (pOwn != nullptr && *pOwn == 0 && *pAsked == 0) {
                        return &rClock;
                    }
                }
                return nullptr;
            }
        }  // namespace

        u32 beginFrame() {
            sFrame++;
            sStepInFrame = 0;
            if (sInUse) {
                const s64 now = monotonic();
                sFrameNs = now - sFrameStartNs;
                sFrameStartNs = now;
            }
            if (!sAnyControl) {
                sStepsThisFrame = 1;
                if (sWorldSoundHeld || __atomic_load_n(&sSoundRate, __ATOMIC_RELAXED) != LUMA_TIME_REAL) {
                    setSoundRate(LUMA_TIME_REAL);
                }
                return 1;
            }
            const u32 s = state();
            u32 steps = 1;
            u32 sound = LUMA_TIME_REAL;
            if (clockSuspended(s)) {
                // Loading, a movie, the skip: the world clock is 1000 and steps asked are dropped (step said BUSY).
                bool dropped = false;
                for (u32 i = 0; i < sClockHigh; i++) {
                    dropped = dropped || sClocks[i].owed != 0;
                    sClocks[i].owed = 0;
                }
                if (dropped) {
                    clockChanged();
                }
            } else if (gameHoldsScene()) {
                // No step while the game holds the scene (F-M1); acc and owed steps wait for it to let go.
                steps = 0;
                sound = sHolders != 0 || sWorldRate < 125u || sWorldRate > 4000u ? 0u : sWorldRate;
            } else if (sHolders != 0) {
                u32 owed = 0;
                for (u32 i = 0; i < sClockHigh; i++) {
                    owed += sClocks[i].owed;
                    sClocks[i].owed = 0;
                }
                steps = owed < LUMA_TIME_STEPS_MOST ? owed : LUMA_TIME_STEPS_MOST;
                sound = 0;
                if (owed != 0) {
                    clockChanged();
                }
            } else {
                sAcc += sWorldRate;
                steps = sAcc / LUMA_TIME_REAL;
                sAcc -= steps * LUMA_TIME_REAL;
                if (steps > LUMA_TIME_STEPS_MOST) {
                    steps = LUMA_TIME_STEPS_MOST;
                }
                sound = sWorldRate < 125u || sWorldRate > 4000u ? 0u : sWorldRate;
            }
            sStepsThisFrame = steps;
            setSoundRate(sound);
            return steps;
        }

        bool isStepAllowed() { return sStepsThisFrame != 0; }

        bool canStepAgain() {
            GameSystem* const pSystem = SingletonHolder< GameSystem >::get();
            if (pSystem == nullptr || pSystem->mWasSceneHeld || pSystem->mSceneController == nullptr ||
                pSystem->mSceneController->isExistRequest() || SingletonHolder< GameSystemRestartProcess >::get()->isActive()) {
                return false;
            }
            // Found again, not this frame's cached state: the step before may have begun a movie or the skip.
            return !clockSuspended(findState());
        }

        void beginStep() {
            sStep++;
            const u32 stepInFrame = sStepInFrame++;
            if (sStepOrderCount == 0) {
                return;
            }
            LumaTimeNow now;
            fill(&now, stepInFrame);
            // A copy of the order: a handler's mod may be taken out during the pass.
            Clock* order[kMostClocks];
            const u32 count = sStepOrderCount;
            for (u32 i = 0; i < count; i++) {
                order[i] = sStepOrder[i];
            }
            for (u32 i = 0; i < count; i++) {
                Clock* const pClock = order[i];
                if (!pClock->gone && pClock->stepFn != nullptr) {
                    callHandler(pClock, pClock->stepFn, pClock->stepUser, &now, 3u);
                }
            }
        }

        u32 soundRate() { return __atomic_load_n(&sSoundRate, __ATOMIC_RELAXED); }

        void framePost() {
            for (u32 c = 0; c < sClockHigh; c++) {
                Clock& rClock = sClocks[c];
                if (rClock.mod == nullptr || rClock.gone || rClock.timersOpen == 0) {
                    continue;
                }
                for (u32 i = 0; i < LUMA_TIME_TIMERS_MOST; i++) {
                    Timer& rTimer = rClock.timers[i];
                    if (rTimer.handle == 0 || rTimer.closed) {
                        continue;
                    }
                    const u64 now = clockNow(rTimer.clock);
                    if (now < rTimer.nextDue) {
                        continue;
                    }
                    u64 times = 1;
                    if (rTimer.once) {
                        rTimer.nextDue = ~0ull;
                    } else {
                        times += (now - rTimer.nextDue) / rTimer.every;
                        // times * every <= now - nextDue + every: only the add can pass the clock's end.
                        rTimer.nextDue = addSaturated(rTimer.nextDue + (times - 1) * rTimer.every, rTimer.every);
                    }
                    rTimer.dueCount += times;
                    (void)sKernel->post(&rTimer.node);
                }
            }
        }

        void frameHandlers() {
            if (sFrameOrderCount == 0) {
                return;
            }
            LumaTimeNow now;
            fill(&now, sStepsThisFrame);
            Clock* order[kMostClocks];
            const u32 count = sFrameOrderCount;
            for (u32 i = 0; i < count; i++) {
                order[i] = sFrameOrder[i];
            }
            for (u32 i = 0; i < count; i++) {
                Clock* const pClock = order[i];
                if (!pClock->gone && pClock->frameFn != nullptr) {
                    callHandler(pClock, pClock->frameFn, pClock->frameUser, &now, 2u);
                }
            }
        }

        bool clockOf(const LumaMod* pMod, u32* pRate, bool* pHolds) {
            const Clock* const pClock = findClock(pMod);
            if (pClock == nullptr || pClock->gone) {
                return false;
            }
            *pRate = pClock->rate;
            *pHolds = pClock->holds;
            return true;
        }

        void letGo(const LumaMod* pMod) {
            Clock* const pClock = findClock(pMod);
            if (pClock == nullptr || pClock->gone) {
                return;
            }
            clearControl(pClock);
            clockChanged();
        }

        const LumaServiceProvider* provider() { return &cProvider; }

        void setUtcClock(int64_t (*pUtcNs)()) { sUtcNs = pUtcNs; }
    }
}

extern "C" {
// For the Mods page (J, plan 6.2; services-time 11), game thread, by mod id: 1 and the mod's rate and hold, or 0 when
// time does not know the mod.
NEBULA_GAME_API int32_t luma_time_clock_of(const char* id, uint32_t* rate, int32_t* holds) {
    luma::time::Clock* const pClock = luma::time::clockOfId(id);
    if (pClock == nullptr || rate == nullptr || holds == nullptr) {
        return 0;
    }
    *rate = pClock->rate;
    *holds = pClock->holds ? 1 : 0;
    return 1;
}

// The host's facts (luma_services_facts.h), once before the first mod: the UTC clock for time.wall.
NEBULA_GAME_API void luma_services_facts(const LumaServicesFacts* facts) {
    if (facts != nullptr && facts->size >= offsetof(LumaServicesFacts, utc_ns) + sizeof(facts->utc_ns)) {
        luma::time::setUtcClock(facts->utc_ns);
    }
}

// The player's "Let go" (services-time 1 item 10): the mod's rate, hold and owed steps cleared from the next frame.
// The page writes the mod log's Note Time.LetGo. 1: done; 0: time does not know the mod.
NEBULA_GAME_API int32_t luma_time_let_go(const char* id) {
    luma::time::Clock* const pClock = luma::time::clockOfId(id);
    if (pClock == nullptr) {
        return 0;
    }
    luma::time::clearControl(pClock);
    luma::time::clockChanged();
    return 1;
}
}
