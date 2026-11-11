#pragma once

// LumaCore's functions and hooks. Every function the game defines has a row (luma_hook.h, written into it by
// tools/lumacore/instrument.py): its name, its signature, a way to call it and a flag. A hook sets the flag, and the
// function then hands each call to luma::run.

#include <luma_hook.h>
#include <nebula_types.h>

namespace luma {
    // One call of a hooked function, as its hooks see it.
    struct Call {
        const Function* function;
        void** args;        // args[0]: `this`, or null for a function without one; args[1..]: pointers to the arguments
        void* result;       // where the result is, or goes; null for a constructor or destructor
        u32 next;           // the runner's own
        u32 ran;            // the runner's own: the function's own code ran
        const void* chain;  // the runner's own: the hooks this call runs, fixed when it starts
    };

    enum HookKind : u32 {
        Hook_Before = 0,  // runs first; returning non-zero skips the function (and must have written the result)
        Hook_After,       // runs last, in reverse order; may change the result
        Hook_Wrap,        // runs around the function: it calls proceed() when it wants the rest to run
        Hook_Replace,     // a wrap only one mod may hold on a function
        Hook_KindCount
    };
    typedef s32 (*HookCallback)(Call* pCall, void* pUser);

    enum FindResult : s32 {
        Find_Found = 0,
        Find_NoName,            // no function has the name
        Find_OtherSignature,    // the name is there with another signature
        Find_SeveralSignatures  // the name has several and none was asked for
    };

    u32 functionCount();
    // By name, then signature.
    const Function* functionAt(u32 index);
    // pSignature may be null when the name has one function.
    const Function* findFunction(const char* pName, const char* pSignature, FindResult* pResult);

    enum HookResult : s32 {
        HookResult_Added = 0,
        HookResult_Refused = -1,     // a constructor or destructor takes Before and After only; a function whose result
                                     // is fixed takes no wrap or replace
        HookResult_Replaced = -2,    // another owner replaces the function already (replacedBy says who)
        HookResult_NotGameThread = -3  // hooks change on the game thread only
    };
    // Hooks of a function run by `order`, then in the order they were added. On the game thread only: a function's
    // hooks are never changed in place; the new list is published at once and a call already running keeps its own.
    HookResult addHook(const Function* pFunction, HookKind kind, HookCallback pCallback, void* pUser, const void* pOwner, s32 order);
    // One hook of an owner: 0 taken out, -1 the owner has no such hook, -3 not on the game thread.
    s32 removeHook(const Function* pFunction, HookKind kind, HookCallback pCallback, void* pUser, const void* pOwner);
    void removeHooks(const void* pOwner);
    const void* replacedBy(const Function* pFunction);
    // A function's result kind (luma_hook.h kPlain, kReference, kFixed; a constructor or destructor: kPlain). On a
    // fixed result a before's skip is not taken (the function runs; reported) and no wrap or replace binds; a
    // reference no hook wrote is filled by the function itself (reported) (loader contract 4, LR-1).
    unsigned resultKind(const Function* pFunction);
    // A constructor or destructor: its hooks are told (before and after only).
    inline bool isTell(const Function* pFunction) { return pFunction->invoke == nullptr; }
    // The hook lists replaced so far that no call can be inside any more, freed; the rest wait for a later call. On the
    // game thread, once a frame (GameSystem::update) and when a mod is taken out. A list is freed when the game
    // thread has no call in it and two flips of the other threads' counters (SRCU), each made once the calls counted
    // on the side it leaves have ended, came after it was replaced (loader contract 4, INBOX-F 37).
    // `waiting`: something else waits on the grace (a mod taken out), so the counters flip even with no list retired.
    void reclaim(bool waiting = false);
    // The lists replaced and not yet freed (a fact for 1.1's tests: bounded, 0 once no call holds one).
    u32 retiredCount();
    // A grace: marked after the hooks are taken out; over when no call on any thread can still be in a list replaced
    // before the mark (two flips since, and no such list still held by the game thread) (loader contract LR-5).
    u32 graceMark();
    bool graceOver(u32 mark);
    // From a wrap: runs the wraps after it and then the function.
    void proceed(Call* pCall);

    // The game thread: marked once where it starts (nebula_game_main).
    void markGameThread();
    bool onGameThread();

    // A before skipped a function whose result is fixed: the skip was not taken. A reference result no hook wrote and
    // no wrap proceeded to: the function ran. `pOwner` the hook's (the before's, else the outermost wrap's). Set by the
    // loader to report each once.
    extern void (*gSkipRefused)(const Function* pFunction, const void* pOwner);
    extern void (*gNoResult)(const Function* pFunction, const void* pOwner);
}
