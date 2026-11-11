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
        void** args;   // args[0]: `this`, or null for a function without one; args[1..]: pointers to the arguments
        void* result;  // where the result is, or goes; null for a constructor or destructor
        u32 next;      // the runner's own
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
        HookResult_Refused = -1,   // a constructor or destructor takes Before and After only
        HookResult_Replaced = -2   // another owner replaces the function already (replacedBy says who)
    };
    // Hooks of a function run by `order`, then in the order they were added. Only while no other thread runs the
    // game: at start-up, and where a reload stops the stage.
    HookResult addHook(const Function* pFunction, HookKind kind, HookCallback pCallback, void* pUser, const void* pOwner, s32 order);
    void removeHooks(const void* pOwner);
    const void* replacedBy(const Function* pFunction);
    // From a wrap: runs the wraps after it and then the function.
    void proceed(Call* pCall);
}
