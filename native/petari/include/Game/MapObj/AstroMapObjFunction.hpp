#pragma once

#include <nebula_geometry.hpp>

class GalaxyNamePlate;
class Table;
class TableIter;
class LiveActor;

namespace MR {
    class FunctorBase;
};  // namespace MR

class AstroMapObjFunction {
public:
    static bool isAlreadyRevival(const char*, s32);
    static bool isEnableRevival(const char*, s32);
    static bool isAlreadyOpen(const char*, s32);
    static bool isEnableOpen(const char*, s32);
    static s32 getDomeIdFromArg0(const TableIter&);
    static const char* getModelName(const char*, s32);
    static const Table* createAstroNamePlateParser();
    static bool trySetAndShowNamePlate(GalaxyNamePlate*, const Table* pInfo, const Vec3f& rPos, const char* pObjName, s32 domeId,
                                       bool needsOpen);
    static bool tryRegisterMultiDemoAndFunction(const char*, LiveActor*, const TableIter&, const MR::FunctorBase&);
    static void initilizePeculiar(const char*, LiveActor*, const TableIter&);
};
