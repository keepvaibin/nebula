#include "Game/Map/PlanetMapCreator.hpp"
#include "Game/Map.hpp"
#include "Game/MapObj.hpp"
#include "Game/NameObj/NameObjFactory.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    static const char* const sArcName = "PlanetMapDataTable";
    static const char* const sFileName = "PlanetMapDataTable.bcsv";

    void makeSubModelName(const char** ppDst, const Table* pIter, s32 idx, const char* pKey, const char* pSuffix) {
        s32 data;
        MR::getCsvDataS32(&data, pIter, pKey, idx);

        const char* pPlanetName;
        MR::getCsvDataStr(&pPlanetName, pIter, "PlanetName", idx);

        if (data != 0) {
            s32 size = strlen(pPlanetName) + strlen(pSuffix) + 1;
            char* buf = new char[size];
            nebula_snprintf(buf, size, "%s%s", pPlanetName, pSuffix);

            *ppDst = buf;
        } else {
            *ppDst = nullptr;
        }
    }

    static const PlanetEntry sUniquePlanetCreateFuncTable[] = {
        {
            "BeamGoRoundPlanet",
            createNameObj< BeamGoRoundPlanet >,
        },
        {
            "BumpAppearPlanet",
            createNameObj< BumpAppearPlanet >,
        },
        {
            "ChoConveyorPlanetB",
            createNameObj< RailPlanetMap >,
        },
        {
            "ChoConveyorPlanetD",
            createNameObj< RotateMoveObj >,
        },
        {
            "DinoPackunBattlePlanet",
            createNameObj< FurPlanetMap >,
        },
        {
            "DarkHopperPlanetA",
            createNameObj< SimpleClipPartsObj >,
        },
        {
            "DarkHopperPlanetB",
            createNameObj< SimpleClipPartsObj >,
        },
        {
            "DarkHopperPlanetC",
            createNameObj< SimpleClipPartsObj >,
        },
        {
            "DarkHopperPlanetD",
            createNameObj< SimpleClipPartsObj >,
        },
        {
            "DarknessRoomPlanet",
            createNameObj< SimpleClipPartsObj >,
        },
        {
            "FlagDiscPlanetB",
            createNameObj< PlanetMapFarClippable >,
        },
        {
            "FlagDiscPlanetC",
            createNameObj< PlanetMapFarClippable >,
        },
        {
            "FlagDiscPlanetD",
            createNameObj< RotateMoveObj >,
        },
        {
            "FlagDiscPlanetE",
            createNameObj< PlanetMapFarClippable >,
        },
        {
            "HatchWaterPlanet",
            createNameObj< HatchWaterPlanet >,
        },
        {
            "HeavensDoorInsidePlanet",
            createNameObj< SimpleMapObj >,
        },
        {
            "HoneyQueenPlanet",
            createNameObj< FurPlanetMap >,
        },
        {
            "LavaJamboSunPlanet",
            createNameObj< LavaJamboSunPlanet >,
        },
        {
            "LavaRotatePlanet",
            createNameObj< LavaSunPlanet >,
        },
        {
            "LavaDomedPlanet",
            createNameObj< LavaSunPlanet >,
        },
        {
            "MarblePlanet",
            createNameObj< MarblePlanet >,
        },
        {
            "PeachCastleGardenPlanet",
            createNameObj< PeachCastleGardenPlanet >,
        },
        {
            "SandCapsuleInsidePlanet",
            createNameObj< SandCapsuleInsidePlanet >,
        },
        {
            "ScaleDownRelayPlanet",
            createNameObj< FlexibleSphere >,
        },
        {
            "SnowCapsulePlanet",
            createNameObj< SnowCapsulePlanet >,
        },
        {
            "TeresaRoomPlanet",
            createNameObj< PlanetMap >,
        },
        {
            "TridentPlanet",
            createNameObj< AstroSimpleObj >,
        },
        {
            "QuestionBoxPlanetA",
            createNameObj< QuestionBoxGalleryObj >,
        },
        {
            "QuestionBoxPlanetB",
            createNameObj< QuestionBoxGalleryObj >,
        },
        {
            "QuestionBoxPlanetC",
            createNameObj< QuestionBoxGalleryObj >,
        },
        {
            "QuestionBoxPlanetD",
            createNameObj< QuestionBoxGalleryObj >,
        },
        {
            "QuestionBoxPlanetE",
            createNameObj< QuestionBoxGalleryObj >,
        },
        {
            "Quicksand2DPlanet",
            createNameObj< RailPlanetMap >,
        },
        {
            "ReverseGravityRoomPlanet",
            createNameObj< ReverseGravityRoomPlanet >,
        },
        {
            "SandStreamHighTowerPlanet",
            createNameObj< RailPlanetMap >,
        },
        {
            "SandStreamJointPlanetA",
            createNameObj< RailPlanetMap >,
        },
        {
            "SandStreamJointPlanetB",
            createNameObj< RailPlanetMap >,
        },
        {
            "StarDustStartPlanet",
            createNameObj< RotateMoveObj >,
        },
        {
            "WormEatenPlanet",
            createNameObj< WormEatenPlanet >,
        },
    };

    const PlanetEntry* findUniquePlanetEntry(const char* pName) {
        for (u32 i = 0; i < ARRAY_SIZE(::sUniquePlanetCreateFuncTable); i++) {
            const PlanetEntry* pEntry = &::sUniquePlanetCreateFuncTable[i];
            if (MR::isEqualString(pName, pEntry->mName)) {
                return pEntry;
            }
        }

        return nullptr;
    }

    static bool isDataForceLow(const PlanetMapData* pMapData) {
        for (s32 i = 0; i < pMapData->mForceScenarioNum; i++) {
            if (!MR::isEqualString(pMapData->mForceScenarioData[i], "Low")) {
                return true;
            }
        }

        return false;
    }
};  // namespace

PlanetMapCreator::PlanetMapCreator(const char* pName) : NameObj(pName), mPlanetMapData(), mIndex() {
    lumacore::setClass(this, lumacore::kClassId< PlanetMapCreator >);
    Table* pIter = MR::createCsvParser(::sArcName, ::sFileName);

    mTableCount = MR::getCsvDataElementNum(pIter);
    mPlanetMapData = new PlanetMapData*[mTableCount];
    createPlanetMapDataTable(pIter);
}

CreatorFuncPtr PlanetMapCreator::getCreateFunc(const char* pName) {
    if (isScenarioForceLow(getTableData(pName))) {
        return createNameObj< PlanetMapWithoutHighModel >;
    }

    const PlanetEntry* pEntry = ::findUniquePlanetEntry(pName);

    if (pEntry != nullptr) {
        return pEntry->mCreateFunc;
    }

    return createNameObj< PlanetMap >;
}

void PlanetMapCreator::createPlanetMapDataTable(Table* pInfo) {
    // ForceLowScenarioName0, 1, ... up to the first the table lacks.
    s32 forceNum = 0;
    while (pInfo->searchItemInfo(MR::FormattedName("ForceLowScenarioName%d", forceNum).c_str()) >= 0) {
        forceNum++;
    }

    mIndex.reset(static_cast< u32 >(mTableCount));
    for (int i = 0; i < mTableCount; i++) {
        mPlanetMapData[i] = new PlanetMapData();
        mPlanetMapData[i]->mForceScenarioNum = forceNum;

        addTableData(pInfo, i);
        if (mPlanetMapData[i]->mPlanetName != nullptr) {
            mIndex.add(mPlanetMapData[i]->mPlanetName, static_cast< u32 >(i));
        }
    }
}

void PlanetMapCreator::addTableData(const Table* pInfo, s32 idx) {
    PlanetMapData* curData = mPlanetMapData[idx];
    MR::getCsvDataStr(&curData->mPlanetName, pInfo, "PlanetName", idx);
    ::makeSubModelName(&curData->mData[0], pInfo, idx, "LowFlag", "Low");
    ::makeSubModelName(&curData->mData[1], pInfo, idx, "MiddleFlag", "Middle");
    ::makeSubModelName(&curData->mData[2], pInfo, idx, "BloomFlag", "Bloom");
    ::makeSubModelName(&curData->mData[3], pInfo, idx, "WaterFlag", "Water");
    ::makeSubModelName(&curData->mData[4], pInfo, idx, "IndirectFlag", "Indirect");

    for (int i = 0; i < 5; i++) {
        if (curData->mData[i] != nullptr) {
            if (!MR::isExistModel(curData->getData(i))) {
                curData->setData(i, nullptr);
            }
        }
    }

    curData->mForceScenarioData = new const char*[curData->mForceScenarioNum];
    for (s32 i = 0; i < curData->mForceScenarioNum; i++) {
        MR::getCsvDataStr(&curData->mForceScenarioData[i], pInfo, MR::FormattedName("ForceLowScenarioName%d", i).c_str(), idx);
    }
}

PlanetMapData* PlanetMapCreator::getTableData(const char* pName) const {
    const u32 row = mIndex.find(pName);
    return row != MR::NameIndex::cNoValue ? mPlanetMapData[row] : nullptr;
}

bool PlanetMapCreator::isScenarioForceLow(const PlanetMapData* pData) const {
    // A planet the table does not list (a mod's) has no forced scenarios.
    if (pData == nullptr) {
        return false;
    }

    bool isForce = ::isDataForceLow(pData);

    if (!isForce) {
        return false;
    }

    MR::FormattedName name("%s_%d", MR::getCurrentStageName(), MR::getCurrentScenarioNo());

    for (s32 i = 0; i < pData->mForceScenarioNum; i++) {
        if (MR::isEqualString(pData->mForceScenarioData[i], "Low")) {
            return false;
        }

        if (MR::isEqualString(name.c_str(), pData->mForceScenarioData[i])) {
            return true;
        }
    }

    return false;
}

CreatorFuncPtr PlanetMapCreatorFunction::getPlanetMapCreator(const char* pName) {
    return MR::getSceneObj< PlanetMapCreator >(SceneObj_PlanetMapCreator)->getCreateFunc(pName);
}

bool PlanetMapCreatorFunction::isLoadArchiveAfterScenarioSelected(const char* pArchive) {
    const PlanetMapData* pData = MR::getSceneObj< PlanetMapCreator >(SceneObj_PlanetMapCreator)->getTableData(pArchive);

    if (pData == nullptr) {
        return false;
    }

    return ::isDataForceLow(pData);
}

bool PlanetMapCreatorFunction::isRegisteredObj(const char* pArchive) {
    return MR::getSceneObj< PlanetMapCreator >(SceneObj_PlanetMapCreator)->getTableData(pArchive) != nullptr;
}
