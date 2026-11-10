// LumaScript's loader and evaluator (LumaScript.hpp). File layout, little-endian:
//   "LSC1", u16 version, u16 flags,
//   u32 counts: strings, constants, imports, functions, steps, handlers, locals, globals, saved,
//   strings: u32 length + bytes (no terminator in the file; one is added at load),
//   constants: u8 kind, then i64 / f64 / 3 x f32 / u32 string index,
//   imports: u8 kind, u32 name string, u32 line,
//   functions: Function rows,
//   steps: Step rows,
//   handlers: u32 name string, u32 function, u32 state string (0xFFFFFFFF: any state),
//   locals, globals, saved: u32 name string, u8 kind each.
#include "Game/System/LumaScript.hpp"
#include "Game/System/LumaCore.hpp"
#include "Game/System/LumaHost.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include <cmath>
#include <cstring>
#include <nebula_module.h>
#include <new>

namespace {
    // Steps one mod's scripts may run in one frame: past it the running handler stops (logged once).
    NEBULA_TUNABLE(u32, cStepBudget, 200000);
    // Frames a handler may wait at most (Op_Wait).
    NEBULA_TUNABLE(u32, cMaxWait, 36000);

    const u16 cVersion = 1;
    const u32 cNoState = 0xFFFFFFFFu;
    const u32 cMaxRegisters = 255;
    const u32 cMaxCallDepth = 32;

    struct Reader {
        const u8* mData;
        u32 mSize;
        u32 mAt;
        bool mBad;

        bool need(u32 bytes) {
            if (mBad || mSize - mAt < bytes) {
                mBad = true;
                return false;
            }
            return true;
        }
        u8 u8v() {
            if (!need(1)) {
                return 0;
            }
            return mData[mAt++];
        }
        u16 u16v() {
            if (!need(2)) {
                return 0;
            }
            const u16 v = static_cast< u16 >(mData[mAt] | (mData[mAt + 1] << 8));
            mAt += 2;
            return v;
        }
        u32 u32v() {
            if (!need(4)) {
                return 0;
            }
            u32 v = 0;
            for (u32 i = 0; i < 4; i++) {
                v |= static_cast< u32 >(mData[mAt + i]) << (8 * i);
            }
            mAt += 4;
            return v;
        }
        u64 u64v() {
            const u64 lo = u32v();
            const u64 hi = u32v();
            return lo | (hi << 32);
        }
        f32 f32v() {
            const u32 bits = u32v();
            f32 v;
            std::memcpy(&v, &bits, sizeof(v));
            return v;
        }
        f64 f64v() {
            const u64 bits = u64v();
            f64 v;
            std::memcpy(&v, &bits, sizeof(v));
            return v;
        }
    };

    NebulaValue none() {
        NebulaValue v;
        std::memset(&v, 0, sizeof(v));
        return v;
    }

    NebulaValue fromBool(bool b) {
        NebulaValue v = none();
        v.kind = NEBULA_KIND_BOOL;
        v.u.i = b ? 1 : 0;
        return v;
    }

    NebulaValue fromInt(s64 i) {
        NebulaValue v = none();
        v.kind = NEBULA_KIND_INT;
        v.u.i = i;
        return v;
    }

    NebulaValue fromFloat(f64 f) {
        NebulaValue v = none();
        v.kind = NEBULA_KIND_FLOAT;
        v.u.f = f;
        return v;
    }

    // Signed overflow wraps (two's complement) instead of being undefined.
    s64 wrapAdd(s64 x, s64 y) {
        return static_cast< s64 >(static_cast< u64 >(x) + static_cast< u64 >(y));
    }
    s64 wrapSub(s64 x, s64 y) {
        return static_cast< s64 >(static_cast< u64 >(x) - static_cast< u64 >(y));
    }
    s64 wrapMul(s64 x, s64 y) {
        return static_cast< s64 >(static_cast< u64 >(x) * static_cast< u64 >(y));
    }
    // A float to an int: NaN is 0, past the range is the range's end.
    s64 toInt(f64 f) {
        if (!(f == f)) {
            return 0;
        }
        if (f >= 9.2233720368547748e18) {
            return 0x7FFFFFFFFFFFFFFFll;
        }
        if (f <= -9.2233720368547758e18) {
            return static_cast< s64 >(0x8000000000000000ull);
        }
        return static_cast< s64 >(f);
    }

    NebulaValue fromVec(f32 x, f32 y, f32 z) {
        NebulaValue v = none();
        v.kind = NEBULA_KIND_VEC3;
        v.u.v[0] = x;
        v.u.v[1] = y;
        v.u.v[2] = z;
        return v;
    }

    bool isNumber(const NebulaValue& v) {
        return v.kind == NEBULA_KIND_INT || v.kind == NEBULA_KIND_FLOAT || v.kind == NEBULA_KIND_BOOL;
    }

    f64 number(const NebulaValue& v) {
        switch (v.kind) {
        case NEBULA_KIND_BOOL:
        case NEBULA_KIND_INT:
            return static_cast< f64 >(v.u.i);
        case NEBULA_KIND_FLOAT:
            return v.u.f;
        default:
            return 0.0;
        }
    }

    bool truthy(const NebulaValue& v) {
        switch (v.kind) {
        case NEBULA_KIND_BOOL:
        case NEBULA_KIND_INT:
            return v.u.i != 0;
        case NEBULA_KIND_FLOAT:
            return v.u.f != 0.0;
        case NEBULA_KIND_HANDLE:
            return v.u.h.p != nullptr && v.u.h.generation == lumacore::generation();
        case NEBULA_KIND_TEXT:
            return v.u.s != nullptr && v.u.s[0] != '\0';
        case NEBULA_KIND_VEC3:
            return v.u.v[0] != 0.0f || v.u.v[1] != 0.0f || v.u.v[2] != 0.0f;
        default:
            return false;
        }
    }

    // Arithmetic keeps ints as ints until a float takes part.
    NebulaValue arith(u8 op, const NebulaValue& a, const NebulaValue& b) {
        using namespace lumascript;
        if (a.kind == NEBULA_KIND_VEC3 || b.kind == NEBULA_KIND_VEC3) {
            if (a.kind == NEBULA_KIND_VEC3 && b.kind == NEBULA_KIND_VEC3) {
                switch (op) {
                case Op_Add:
                    return fromVec(a.u.v[0] + b.u.v[0], a.u.v[1] + b.u.v[1], a.u.v[2] + b.u.v[2]);
                case Op_Sub:
                    return fromVec(a.u.v[0] - b.u.v[0], a.u.v[1] - b.u.v[1], a.u.v[2] - b.u.v[2]);
                case Op_Mul:
                    return fromVec(a.u.v[0] * b.u.v[0], a.u.v[1] * b.u.v[1], a.u.v[2] * b.u.v[2]);
                default:
                    return none();
                }
            }
            const NebulaValue& vec = a.kind == NEBULA_KIND_VEC3 ? a : b;
            const f32 s = static_cast< f32 >(number(a.kind == NEBULA_KIND_VEC3 ? b : a));
            if (op == Op_Mul) {
                return fromVec(vec.u.v[0] * s, vec.u.v[1] * s, vec.u.v[2] * s);
            }
            if (op == Op_Div && a.kind == NEBULA_KIND_VEC3 && s != 0.0f) {
                return fromVec(vec.u.v[0] / s, vec.u.v[1] / s, vec.u.v[2] / s);
            }
            return none();
        }
        if (!isNumber(a) || !isNumber(b)) {
            return none();
        }
        if (a.kind != NEBULA_KIND_FLOAT && b.kind != NEBULA_KIND_FLOAT) {
            const s64 x = a.u.i;
            const s64 y = b.u.i;
            switch (op) {
            case Op_Add:
                return fromInt(wrapAdd(x, y));
            case Op_Sub:
                return fromInt(wrapSub(x, y));
            case Op_Mul:
                return fromInt(wrapMul(x, y));
            case Op_Div:
                return y == 0 ? none() : y == -1 ? fromInt(wrapSub(0, x)) : fromInt(x / y);
            case Op_Mod:
                return y == 0 ? none() : y == -1 ? fromInt(0) : fromInt(x % y);
            default:
                return none();
            }
        }
        const f64 x = number(a);
        const f64 y = number(b);
        switch (op) {
        case Op_Add:
            return fromFloat(x + y);
        case Op_Sub:
            return fromFloat(x - y);
        case Op_Mul:
            return fromFloat(x * y);
        case Op_Div:
            return y == 0.0 ? none() : fromFloat(x / y);
        case Op_Mod: {
            if (y == 0.0) {
                return none();
            }
            // fmod without the C runtime: x - trunc(x / y) * y.
            const f64 q = x / y;
            return fromFloat(x - (q < 0.0 ? -std::floor(-q) : std::floor(q)) * y);
        }
        default:
            return none();
        }
    }

    bool sameValue(const NebulaValue& a, const NebulaValue& b) {
        if (isNumber(a) && isNumber(b)) {
            return number(a) == number(b);
        }
        if (a.kind != b.kind) {
            return false;
        }
        switch (a.kind) {
        case NEBULA_KIND_NONE:
            return true;
        case NEBULA_KIND_VEC3:
            return a.u.v[0] == b.u.v[0] && a.u.v[1] == b.u.v[1] && a.u.v[2] == b.u.v[2];
        case NEBULA_KIND_TEXT:
            return MR::isEqualString(a.u.s != nullptr ? a.u.s : "", b.u.s != nullptr ? b.u.s : "");
        case NEBULA_KIND_HANDLE:
            return a.u.h.p == b.u.h.p && a.type == b.type;
        default:
            return false;
        }
    }

    u32 sRandom = 0x2545F491u;
    f64 random01() {
        // xorshift32: the same sequence on every platform.
        sRandom ^= sRandom << 13;
        sRandom ^= sRandom >> 17;
        sRandom ^= sRandom << 5;
        return static_cast< f64 >(sRandom) / 4294967296.0;
    }
};  // namespace

namespace lumascript {
    struct Import {
        u8 kind;
        u32 name;
        u32 line;
        s32 index;  // resolved: toolkit row, API call, registry row; fields resolve per class, at the call
        u16 fieldClass;                     // the class the cached field was found for
        const lumacore::FieldRow* field;    // null: not found for fieldClass
    };

    // The events the evaluator itself runs; the rest are found by name, once, by the dispatch records.
    enum HandlerEvent : u8 {
        HandlerEvent_Other = 0,
        HandlerEvent_Frame,
        HandlerEvent_Enter,
        HandlerEvent_Leave,
    };

    struct Handler {
        u32 name;
        u32 function;
        u32 state;
        u8 event;
    };

    struct Variable {
        u32 name;
        u8 kind;
    };

    struct Matrix12 {
        f32 m[12];
    };

    class Program {
    public:
        const char* mMod;
        char** mStrings;
        u32 mStringCount;
        NebulaValue* mConstants;
        u32 mConstantCount;
        Import* mImports;
        u32 mImportCount;
        Function* mFunctions;
        u32 mFunctionCount;
        Step* mSteps;
        u32 mStepCount;
        Handler* mHandlers;
        u32 mHandlerCount;
        Variable* mLocals;
        u32 mLocalCount;
        NebulaValue* mGlobals;
        Matrix12* mGlobalMatrices;
        Variable* mGlobalNames;
        u32 mGlobalCount;
        Variable* mSaved;
        u32 mSavedCount;
        u32 mStepsThisFrame;
        bool mBudgetLogged;
        bool mWaitLogged;
        bool mDepthLogged;
        bool mHasFrame;  // any handler answers "frame", "enter" or "leave"
        // Text a script keeps (in a variable, past this frame): each distinct text once, owned here.
        char** mKept;
        u32 mKeptCount;
        u32 mKeptRoom;
        u32 mKeptBytes;
        bool mKeptLogged;

        const char* string(u32 index) const {
            return index < mStringCount ? mStrings[index] : "";
        }
    };

    struct Waiting {
        u32 function;
        u32 pc;
        u32 frames;
        NebulaValue* registers;  // the function's registers, owned
        Matrix12* matrices;      // their own matrices (made only when one holds a script's matrix)
    };

    // Registers of the running calls: one stack for every script (they all run on the game's thread).
    const u32 cStackValues = 16384;
    NebulaValue* sStack = nullptr;
    u32 sStackTop = 0;
    // Text Op_Format makes: valid for the frame (a variable keeps a copy).
    const u32 cFormatSlots = 256;
    const u32 cFormatLength = 256;
    char sFormatted[cFormatSlots][cFormatLength];
    u32 sFormatNext = 0;
    // Kept text per script, at most.
    NEBULA_TUNABLE(u32, cKeptBytes, 262144);

    class Instance {
    public:
        Program* mProgram;
        NebulaValue mSelf;
        NebulaValue* mLocals;
        Matrix12* mLocalMatrices;  // each local's own matrix, when it holds one a script made
        u32 mState;       // string index; cNoState before the first set_state
        u32 mNextState;   // set by Op_SetState, taken at the next frame
        u32 mStateFrames;
        Waiting* mWaiting;
        u32 mWaitingCount;
        u32 mWaitingRoom;
    };

    static void freeProgram(Program* p) {
        if (p == nullptr) {
            return;
        }
        for (u32 i = 0; i < p->mStringCount; i++) {
            delete[] p->mStrings[i];
        }
        delete[] p->mStrings;
        delete[] p->mConstants;
        delete[] p->mImports;
        delete[] p->mFunctions;
        delete[] p->mSteps;
        delete[] p->mHandlers;
        delete[] p->mLocals;
        delete[] p->mGlobals;
        delete[] p->mGlobalMatrices;
        delete[] p->mGlobalNames;
        delete[] p->mSaved;
        for (u32 i = 0; i < p->mKeptCount; i++) {
            delete[] p->mKept[i];
        }
        delete[] p->mKept;
        delete p;
    }

    // A matrix a script made (the ring, or a variable's own): generation 0. The game's matrices are views into the
    // scene and stay views.
    static bool scriptMatrix(const NebulaValue& rValue) {
        return rValue.kind == NEBULA_KIND_HANDLE && (rValue.type & 0xFFFF) == lumacore::HandleType_Matrix &&
               rValue.u.h.generation == 0 && rValue.u.h.p != nullptr;
    }

    // A value a variable keeps past this frame. Text: a copy that outlives the frame, shared by equal texts (none when
    // the script's kept text is full, logged once). A script's matrix: copied into the variable's own 12 floats, so
    // the ring turning over or another variable changing never changes it.
    static NebulaValue keep(Program* p, const NebulaValue& rValue, Matrix12* pMatrix) {
        if (pMatrix != nullptr && scriptMatrix(rValue)) {
            if (rValue.u.h.p != pMatrix) {
                std::memcpy(pMatrix->m, rValue.u.h.p, sizeof(pMatrix->m));
            }
            NebulaValue v = rValue;
            v.u.h.p = pMatrix;
            return v;
        }
        if (rValue.kind != NEBULA_KIND_TEXT || rValue.u.s == nullptr) {
            return rValue;
        }
        const char* const text = rValue.u.s;
        for (u32 i = 0; i < p->mStringCount; i++) {
            if (p->mStrings[i] == text) {
                return rValue;  // the script's own text lives as long as it does
            }
        }
        for (u32 i = 0; i < p->mKeptCount; i++) {
            if (p->mKept[i] == text || MR::isEqualString(p->mKept[i], text)) {
                NebulaValue v = rValue;
                v.u.s = p->mKept[i];
                return v;
            }
        }
        const u32 length = static_cast< u32 >(std::strlen(text));
        if (p->mKeptBytes + length + 1 > cKeptBytes) {
            if (!p->mKeptLogged) {
                p->mKeptLogged = true;
                nebula_log("mods: %s: a script keeps more text than it may (%u bytes); the rest is none\n", p->mMod,
                           cKeptBytes);
            }
            return none();
        }
        if (p->mKeptCount == p->mKeptRoom) {
            const u32 room = p->mKeptRoom == 0 ? 16 : p->mKeptRoom * 2;
            char** grown = new char*[room];
            for (u32 i = 0; i < p->mKeptCount; i++) {
                grown[i] = p->mKept[i];
            }
            delete[] p->mKept;
            p->mKept = grown;
            p->mKeptRoom = room;
        }
        char* copy = new char[length + 1];
        std::memcpy(copy, text, length + 1);
        p->mKept[p->mKeptCount++] = copy;
        p->mKeptBytes += length + 1;
        NebulaValue v = rValue;
        v.u.s = copy;
        return v;
    }

    static void say(char* pMessage, u32 size, const char* pText, const char* pName, u32 line) {
        if (pMessage == nullptr || size == 0) {
            return;
        }
        if (line != 0) {
            nebula_snprintf(pMessage, size, "%s %s (line %u)", pText, pName != nullptr ? pName : "", line);
        } else {
            nebula_snprintf(pMessage, size, "%s %s", pText, pName != nullptr ? pName : "");
        }
    }

    static bool resolve(Program* p, Import& rImport, char* pMessage, u32 size) {
        const char* const name = p->string(rImport.name);
        switch (rImport.kind) {
        case Import_Toolkit:
        case Import_Api:  // the plugin API's rows are in the toolkit as "nebula.<name>"
            rImport.index = lumacore::findToolkit(name);
            break;
        case Import_Field:
            rImport.index = 0;  // by the object's class, at each call
            return true;
        case Import_Registry:
            rImport.index = lumacore::registryIndex(name);
            break;
        case Import_Event:
            rImport.index = 0;
            return true;
        default:
            rImport.index = -1;
            break;
        }
        if (rImport.index < 0) {
            say(pMessage, size, "the game has no", name, rImport.line);
            return false;
        }
        return true;
    }

    Program* load(const u8* pBytes, u32 size, const char* pModName, char* pMessage, u32 messageSize) {
        Reader r{pBytes, size, 0, false};
        if (size < 8 || pBytes[0] != 'L' || pBytes[1] != 'S' || pBytes[2] != 'C' || pBytes[3] != '1') {
            say(pMessage, messageSize, "not a LumaScript file:", pModName, 0);
            return nullptr;
        }
        r.mAt = 4;
        const u16 version = r.u16v();
        (void)r.u16v();
        if (version > cVersion) {
            say(pMessage, messageSize, "a script made for a newer Nebula:", pModName, 0);
            return nullptr;
        }
        if (sStack == nullptr) {
            sStack = new NebulaValue[cStackValues];
        }
        Program* p = new Program();
        if (p == nullptr) {
            return nullptr;
        }
        std::memset(p, 0, sizeof(*p));
        p->mMod = pModName;
        p->mStringCount = r.u32v();
        p->mConstantCount = r.u32v();
        p->mImportCount = r.u32v();
        p->mFunctionCount = r.u32v();
        p->mStepCount = r.u32v();
        p->mHandlerCount = r.u32v();
        p->mLocalCount = r.u32v();
        p->mGlobalCount = r.u32v();
        p->mSavedCount = r.u32v();
        // Counts can't exceed the file: every row is at least one byte.
        const u32 rest = size - r.mAt;
        if (r.mBad || p->mStringCount > rest || p->mConstantCount > rest || p->mImportCount > rest ||
            p->mFunctionCount > rest || p->mStepCount > rest / sizeof(Step) || p->mHandlerCount > rest ||
            p->mLocalCount > rest || p->mGlobalCount > rest || p->mSavedCount > rest) {
            say(pMessage, messageSize, "a damaged script:", pModName, 0);
            freeProgram(p);
            return nullptr;
        }
        p->mStrings = new char*[p->mStringCount]();
        for (u32 i = 0; i < p->mStringCount; i++) {
            const u32 length = r.u32v();
            if (!r.need(length)) {
                break;
            }
            p->mStrings[i] = new char[length + 1];
            std::memcpy(p->mStrings[i], pBytes + r.mAt, length);
            p->mStrings[i][length] = '\0';
            r.mAt += length;
        }
        p->mConstants = new NebulaValue[p->mConstantCount];
        for (u32 i = 0; i < p->mConstantCount; i++) {
            const u8 kind = r.u8v();
            NebulaValue v = none();
            switch (kind) {
            case NEBULA_KIND_BOOL:
            case NEBULA_KIND_INT:
                v.kind = kind;
                v.u.i = static_cast< s64 >(r.u64v());
                break;
            case NEBULA_KIND_FLOAT:
                v = fromFloat(r.f64v());
                break;
            case NEBULA_KIND_VEC3: {
                const f32 x = r.f32v();
                const f32 y = r.f32v();
                v = fromVec(x, y, r.f32v());
                break;
            }
            case NEBULA_KIND_TEXT:
                v.kind = NEBULA_KIND_TEXT;
                v.u.s = p->string(r.u32v());
                break;
            case NEBULA_KIND_NONE:
                break;
            default:
                r.mBad = true;
                break;
            }
            p->mConstants[i] = v;
        }
        p->mImports = new Import[p->mImportCount];
        for (u32 i = 0; i < p->mImportCount; i++) {
            p->mImports[i].kind = r.u8v();
            p->mImports[i].name = r.u32v();
            p->mImports[i].line = r.u32v();
            p->mImports[i].index = -1;
            p->mImports[i].fieldClass = 0xFFFF;
            p->mImports[i].field = nullptr;
        }
        p->mFunctions = new Function[p->mFunctionCount];
        for (u32 i = 0; i < p->mFunctionCount; i++) {
            Function& f = p->mFunctions[i];
            f.name = r.u32v();
            f.firstStep = r.u32v();
            f.stepCount = r.u32v();
            f.registers = r.u16v();
            f.params = r.u16v();
            f.line = r.u32v();
            if (f.registers > cMaxRegisters || f.params > f.registers || f.firstStep > p->mStepCount ||
                f.stepCount > p->mStepCount - f.firstStep) {
                r.mBad = true;
            }
        }
        p->mSteps = new Step[p->mStepCount];
        for (u32 i = 0; i < p->mStepCount; i++) {
            Step& s = p->mSteps[i];
            s.op = r.u8v();
            s.dst = r.u8v();
            s.a = r.u8v();
            s.b = r.u8v();
            s.imm = r.u16v();
            s.imm2 = r.u16v();
            if (s.op >= Op_Count) {
                r.mBad = true;
            }
        }
        p->mHandlers = new Handler[p->mHandlerCount];
        for (u32 i = 0; i < p->mHandlerCount; i++) {
            p->mHandlers[i].name = r.u32v();
            p->mHandlers[i].function = r.u32v();
            p->mHandlers[i].state = r.u32v();
            p->mHandlers[i].event = HandlerEvent_Other;
            if (p->mHandlers[i].function >= p->mFunctionCount) {
                r.mBad = true;
            }
        }
        p->mLocals = new Variable[p->mLocalCount];
        for (u32 i = 0; i < p->mLocalCount; i++) {
            p->mLocals[i].name = r.u32v();
            p->mLocals[i].kind = r.u8v();
        }
        p->mGlobals = new NebulaValue[p->mGlobalCount];
        p->mGlobalMatrices = new Matrix12[p->mGlobalCount > 0 ? p->mGlobalCount : 1];
        p->mGlobalNames = new Variable[p->mGlobalCount];
        for (u32 i = 0; i < p->mGlobalCount; i++) {
            p->mGlobalNames[i].name = r.u32v();
            p->mGlobalNames[i].kind = r.u8v();
            p->mGlobals[i] = none();
        }
        p->mSaved = new Variable[p->mSavedCount];
        for (u32 i = 0; i < p->mSavedCount; i++) {
            p->mSaved[i].name = r.u32v();
            p->mSaved[i].kind = r.u8v();
        }
        if (r.mBad) {
            say(pMessage, messageSize, "a damaged script:", pModName, 0);
            freeProgram(p);
            return nullptr;
        }
        // Strings named by index (handlers, states) exist.
        for (u32 i = 0; i < p->mHandlerCount; i++) {
            Handler& h = p->mHandlers[i];
            if (h.name >= p->mStringCount || (h.state != cNoState && h.state >= p->mStringCount)) {
                say(pMessage, messageSize, "a damaged script:", pModName, 0);
                freeProgram(p);
                return nullptr;
            }
            const char* const name = p->string(h.name);
            h.event = MR::isEqualString(name, "frame")   ? HandlerEvent_Frame
                      : MR::isEqualString(name, "enter") ? HandlerEvent_Enter
                      : MR::isEqualString(name, "leave") ? HandlerEvent_Leave
                                                         : HandlerEvent_Other;
            p->mHasFrame = p->mHasFrame || h.event != HandlerEvent_Other;
        }
        // Every step's operands within its function's registers and the program's tables: the evaluator then checks
        // nothing per step but the budget.
        for (u32 f = 0; f < p->mFunctionCount; f++) {
            const Function& fn = p->mFunctions[f];
            const u32 regs = fn.registers;
            for (u32 i = fn.firstStep; i < fn.firstStep + fn.stepCount; i++) {
                const Step& s = p->mSteps[i];
                const bool dst = s.dst < regs;
                const bool a = s.a < regs;
                const bool b = s.b < regs;
                const bool reg = s.imm < regs;
                const bool range = static_cast< u32 >(s.a) + s.b <= regs;
                bool ok = true;
                switch (s.op) {
                case Op_Nop:
                    break;
                case Op_LoadConst:
                    ok = dst && s.imm < p->mConstantCount;
                    break;
                case Op_Move:
                case Op_Neg:
                case Op_Not:
                case Op_VecX:
                case Op_VecY:
                case Op_VecZ:
                case Op_Length:
                case Op_Normalize:
                case Op_Abs:
                case Op_Floor:
                case Op_Sqrt:
                case Op_Sin:
                case Op_Cos:
                case Op_ToInt:
                case Op_ToFloat:
                    ok = dst && a;
                    break;
                case Op_Add:
                case Op_Sub:
                case Op_Mul:
                case Op_Div:
                case Op_Mod:
                case Op_Eq:
                case Op_Ne:
                case Op_Lt:
                case Op_Le:
                case Op_Gt:
                case Op_Ge:
                case Op_And:
                case Op_Or:
                case Op_Dot:
                case Op_Cross:
                case Op_Min:
                case Op_Max:
                case Op_Atan2:
                case Op_Random:
                    ok = dst && a && b;
                    break;
                case Op_MakeVec3:
                case Op_Lerp:
                    ok = dst && a && b && reg;
                    break;
                case Op_Jump:
                    ok = s.imm < fn.stepCount;
                    break;
                case Op_JumpIf:
                case Op_JumpIfNot:
                    ok = a && s.imm < fn.stepCount;
                    break;
                case Op_Call:
                    ok = range && s.imm < p->mImportCount &&
                         (p->mImports[s.imm].kind == Import_Toolkit || p->mImports[s.imm].kind == Import_Api) &&
                         (s.imm2 == 0 || static_cast< u32 >(s.dst) + s.imm2 <= regs);
                    break;
                case Op_CallScript:
                    ok = dst && range && s.imm < p->mFunctionCount;
                    break;
                case Op_Return:
                    ok = s.b != 1 || a;
                    break;
                case Op_GetLocal:
                case Op_GetGlobal:
                case Op_GetSaved:
                    ok = dst;
                    break;
                case Op_SetLocal:
                case Op_SetGlobal:
                case Op_SetSaved:
                    ok = a;
                    break;
                case Op_FieldGet:
                    ok = dst && a && (b || s.b == 255) && s.imm < p->mImportCount &&
                         p->mImports[s.imm].kind == Import_Field;
                    break;
                case Op_FieldSet:
                    ok = a && b && s.imm < p->mImportCount && p->mImports[s.imm].kind == Import_Field;
                    break;
                case Op_SetState:
                    ok = s.imm < p->mStringCount;
                    break;
                case Op_Wait:
                    ok = a;
                    break;
                case Op_Emit:
                    ok = a && s.imm < p->mImportCount && p->mImports[s.imm].kind == Import_Event;
                    break;
                case Op_Registry:
                    ok = dst && s.imm < p->mImportCount && p->mImports[s.imm].kind == Import_Registry;
                    break;
                case Op_Format:
                    ok = dst && range && s.imm < p->mConstantCount && p->mConstants[s.imm].kind == NEBULA_KIND_TEXT;
                    break;
                default:
                    ok = false;
                    break;
                }
                if (!ok) {
                    say(pMessage, messageSize, "a damaged script step in", p->string(fn.name), fn.line);
                    freeProgram(p);
                    return nullptr;
                }
            }
        }
        for (u32 i = 0; i < p->mImportCount; i++) {
            if (!resolve(p, p->mImports[i], pMessage, messageSize)) {
                freeProgram(p);
                return nullptr;
            }
        }
        return p;
    }

    void unload(Program* pProgram) {
        freeProgram(pProgram);
    }

    Instance* createInstance(Program* pProgram, const NebulaValue& rSelf) {
        Instance* i = new Instance();
        if (i == nullptr) {
            return nullptr;
        }
        i->mProgram = pProgram;
        i->mSelf = rSelf;
        i->mLocals = new NebulaValue[pProgram->mLocalCount > 0 ? pProgram->mLocalCount : 1];
        i->mLocalMatrices = new Matrix12[pProgram->mLocalCount > 0 ? pProgram->mLocalCount : 1];
        for (u32 n = 0; n < pProgram->mLocalCount; n++) {
            i->mLocals[n] = none();
        }
        i->mState = cNoState;
        i->mNextState = cNoState;
        i->mStateFrames = 0;
        i->mWaiting = nullptr;
        i->mWaitingCount = 0;
        i->mWaitingRoom = 0;
        return i;
    }

    void destroyInstance(Instance* pInstance) {
        if (pInstance == nullptr) {
            return;
        }
        delete[] pInstance->mLocals;
        delete[] pInstance->mLocalMatrices;
        for (u32 i = 0; i < pInstance->mWaitingCount; i++) {
            delete[] pInstance->mWaiting[i].registers;
            delete[] pInstance->mWaiting[i].matrices;
        }
        delete[] pInstance->mWaiting;
        delete pInstance;
    }

    s32 findHandler(const Program* pProgram, const char* pName) {
        for (u32 i = 0; i < pProgram->mHandlerCount; i++) {
            if (MR::isEqualString(pProgram->string(pProgram->mHandlers[i].name), pName)) {
                return static_cast< s32 >(i);
            }
        }
        return -1;
    }

    static void addWaiting(Instance* pInstance, u32 function, u32 pc, u32 frames, const NebulaValue* pRegisters,
                           u32 count) {
        if (pInstance->mWaitingCount == pInstance->mWaitingRoom) {
            const u32 room = pInstance->mWaitingRoom == 0 ? 4 : pInstance->mWaitingRoom * 2;
            Waiting* grown = new Waiting[room];
            for (u32 n = 0; n < pInstance->mWaitingCount; n++) {
                grown[n] = pInstance->mWaiting[n];
            }
            delete[] pInstance->mWaiting;
            pInstance->mWaiting = grown;
            pInstance->mWaitingRoom = room;
        }
        Waiting& w = pInstance->mWaiting[pInstance->mWaitingCount++];
        w.function = function;
        w.pc = pc;
        w.frames = frames;
        w.registers = new NebulaValue[count > 0 ? count : 1];
        w.matrices = nullptr;
        for (u32 n = 0; n < count; n++) {
            if (w.matrices == nullptr && scriptMatrix(pRegisters[n])) {
                w.matrices = new Matrix12[count];
            }
            // Formatted text lasts one frame and the matrix ring turns over: the wait keeps its own copies.
            w.registers[n] = keep(pInstance->mProgram, pRegisters[n], w.matrices != nullptr ? &w.matrices[n] : nullptr);
        }
    }

    // A field import for an object's class: looked up once per class change (the same script site mostly sees one class).
    static const lumacore::FieldRow* fieldFor(Program* p, Import& rImport, const NebulaValue& rObject) {
        if (rObject.kind != NEBULA_KIND_HANDLE || rObject.u.h.p == nullptr) {
            return nullptr;
        }
        const u16 id = lumacore::classOfValue(rObject);
        if (id != rImport.fieldClass) {
            rImport.fieldClass = id;
            rImport.field = lumacore::findField(id, p->string(rImport.name));
        }
        return rImport.field;
    }

    static NebulaValue execute(Instance* pInstance, u32 function, u32 pc, NebulaValue* r, u32 depth);

    static NebulaValue callFunction(Instance* pInstance, u32 function, const NebulaValue* pArgs, u32 count, u32 depth) {
        Program* const p = pInstance->mProgram;
        const Function& fn = p->mFunctions[function];
        if (depth >= cMaxCallDepth || cStackValues - sStackTop < fn.registers) {
            if (!p->mDepthLogged) {
                p->mDepthLogged = true;
                nebula_log("mods: %s: %s (line %u) calls too deep; that call does nothing\n", p->mMod,
                           p->string(fn.name), fn.line);
            }
            return none();
        }
        NebulaValue* const registers = sStack + sStackTop;
        sStackTop += fn.registers;
        for (u32 n = 0; n < fn.registers; n++) {
            registers[n] = n < count && n < fn.params ? pArgs[n] : none();
        }
        const NebulaValue result = execute(pInstance, function, 0, registers, depth);
        sStackTop -= fn.registers;
        return result;
    }

    static NebulaValue execute(Instance* pInstance, u32 function, u32 pc, NebulaValue* r, u32 depth) {
        Program* const p = pInstance->mProgram;
        const Function& fn = p->mFunctions[function];
        const Step* const steps = p->mSteps + fn.firstStep;
        while (pc < fn.stepCount) {
            if (++p->mStepsThisFrame > ::cStepBudget) {
                if (!p->mBudgetLogged) {
                    p->mBudgetLogged = true;
                    nebula_log("mods: %s: a script ran past its steps for one frame in %s (line %u); it stops there\n",
                               p->mMod, p->string(fn.name), fn.line);
                }
                return none();
            }
            const Step& s = steps[pc++];
            switch (s.op) {
            case Op_Nop:
                break;
            case Op_LoadConst:
                r[s.dst] = p->mConstants[s.imm];
                break;
            case Op_Move:
                r[s.dst] = r[s.a];
                break;
            case Op_Add:
            case Op_Sub:
            case Op_Mul:
            case Op_Div:
            case Op_Mod:
                r[s.dst] = arith(s.op, r[s.a], r[s.b]);
                break;
            case Op_Neg:
                if (r[s.a].kind == NEBULA_KIND_VEC3) {
                    r[s.dst] = fromVec(-r[s.a].u.v[0], -r[s.a].u.v[1], -r[s.a].u.v[2]);
                } else if (r[s.a].kind == NEBULA_KIND_FLOAT) {
                    r[s.dst] = fromFloat(-r[s.a].u.f);
                } else if (isNumber(r[s.a])) {
                    r[s.dst] = fromInt(wrapSub(0, r[s.a].u.i));
                } else {
                    r[s.dst] = none();
                }
                break;
            case Op_Not:
                r[s.dst] = fromBool(!truthy(r[s.a]));
                break;
            case Op_Eq:
                r[s.dst] = fromBool(sameValue(r[s.a], r[s.b]));
                break;
            case Op_Ne:
                r[s.dst] = fromBool(!sameValue(r[s.a], r[s.b]));
                break;
            case Op_Lt:
                r[s.dst] = fromBool(number(r[s.a]) < number(r[s.b]));
                break;
            case Op_Le:
                r[s.dst] = fromBool(number(r[s.a]) <= number(r[s.b]));
                break;
            case Op_Gt:
                r[s.dst] = fromBool(number(r[s.a]) > number(r[s.b]));
                break;
            case Op_Ge:
                r[s.dst] = fromBool(number(r[s.a]) >= number(r[s.b]));
                break;
            case Op_And:
                r[s.dst] = fromBool(truthy(r[s.a]) && truthy(r[s.b]));
                break;
            case Op_Or:
                r[s.dst] = fromBool(truthy(r[s.a]) || truthy(r[s.b]));
                break;
            case Op_Jump:
                pc = s.imm;
                break;
            case Op_JumpIf:
                if (truthy(r[s.a])) {
                    pc = s.imm;
                }
                break;
            case Op_JumpIfNot:
                if (!truthy(r[s.a])) {
                    pc = s.imm;
                }
                break;
            case Op_Call: {
                // The generator refuses a toolkit function with more than 7 out-parameters.
                NebulaValue out[8];
                for (u32 n = 0; n < 8; n++) {
                    out[n] = none();
                }
                const lumacore::ToolkitRow* row = lumacore::toolkitRow(static_cast< u32 >(p->mImports[s.imm].index));
                row->thunk(r + s.a, s.b, out);
                const u32 results = s.imm2 > 8 ? 8 : s.imm2;
                for (u32 n = 0; n < results; n++) {
                    r[s.dst + n] = out[n];
                }
                break;
            }
            case Op_Registry:
                r[s.dst] = fromInt(p->mImports[s.imm].index);
                break;
            case Op_CallScript:
                r[s.dst] = callFunction(pInstance, s.imm, r + s.a, s.b, depth + 1);
                break;
            case Op_Return:
                return s.b == 1 ? r[s.a] : none();
            case Op_GetLocal:
                r[s.dst] = s.imm < p->mLocalCount ? pInstance->mLocals[s.imm] : none();
                break;
            case Op_SetLocal:
                if (s.imm < p->mLocalCount) {
                    pInstance->mLocals[s.imm] = keep(p, r[s.a], &pInstance->mLocalMatrices[s.imm]);
                }
                break;
            case Op_GetGlobal:
                r[s.dst] = s.imm < p->mGlobalCount ? p->mGlobals[s.imm] : none();
                break;
            case Op_SetGlobal:
                if (s.imm < p->mGlobalCount) {
                    p->mGlobals[s.imm] = keep(p, r[s.a], &p->mGlobalMatrices[s.imm]);
                }
                break;
            case Op_GetSaved:
                r[s.dst] = s.imm < p->mSavedCount ? keep(p, lumacore::scriptSaved(p->mMod, p->string(p->mSaved[s.imm].name),
                                                                                p->mSaved[s.imm].kind), nullptr)
                                                  : none();
                break;
            case Op_SetSaved:
                if (s.imm < p->mSavedCount) {
                    lumacore::setScriptSaved(p->mMod, p->string(p->mSaved[s.imm].name), r[s.a]);
                }
                break;
            case Op_FieldGet: {
                const lumacore::FieldRow* field = fieldFor(p, p->mImports[s.imm], r[s.a]);
                NebulaValue v = none();
                if (field != nullptr) {
                    const s64 element = s.b == 255 ? 0 : toInt(number(r[s.b]));
                    if (element >= 0 && element < static_cast< s64 >(field->count)) {
                        (void)lumacore::readField(r[s.a], *field, static_cast< u32 >(element), &v);
                    }
                }
                r[s.dst] = v;
                break;
            }
            case Op_FieldSet: {
                const lumacore::FieldRow* field = fieldFor(p, p->mImports[s.imm], r[s.a]);
                if (field != nullptr) {
                    (void)lumacore::writeField(r[s.a], *field, 0u, r[s.b]);
                }
                break;
            }
            case Op_MakeVec3:
                r[s.dst] = fromVec(static_cast< f32 >(number(r[s.a])), static_cast< f32 >(number(r[s.b])),
                                   static_cast< f32 >(number(r[s.imm])));
                break;
            case Op_VecX:
            case Op_VecY:
            case Op_VecZ:
                r[s.dst] = r[s.a].kind == NEBULA_KIND_VEC3 ? fromFloat(r[s.a].u.v[s.op - Op_VecX]) : none();
                break;
            case Op_Dot: {
                const NebulaValue& a = r[s.a];
                const NebulaValue& b = r[s.b];
                if (a.kind != NEBULA_KIND_VEC3 || b.kind != NEBULA_KIND_VEC3) {
                    r[s.dst] = none();
                    break;
                }
                r[s.dst] = fromFloat(static_cast< f64 >(a.u.v[0]) * b.u.v[0] + static_cast< f64 >(a.u.v[1]) * b.u.v[1] +
                                     static_cast< f64 >(a.u.v[2]) * b.u.v[2]);
                break;
            }
            case Op_Cross: {
                if (r[s.a].kind != NEBULA_KIND_VEC3 || r[s.b].kind != NEBULA_KIND_VEC3) {
                    r[s.dst] = none();
                    break;
                }
                const f32* a = r[s.a].u.v;
                const f32* b = r[s.b].u.v;
                r[s.dst] = fromVec(a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]);
                break;
            }
            case Op_Length: {
                if (r[s.a].kind != NEBULA_KIND_VEC3) {
                    r[s.dst] = none();
                    break;
                }
                const f32* a = r[s.a].u.v;
                r[s.dst] = fromFloat(std::sqrt(static_cast< f64 >(a[0]) * a[0] + static_cast< f64 >(a[1]) * a[1] +
                                               static_cast< f64 >(a[2]) * a[2]));
                break;
            }
            case Op_Normalize: {
                if (r[s.a].kind != NEBULA_KIND_VEC3) {
                    r[s.dst] = none();
                    break;
                }
                const f32* a = r[s.a].u.v;
                const f32 length = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
                r[s.dst] = length > 0.0f ? fromVec(a[0] / length, a[1] / length, a[2] / length) : fromVec(0.0f, 0.0f, 0.0f);
                break;
            }
            case Op_Lerp: {
                const f64 t = number(r[s.imm]);
                if (r[s.a].kind == NEBULA_KIND_VEC3 && r[s.b].kind == NEBULA_KIND_VEC3) {
                    const f32* a = r[s.a].u.v;
                    const f32* b = r[s.b].u.v;
                    const f32 tf = static_cast< f32 >(t);
                    r[s.dst] = fromVec(a[0] + (b[0] - a[0]) * tf, a[1] + (b[1] - a[1]) * tf, a[2] + (b[2] - a[2]) * tf);
                } else {
                    r[s.dst] = fromFloat(number(r[s.a]) + (number(r[s.b]) - number(r[s.a])) * t);
                }
                break;
            }
            case Op_Min:
                r[s.dst] = number(r[s.a]) <= number(r[s.b]) ? r[s.a] : r[s.b];
                break;
            case Op_Max:
                r[s.dst] = number(r[s.a]) >= number(r[s.b]) ? r[s.a] : r[s.b];
                break;
            case Op_Abs:
                r[s.dst] = r[s.a].kind == NEBULA_KIND_FLOAT ? fromFloat(std::fabs(r[s.a].u.f))
                                                            : fromInt(r[s.a].u.i < 0 ? wrapSub(0, r[s.a].u.i) : r[s.a].u.i);
                break;
            case Op_Floor:
                r[s.dst] = fromInt(toInt(std::floor(number(r[s.a]))));
                break;
            case Op_Sqrt:
                r[s.dst] = number(r[s.a]) >= 0.0 ? fromFloat(std::sqrt(number(r[s.a]))) : none();
                break;
            case Op_Sin:
                r[s.dst] = fromFloat(std::sin(number(r[s.a])));
                break;
            case Op_Cos:
                r[s.dst] = fromFloat(std::cos(number(r[s.a])));
                break;
            case Op_Atan2:
                r[s.dst] = fromFloat(std::atan2(number(r[s.a]), number(r[s.b])));
                break;
            case Op_Random: {
                const f64 lo = number(r[s.a]);
                const f64 hi = number(r[s.b]);
                const f64 x = lo + (hi - lo) * random01();
                r[s.dst] = r[s.a].kind == NEBULA_KIND_INT && r[s.b].kind == NEBULA_KIND_INT
                               ? fromInt(toInt(std::floor(x)))
                               : fromFloat(x);
                break;
            }
            case Op_SetState:
                pInstance->mNextState = s.imm;
                break;
            case Op_Wait: {
                if (depth != 0) {
                    // A called function can't wait: only a handler's own steps can (the compiler says so too).
                    if (!p->mWaitLogged) {
                        p->mWaitLogged = true;
                        nebula_log("mods: %s: %s (line %u) waits inside a called function; it doesn't wait\n", p->mMod,
                                   p->string(fn.name), fn.line);
                    }
                    break;
                }
                const f64 frames = number(r[s.a]);
                const u32 count = frames <= 0.0 ? 1u : frames >= ::cMaxWait ? ::cMaxWait : static_cast< u32 >(frames);
                addWaiting(pInstance, function, pc, count, r, fn.registers);
                return none();
            }
            case Op_Emit:
                lumacore::scriptEmit(p->string(p->mImports[s.imm].name), r[s.a]);
                break;
            case Op_Format:
                r[s.dst] = format(p->mConstants[s.imm].u.s, r + s.a, s.b);
                break;
            case Op_ToInt:
                r[s.dst] = fromInt(toInt(number(r[s.a])));
                break;
            case Op_ToFloat:
                r[s.dst] = fromFloat(number(r[s.a]));
                break;
            default:
                return none();
            }
        }
        return none();
    }

    // "{}" takes the next value: an int, a float (as written, at most 3 decimals), a vec3, a text, a bool.
    NebulaValue format(const char* pFormat, const NebulaValue* pValues, u32 count) {
        char* const out = sFormatted[sFormatNext];
        sFormatNext = (sFormatNext + 1) % cFormatSlots;
        u32 at = 0;
        u32 next = 0;
        const char* f = pFormat != nullptr ? pFormat : "";
        while (*f != '\0' && at + 1 < cFormatLength) {
            if (f[0] == '{' && f[1] == '}') {
                f += 2;
                const NebulaValue v = next < count ? pValues[next] : none();
                next++;
                const u32 room = cFormatLength - at;
                s32 wrote = 0;
                switch (v.kind) {
                case NEBULA_KIND_BOOL:
                    wrote = nebula_snprintf(out + at, room, "%s", v.u.i != 0 ? "true" : "false");
                    break;
                case NEBULA_KIND_INT:
                    wrote = nebula_snprintf(out + at, room, "%lld", static_cast< long long >(v.u.i));
                    break;
                case NEBULA_KIND_FLOAT:
                    wrote = nebula_snprintf(out + at, room, "%.3g", v.u.f);
                    break;
                case NEBULA_KIND_VEC3:
                    wrote = nebula_snprintf(out + at, room, "(%.3g, %.3g, %.3g)", static_cast< f64 >(v.u.v[0]),
                                            static_cast< f64 >(v.u.v[1]), static_cast< f64 >(v.u.v[2]));
                    break;
                case NEBULA_KIND_TEXT:
                    wrote = nebula_snprintf(out + at, room, "%s", v.u.s != nullptr ? v.u.s : "");
                    break;
                default:
                    wrote = nebula_snprintf(out + at, room, "none");
                    break;
                }
                if (wrote < 0) {
                    break;
                }
                at = static_cast< u32 >(wrote) >= room ? cFormatLength - 1 : at + static_cast< u32 >(wrote);
            } else {
                out[at++] = *f++;
            }
        }
        out[at] = '\0';
        NebulaValue v = none();
        v.kind = NEBULA_KIND_TEXT;
        v.u.s = out;
        return v;
    }

    NebulaValue run(Instance* pInstance, u32 handler, const NebulaValue* pArgs, u32 count) {
        Program* const p = pInstance->mProgram;
        if (handler >= p->mHandlerCount) {
            return none();
        }
        const Handler& h = p->mHandlers[handler];
        if (h.state != cNoState && h.state != pInstance->mState) {
            return none();
        }
        // Handlers take self and at most 7 values (the dispatch points pass fewer).
        NebulaValue args[8];
        args[0] = pInstance->mSelf;
        const u32 n = count < 7 ? count : 7;
        for (u32 i = 0; i < n; i++) {
            args[i + 1] = pArgs[i];
        }
        return callFunction(pInstance, h.function, args, n + 1, 0);
    }

    static void runEvent(Instance* pInstance, u8 event, u32 state) {
        Program* const p = pInstance->mProgram;
        for (u32 i = 0; i < p->mHandlerCount; i++) {
            const Handler& h = p->mHandlers[i];
            if (h.event == event && (h.state == state || (event == HandlerEvent_Frame && h.state == cNoState))) {
                NebulaValue self = pInstance->mSelf;
                (void)callFunction(pInstance, h.function, &self, 1, 0);
            }
        }
    }

    void frame(Instance* pInstance) {
        Program* const p = pInstance->mProgram;
        if (pInstance->mNextState != cNoState) {
            const u32 next = pInstance->mNextState;
            pInstance->mNextState = cNoState;
            if (next != pInstance->mState) {
                const u32 old = pInstance->mState;
                pInstance->mState = next;
                pInstance->mStateFrames = 0;
                if (p->mHasFrame) {
                    runEvent(pInstance, HandlerEvent_Leave, old);
                    runEvent(pInstance, HandlerEvent_Enter, next);
                }
            }
        }
        if (pInstance->mWaitingCount != 0) {
            // Count down every wait there was at the start of this frame; the finished ones leave the list first, then
            // continue (one that waits again joins the list for the next frame).
            Waiting due[16];
            u32 dueCount = 0;
            u32 kept = 0;
            for (u32 i = 0; i < pInstance->mWaitingCount; i++) {
                Waiting& w = pInstance->mWaiting[i];
                if (--w.frames == 0 && dueCount < 16) {
                    due[dueCount++] = w;
                } else {
                    if (w.frames == 0) {
                        w.frames = 1;  // more than 16 finish together: the rest go next frame
                    }
                    pInstance->mWaiting[kept++] = w;
                }
            }
            pInstance->mWaitingCount = kept;
            for (u32 i = 0; i < dueCount; i++) {
                (void)execute(pInstance, due[i].function, due[i].pc, due[i].registers, 0);
                delete[] due[i].registers;
                delete[] due[i].matrices;
            }
        }
        if (p->mHasFrame) {
            runEvent(pInstance, HandlerEvent_Frame, pInstance->mState);
        }
        pInstance->mStateFrames++;
    }

    void beginFrame() {
        lumacore::forEachScriptProgram([](Program* p) {
            p->mStepsThisFrame = 0;
        });
        sFormatNext = 0;
    }
};  // namespace lumascript
