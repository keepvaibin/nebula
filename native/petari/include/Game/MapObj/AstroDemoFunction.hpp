#pragma once

#include <revolution/types.h>

class JMapInfoIter;
class LiveActor;

namespace MR {
    class FunctorBase;
};  // namespace MR

#ifdef TARGET_PC
// The functions are defined in namespace AstroDemoFunction (Game/Demo/AstroDemoFunction.cpp); the MSVC ABI mangles
// a class's static members apart from a namespace's functions. No caller uses the results.
#include "Game/Demo/AstroDemoFunction.hpp"
#else
class AstroDemoFunction {
public:
    static s32 getOpenedAstroDomeNum();
    static const char* getGrandStarReturnDemoName(int);

    static bool tryRegisterDemo(LiveActor*, const char*, const JMapInfoIter&);
    static bool tryRegisterGrandStarReturnAndSimpleCast(LiveActor*, const JMapInfoIter&);
    static bool tryRegisterGrandStarReturnWithFunctionAndSimpleCast(LiveActor*, const JMapInfoIter&, const MR::FunctorBase&);
};
#endif
