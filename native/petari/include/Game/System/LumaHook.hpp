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
                                     // has no default takes no wrap or replace (noSkip)
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
    // Whether a function's result has no default the kernel can give it (a reference, or a class with no default
    // constructor or with a destructor): a before's skip is then not taken (the function runs; a script error is
    // reported), and no wrap or replace binds (loader contract 4, Z-2).
    bool noSkip(const Function* pFunction);
    // The hook lists replaced since the last call, freed: only where no call can be inside one (a mod taken out while
    // the stage is stopped, the game's end).
    void freeRetired();
    // From a wrap: runs the wraps after it and then the function.
    void proceed(Call* pCall);

    // The game thread: marked once where it starts (nebula_game_main).
    void markGameThread();
    bool onGameThread();

    // A before skipped a function whose result has no default: the skip was not taken. Set by the loader to report it.
    extern void (*gSkipRefused)(const Function* pFunction, const void* pOwner);
}
