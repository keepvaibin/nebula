#include "Game/MapObj/AstroMapObjFunction.hpp"
#include "Game/Demo/AstroDemoFunction.hpp"
#include "Game/Screen/GalaxyNamePlate.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"

namespace {
    inline s32 findModelName(const Table* pInfo, const char* pModelName) {
        s32 num = MR::getCsvDataElementNum(pInfo);

        for (s32 i = 0; i < num; i++) {
            const char* pCsvModelName;
            MR::getCsvDataStr(&pCsvModelName, pInfo, "ModelName", i);

            if (MR::isEqualString(pModelName, pCsvModelName)) {
                return i;
            }
        }

        return -1;
    }

    const char* const cAstroNamePlateArcName = "AstroNamePlateData";
    const char* const cAstroNamePlateFileName = "AstroNamePlateData.bcsv";
    const char* const cAstroDomeNameTable[] = {"AstroDomeObservatory", "AstroDomeWell",    "AstroDomeKitchen",
                                               "AstroDomeBedRoom",     "AstroDomeMachine", "AstroDomeTower"};
    const char* const cAstroDomeSkyNameTable[] = {"AstroDomeSkyA", "AstroDomeSkyB", "AstroDomeSkyC", "AstroDomeSkyA", "AstroDomeSkyB", "AstroDomeSkyC"};
    const char* const cAstroDomeEntranceNameTable[] = {"AstroDomeEntranceObservatory", "AstroDomeEntranceWell",    "AstroDomeEntranceKitchen",
                                                       "AstroDomeEntranceBedRoom",     "AstroDomeEntranceMachine", "AstroDomeEntranceTower"};
    const char* const cAstroStarPlateNameTable[] = {"AstroStarPlateObservatory", "AstroStarPlateWell",    "AstroStarPlateKitchen",
                                                    "AstroStarPlateBedRoom",     "AstroStarPlateMachine", "AstroStarPlateTower"};

    s32 getNumGrandStarForRevival(const char* pObjName, s32 domeId) {
        const char* modelName = AstroMapObjFunction::getModelName(pObjName, domeId);

        if (MR::isEqualString(modelName, "AstroDomeEntranceObservatory")) {
            return 1;
        }

        if (MR::isEqualString(modelName, "AstroDomeEntranceWell")) {
            return 1;
        }

        if (MR::isEqualString(modelName, "AstroBaseA")) {
            return 1;
        }

        if (MR::isEqualString(modelName, "AstroBaseCenterB")) {
            return 1;
        }

        if (MR::isEqualString(modelName, "AstroRotateStepA")) {
            return 1;
        }

        if (MR::isEqualString(modelName, "AstroDecoratePartsA")) {
            return 1;
        }

        if (MR::isEqualString(modelName, "AstroDecoratePartsGearA")) {
            return 1;
        }

        if (MR::isEqualString(modelName, "AstroParking")) {
            return 1;
        }

        if (MR::isEqualString(modelName, "AstroDomeEntranceKitchen")) {
            return 3;
        }

        if (MR::isEqualString(modelName, "AstroDomeEntranceBedRoom")) {
            return 3;
        }

        if (MR::isEqualString(modelName, "AstroBaseB")) {
            return 3;
        }

        if (MR::isEqualString(modelName, "AstroBaseKitchen")) {
            return 3;
        }

        if (MR::isEqualString(modelName, "AstroLibrary")) {
            return 3;
        }

        if (MR::isEqualString(modelName, "AstroCountDownPlate")) {
            return 5;
        }

        if (MR::isEqualString(modelName, "AstroDomeEntranceMachine")) {
            return 5;
        }

        if (MR::isEqualString(modelName, "AstroDomeEntranceTower")) {
            return 5;
        }

        if (MR::isEqualString(modelName, "AstroBaseC")) {
            return 5;
        }

        if (MR::isEqualString(modelName, "AstroBaseCenterA")) {
            return 5;
        }

        if (MR::isEqualString(modelName, "AstroBaseCenterTop")) {
            return 5;
        }

        if (MR::isEqualString(modelName, "AstroRotateStepB")) {
            return 5;
        }

        s32 num = -1;

        if (domeId != -1) {
            num = domeId;
        }

        return num;
    }

    s32 getNumGrandStarForOpen(const char* pObjName, s32 domeId) {
        s32 num = -1;

        if (domeId != -1) {
            num = domeId;
        }

        return num;
    }
};  // namespace

bool AstroMapObjFunction::isAlreadyRevival(const char* pObjName, s32 domeId) {
    if (MR::isEqualString(pObjName, "AstroChildRoom")) {
        return MR::isOnGameEventFlagChildsRoom();
    }

    if (MR::isEqualString(pObjName, "TridentPlanet")) {
        return MR::isOnGameEventFlagGreenDriver();
    }

    s32 openedNum = AstroDemoFunction::getOpenedAstroDomeNum();

    return openedNum >= ::getNumGrandStarForRevival(pObjName, domeId);
}

bool AstroMapObjFunction::isEnableRevival(const char* pObjName, s32 domeId) {
    if (MR::isEqualString(pObjName, "AstroChildRoom")) {
        return true;
    }

    s32 openedNum = AstroDemoFunction::getOpenedAstroDomeNum();

    return ::getNumGrandStarForRevival(pObjName, domeId) == openedNum + 1;
}

bool AstroMapObjFunction::isAlreadyOpen(const char* pObjName, s32 domeId) {
    if (MR::isEqualString(pObjName, "AstroLibrary")) {
        return MR::isOnGameEventFlagLibraryRoom();
    }

    s32 openedNum = AstroDemoFunction::getOpenedAstroDomeNum();

    return openedNum >= ::getNumGrandStarForOpen(pObjName, domeId);
}

bool AstroMapObjFunction::isEnableOpen(const char* pObjName, s32 domeId) {
    s32 openedNum = AstroDemoFunction::getOpenedAstroDomeNum();

    return ::getNumGrandStarForOpen(pObjName, domeId) == openedNum + 1;
}

s32 AstroMapObjFunction::getDomeIdFromArg0(const TableIter& rIter) {
    s32 domeId = -1;
    MR::getPlacementArg0NoInit(rIter, &domeId);

    return domeId;
}

const char* AstroMapObjFunction::getModelName(const char* pObjName, s32 domeId) {
    const char* const* nameTable = nullptr;

    if (MR::isEqualString(pObjName, "AstroDome")) {
        nameTable = ::cAstroDomeNameTable;
    } else if (MR::isEqualString(pObjName, "AstroDomeSky")) {
        nameTable = ::cAstroDomeSkyNameTable;
    } else if (MR::isEqualString(pObjName, "AstroDomeEntrance")) {
        nameTable = ::cAstroDomeEntranceNameTable;
    } else if (MR::isEqualString(pObjName, "AstroStarPlate")) {
        nameTable = ::cAstroStarPlateNameTable;
    }

    if (nameTable != nullptr && domeId >= 1 && domeId <= 6) {
        return nameTable[domeId - 1];
    }
    // A dome past the game's six: its model is named by its number.
    if (nameTable != nullptr) {
        static MR::GrowBuffer< char > sName;  // zero-initialised: the last name made
        const MR::FormattedName name("%s%d", pObjName, domeId);
        const u32 size = strlen(name.c_str()) + 1;
        memcpy(sName.room(size), name.c_str(), size);
        return sName.mItems;
    }

    return pObjName;
}

const Table* AstroMapObjFunction::createAstroNamePlateParser() {
    return MR::createCsvParser(::cAstroNamePlateArcName, ::cAstroNamePlateFileName);
}

bool AstroMapObjFunction::trySetAndShowNamePlate(GalaxyNamePlate* pNamePlate, const Table* pInfo, const Vec3f& rPos,
                                                 const char* pObjName, s32 domeId, bool needsOpen) {
    AreaObj* astroOverlookAreaObj = MR::getCurrentAstroOverlookAreaObj();

    if (astroOverlookAreaObj == nullptr) {
        return false;
    }

    if (!needsOpen && !isAlreadyRevival(pObjName, domeId)) {
        return false;
    }

    if (needsOpen && !isAlreadyOpen(pObjName, domeId)) {
        return false;
    }

    const s32 areaArg = MR::getAreaObjArg(astroOverlookAreaObj, 0);
    const MR::FormattedName offsetYId("OffsetY%d", areaArg);
    const MR::FormattedName upperFlagId("UpperFlag%d", areaArg);

    const char* modelName = getModelName(pObjName, domeId);
    s32 i = ::findModelName(pInfo, modelName);

    f32 offsetY;
    MR::getCsvDataF32(&offsetY, pInfo, offsetYId.c_str(), i);

    bool upperFlag;
    MR::getCsvDataBool(&upperFlag, pInfo, upperFlagId.c_str(), i);

    Vec3f pos = rPos;
    pos.y += offsetY;

    pNamePlate->setPos3D(pos);

    const char16_t* msg;

    if (MR::isEqualString(pObjName, "AstroDomeEntrance")) {
        msg = MR::getScenarioNameOnCurrentLanguage("AstroDome", domeId);
    } else if (MR::isEqualString(pObjName, "AstroLibrary")) {
        msg = MR::getGameMessageDirect("AstroName_LibraryRoom");
    } else if (MR::isEqualString(pObjName, "AstroChildRoom")) {
        msg = MR::getGameMessageDirect("AstroName_ChildRoom");
    } else if (MR::isEqualString(pObjName, "AstroParking")) {
        msg = MR::getGameMessageDirect("AstroName_ParkingLot");
    } else if (MR::isEqualString(pObjName, "TridentPlanet")) {
        msg = MR::getGameMessageDirect("AstroName_Trident");
    } else {
        msg = nullptr;
    }

    pNamePlate->show(msg, upperFlag);

    return true;
}

bool AstroMapObjFunction::tryRegisterMultiDemoAndFunction(const char* pObjName, LiveActor* pActor, const TableIter& rIter,
                                                          const MR::FunctorBase& rFunctor) {
    const char* modelName = getModelName(pObjName, getDomeIdFromArg0(rIter));
    const char* demoName = nullptr;

    if (MR::isEqualString(modelName, ::cAstroDomeEntranceNameTable[1])) {
        demoName = "グランドスター１帰還";
    } else if (MR::isEqualString(modelName, ::cAstroDomeEntranceNameTable[3])) {
        demoName = "グランドスター３帰還";
    } else if (MR::isEqualString(modelName, ::cAstroDomeEntranceNameTable[5])) {
        demoName = "グランドスター５帰還";
    }

    if (demoName != nullptr && MR::isDemoExist(demoName) && MR::tryRegisterDemoCast(pActor, demoName, rIter)) {
        MR::tryRegisterDemoActionFunctorDirect(pActor, rFunctor, demoName, nullptr);

        return true;
    }

    return false;
}

void AstroMapObjFunction::initilizePeculiar(const char* pObjName, LiveActor* pActor, const TableIter& rIter) {
    s32 domeId = getDomeIdFromArg0(rIter);

    if (!MR::isEqualString(getModelName(pObjName, domeId), ::cAstroDomeEntranceNameTable[0])) {
        return;
    }

    MR::needStageSwitchWriteA(pActor, rIter);

    if (MR::isKinopioExplorerRescued()) {
        return;
    }

    MR::onSwitchA(pActor);
}
