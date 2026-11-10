#pragma once

// LumaScript: a mod's logic as data (agents/LUMACORE-PLAN.md 8.5). LumaLink compiles graphs and the text form into a
// .lumascript; the game runs it here on every platform: a fixed evaluator over a step list, no code generated.

#include <nebula_mod_api.h>
#include <nebula_types.h>

namespace lumascript {
    // Opcodes. Each step is 8 bytes: op, dst, a, b (registers), imm, imm2 (constants, imports, jump targets, counts).
    enum Op : u8 {
        Op_Nop = 0,
        Op_LoadConst,   // dst = const[imm]
        Op_Move,        // dst = a
        Op_Add, Op_Sub, Op_Mul, Op_Div, Op_Mod,  // dst = a op b (int, float, vec3 by kind; vec3 * float scales)
        Op_Neg, Op_Not,                          // dst = op a
        Op_Eq, Op_Ne, Op_Lt, Op_Le, Op_Gt, Op_Ge,  // dst = bool
        Op_And, Op_Or,
        Op_Jump,        // pc = imm
        Op_JumpIf,      // if a: pc = imm
        Op_JumpIfNot,
        Op_Call,        // dst.. = import[imm](a .. a+b-1); imm2: how many results are kept
        Op_CallScript,  // dst = function[imm](a .. a+b-1)
        Op_Return,      // returns a (b == 1) or nothing
        Op_GetLocal,    // dst = instance variable imm
        Op_SetLocal,    // instance variable imm = a
        Op_GetGlobal,   // dst = mod variable imm
        Op_SetGlobal,
        Op_GetSaved,    // dst = saved variable imm (kept in the save)
        Op_SetSaved,
        Op_FieldGet,    // dst = field import[imm] of object a, element b (255: 0)
        Op_FieldSet,    // field import[imm] of object a, element 0 = b
        Op_MakeVec3,    // dst = (a, b, register imm)
        Op_VecX, Op_VecY, Op_VecZ,
        Op_Dot, Op_Cross, Op_Length, Op_Normalize,
        Op_Lerp,        // dst = a + (b - a) * register imm
        Op_Min, Op_Max, Op_Abs, Op_Floor, Op_Sqrt, Op_Sin, Op_Cos, Op_Atan2,
        Op_Random,      // dst = random in [a, b)
        Op_SetState,    // the instance's state = string imm (its enter handler runs next frame)
        Op_Wait,        // this handler waits a frames, then continues after this step
        Op_Emit,        // event string imm with value a, to every script that answers it
        Op_Format,      // dst = text: format string const imm with a .. a+b-1
        Op_ToInt, Op_ToFloat,
        Op_Count
    };

    struct Step {
        u8 op;
        u8 dst;
        u8 a;
        u8 b;
        u16 imm;
        u16 imm2;
    };

    // What an import names: a toolkit function, an API call, a field, a registry row, an event.
    enum ImportKind : u8 {
        Import_Toolkit = 0,
        Import_Api,
        Import_Field,
        Import_Registry,
        Import_Event,
    };

    struct Function {
        u32 name;       // string index
        u32 firstStep;
        u32 stepCount;
        u16 registers;  // up to 255
        u16 params;
        u32 line;       // source line of the first step, for messages
    };

    // A loaded script: the file's tables, its imports resolved.
    class Program;

    // Loads a .lumascript (the bytes stay owned by the caller until unload). Null and a message on any problem:
    // a bad file, a name the game doesn't have (with the source line), a version newer than this game's.
    Program* load(const u8* pBytes, u32 size, const char* pModName, char* pMessage, u32 messageSize);
    void unload(Program* pProgram);

    // An instance: one per actor (or one for a mod's global script). Its locals, state, waits. Destroyed only between
    // script runs (the dispatch records free an actor's instance when its scene ends, never from inside a handler).
    class Instance;
    Instance* createInstance(Program* pProgram, const NebulaValue& rSelf);
    void destroyInstance(Instance* pInstance);

    // Runs the handler for an event name ("start", "frame", "touch_player", "message:ACTMES_PLAYER_SPIN"...), with
    // arguments. Returns what it returned (none when it has no such handler or returned nothing). A handler that runs
    // past the mod's step budget for this frame stops there, logged once.
    NebulaValue run(Instance* pInstance, u32 handler, const NebulaValue* pArgs, u32 count);
    // The handler's index by name, or -1 (resolved once, at load, by the dispatch records).
    s32 findHandler(const Program* pProgram, const char* pName);
    // Each frame: waits count down and continue; the state's "frame" handler runs.
    void frame(Instance* pInstance);
    // Starts a frame: every script's step budget (a tunable, per script file) and the frame's formatted text.
    void beginFrame();
    // Text from a format string: "{}" takes the next value. Valid for this frame; a variable keeps its own copy.
    NebulaValue format(const char* pFormat, const NebulaValue* pValues, u32 count);
};  // namespace lumascript
