#include "Game/System/LumaDrawNative.hpp"

#include "Game/Render/FrameList.hpp"
#include "Game/System/LumaHook.hpp"
#include "Game/System/LumaModLog.hpp"
#include "Game/System/NebulaHost.hpp"
#include <luma_draw_native.h>
#include <luma_modlog.h>
#include <luma_services.h>
#include <nebula_format.h>
#include <nebula_frame_list.h>

// The draw.native service (docs/modding/reference/services/draw.md). The calls, the provider and point() run on the game
// thread; trampoline() runs on the host's render thread.
//
// A function is a slot of one table across mods, never moved, so a pass names it by its slot and the slot's generation
// then (the pass's user word). The host calls the trampoline when it reaches the pass, for every picture it draws from
// the list, maybe frames after the game recorded it. The trampoline counts itself in the slot's `calling`, then checks the
// generation: a slot the game changed since (removed, replaced, its mod stopped) is skipped. The game thread retires a
// slot by moving its generation on, and wipes it (or gives it to another function) only once it sees `calling` at
// zero after that (sequentially consistent on both sides: either the trampoline sees the new generation, or the game
// sees it calling), so a mod's function never starts after its mod's end, and its slot never changes under a call. The
// game thread never waits for a call: the trampoline is counted as a call reading a hook list is (CodeHold), so a mod
// taken out while its function runs keeps its library and memory until the call has returned (M-013).
//
// No cost with no mod: point() is one load while no function is set.

namespace luma {
    namespace draw {
        namespace native {
            namespace {
                const u32 kMost = LUMA_DRAW_NATIVE_MOST;
                const u32 kFactFrames = 120;

                struct Fn {
                    const LumaMod* mod;  // null: free
                    u32 point;
                    LumaDrawNativeFn fn;
                    void* user;
                    u32 generation;      // moved on at every change (atomic)
                    u32 calling;         // the render thread's calls running now (atomic)
                    u32 facts;
                    bool retired;        // its generation moved on, a call may still run: wiped when none does
                };

                const LumaServiceKernel* sKernel = nullptr;
                Fn sFns[kMost];
                u32 sSet = 0;  // slots in use

                void report(const LumaMod* pMod, const char* pWhere, u32 code, const char* pArg0) {
                    const char* args[1] = {pArg0};
                    sKernel->report(pMod, pWhere, code, args, pArg0 != nullptr ? 1u : 0u);
                }

                s32 badArgument(const LumaMod* pMod, const char* pWhere, const char* pWhat) {
                    report(pMod, pWhere, 101u, pWhat);
                    return LUMA_E_ARGUMENT;
                }

                s32 enter(const LumaMod* pMod, const char* pWhere) {
                    if (pMod == nullptr || sKernel->mod_id(pMod) == nullptr) {
                        sKernel->unknown_mod(pWhere);
                        return LUMA_E_ARGUMENT;
                    }
                    if (sKernel->mod_live(pMod) == 0) {
                        return LUMA_E_CANCELLED;
                    }
                    if (sKernel->game_thread() == 0) {
                        report(pMod, pWhere, 100u, nullptr);
                        return LUMA_E_THREAD;
                    }
                    return LUMA_OK;
                }

                void wipe(Fn& rFn) {
                    rFn.mod = nullptr;
                    rFn.fn = nullptr;
                    rFn.user = nullptr;
                    rFn.point = 0;
                    rFn.facts = 0;
                    rFn.retired = false;
                    sSet--;
                }

                // No call of the slot's function starts after this; one running goes on (its mod's library stays until
                // it returns), and the slot is wiped when the game thread sees none running.
                void clear(Fn& rFn) {
                    __atomic_add_fetch(&rFn.generation, 1u, __ATOMIC_SEQ_CST);
                    if (__atomic_load_n(&rFn.calling, __ATOMIC_SEQ_CST) == 0u) {
                        wipe(rFn);
                    } else {
                        rFn.retired = true;
                    }
                }

                // Retired slots no call runs in any more: free again.
                void sweep() {
                    for (u32 i = 0; i < kMost; i++) {
                        if (sFns[i].retired && __atomic_load_n(&sFns[i].calling, __ATOMIC_SEQ_CST) == 0u) {
                            wipe(sFns[i]);
                        }
                    }
                }

                // The render thread: the slot and generation from the pass's user word.
                void trampoline(void* pToken, const NebulaFrame* pFrame) {
                    const uintptr_t token = reinterpret_cast< uintptr_t >(pToken);
                    const u32 slot = static_cast< u32 >(token & 0xFFu);
                    const u32 generation = static_cast< u32 >(token >> 8);
                    if (slot >= kMost) {
                        return;
                    }
                    Fn& rFn = sFns[slot];
                    // Counted before the generation is read: a take-out after this point keeps the mod's library until
                    // the call has left.
                    const CodeHold hold;
                    __atomic_add_fetch(&rFn.calling, 1u, __ATOMIC_SEQ_CST);
                    if ((__atomic_load_n(&rFn.generation, __ATOMIC_SEQ_CST) & 0xFFFFFFu) == generation && rFn.fn != nullptr) {
                        rFn.fn(rFn.user, pFrame);
                    }
                    __atomic_sub_fetch(&rFn.calling, 1u, __ATOMIC_SEQ_CST);
                }

                bool nativePoint(u32 point) {
                    return point == LUMA_DRAW_WORLD_BEFORE || point == LUMA_DRAW_WORLD_OPAQUE_AFTER ||
                           point == LUMA_DRAW_WORLD_AFTER || point == LUMA_DRAW_PRESENT;
                }

                // ---- Calls ----

                int32_t svcAt(const LumaMod* pMod, LumaDrawPoint point, LumaDrawNativeFn fn, void* pUser) {
                    const char* const pWhere = "draw.native.at";
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    const u32 p = static_cast< u32 >(point);
                    if (!nativePoint(p)) {
                        return badArgument(pMod, pWhere, "point");
                    }
                    // A function in the mod's code (M-029): anything else would be called where no guard can resume.
                    if (fn != nullptr && sKernel->mod_code(pMod, reinterpret_cast<const void*>(fn)) != 1) {
                        return badArgument(pMod, pWhere, "fn");
                    }
                    sweep();
                    Fn* pOwn = nullptr;
                    Fn* pFree = nullptr;
                    for (u32 i = 0; i < kMost; i++) {
                        if (sFns[i].retired) {
                            continue;
                        }
                        if (sFns[i].mod == pMod && sFns[i].point == p) {
                            pOwn = &sFns[i];
                        } else if (sFns[i].mod == nullptr && pFree == nullptr) {
                            pFree = &sFns[i];
                        }
                    }
                    if (fn == nullptr) {
                        if (pOwn != nullptr) {
                            clear(*pOwn);
                        }
                        return LUMA_OK;
                    }
                    // A replaced function: its slot is retired (a call of it may still run) and the new one takes a free
                    // slot, so no slot changes under a call.
                    if (pFree == nullptr) {
                        char most[16];
                        nebula_snprintf(most, sizeof(most), "%u", kMost);
                        report(pMod, pWhere, 104u, most);
                        return LUMA_E_LIMIT;
                    }
                    if (pOwn != nullptr) {
                        clear(*pOwn);
                    }
                    Fn* const pSlot = pFree;
                    sSet++;
                    pSlot->mod = pMod;
                    pSlot->point = p;
                    pSlot->fn = fn;
                    pSlot->user = pUser;
                    pSlot->facts = 0;
                    return LUMA_OK;
                }

                int32_t svcBackend(const LumaMod* pMod, uint32_t* pOut) {
                    const char* const pWhere = "draw.native.backend";
                    if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                        return status;
                    }
                    if (luma_value_out(sKernel, pMod, pOut, uint32_t(NebulaHost::option(NEBULA_OPTION_GRAPHICS_BACKEND))) != LUMA_OK) {
                        return badArgument(pMod, pWhere, "out");
                    }
                    return LUMA_OK;
                }

                const LumaDrawNativeService cTable = {
                    {sizeof(LumaDrawNativeService), LUMA_DRAW_NATIVE_MAJOR, LUMA_DRAW_NATIVE_MINOR},
                    svcAt,
                    svcBackend,
                };

                // ---- The provider ----

                // After the mod's stop: none of its functions starts again; one running goes on, and the mod's library
                // and memory go only after it returned (the grace counts it).
                void modGone(void* pUser, const LumaMod* pMod) {
                    (void)pUser;
                    sweep();
                    for (u32 i = 0; i < kMost; i++) {
                        if (!sFns[i].retired && sFns[i].mod == pMod) {
                            clear(sFns[i]);
                        }
                    }
                }

                void registered(void* pUser, const LumaServiceKernel* pKernel) {
                    (void)pUser;
                    sKernel = pKernel;
                }

                // No rows of its own: the registry's (100 to 104). The permission `native` is the catalog's.
                const LumaServiceProvider cProvider = {
                    sizeof(LumaServiceProvider),
                    LUMA_DRAW_NATIVE_NAME,
                    &cTable.base,
                    nullptr,
                    nullptr,
                    0,
                    nullptr,
                    nullptr,
                    modGone,
                    nullptr,
                    registered,
                };
            }  // namespace

            void point(u32 point) {
                if (sSet == 0) {
                    return;
                }
                const bool facts = luma::modlog::facts();
                for (u32 i = 0; i < kMost; i++) {
                    Fn& rFn = sFns[i];
                    if (rFn.mod == nullptr || rFn.retired || rFn.point != point || sKernel->mod_live(rFn.mod) == 0) {
                        continue;
                    }
                    const u32 generation = __atomic_load_n(&rFn.generation, __ATOMIC_SEQ_CST);
                    // The slot in the low byte, the generation above (24 bits kept: a pass is never older than the lists
                    // the host keeps, far fewer than 2^24 changes of one slot).
                    const uintptr_t token = static_cast< uintptr_t >(i) | static_cast< uintptr_t >(generation & 0xFFFFFFu) << 8;
                    Frame::native(point, trampoline, reinterpret_cast< void* >(token));
                    if (facts && rFn.facts < kFactFrames) {
                        rFn.facts++;
                        char key[40];
                        nebula_snprintf(key, sizeof(key), "draw.native.%u", point);
                        char mod[80];
                        luma::modlog::jsonString(mod, sizeof(mod), sKernel->mod_id(rFn.mod));
                        char json[120];
                        nebula_snprintf(json, sizeof(json), "{\"mod\":%s,\"point\":%u}", mod, point);
                        luma::modlog::fact(key, json);
                    }
                }
            }

            const LumaServiceProvider* provider() {
                return &cProvider;
            }
        }  // namespace native
    }  // namespace draw
}  // namespace luma
