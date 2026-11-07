#pragma once

class TableIter;
class LiveActor;

namespace MR {
    class FunctorBase;
};  // namespace MR

namespace AstroDemoFunction {
    int getOpenedAstroDomeNum();
    const char* getGrandStarReturnDemoName(int);
    int getActiveGrandStarReturnDemoIndex();
    void tryRegisterDemo(LiveActor*, const char*, const TableIter&);
    void tryRegisterAstroDemoAll(LiveActor*, const TableIter&);
    void tryRegisterGrandStarReturn(LiveActor*, const TableIter&);
    void tryRegisterGrandStarReturnWithFunction(LiveActor*, const TableIter&, const MR::FunctorBase&);
    void tryRegisterGrandStarReturnAndSimpleCast(LiveActor*, const TableIter&);
    void tryRegisterGrandStarReturnWithFunctionAndSimpleCast(LiveActor*, const TableIter&, const MR::FunctorBase&);
    bool tryRegisterSimpleCastIfAstroGalaxy(LiveActor*);
    void tryRegisterDemoForTico(LiveActor*, const TableIter&);
    void tryRegisterDemoForLuigiAndKinopio(LiveActor*, const TableIter&);
};  // namespace AstroDemoFunction
