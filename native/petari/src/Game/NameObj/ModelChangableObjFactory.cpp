#include "Game/NameObj/ModelChangableObjFactory.hpp"
#include "Game/Boss/TripodBossFixParts.hpp"
#include "Game/Boss/TripodBossRailMoveParts.hpp"
#include "Game/Boss/TripodBossRotateParts.hpp"
#include "Game/Map/SunshadeMapParts.hpp"
#include "Game/MapObj/AssemblyBlock.hpp"
#include "Game/MapObj/ClipFieldMapParts.hpp"
#include "Game/MapObj/FlexibleSphere.hpp"
#include "Game/MapObj/MercatorFixParts.hpp"
#include "Game/MapObj/MercatorRailMoveParts.hpp"
#include "Game/MapObj/MercatorRotateParts.hpp"
#include "Game/MapObj/SimpleNormalMapObj.hpp"
#include "Game/NameObj/NameObjFactory.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstring>

namespace {
    const Model2CreateFunc cModelChangableObjCreatorTable[] = {
        {
            nullptr,
            "AssemblyBlock",
            createNameObj< AssemblyBlock >,
        },
        {
            nullptr,
            "ClipFieldMapParts",
            createNameObj< ClipFieldMapParts >,
        },
        {
            nullptr,
            "FlexibleSphere",
            createNameObj< FlexibleSphere >,
        },
        {
            nullptr,
            "MercatorFixParts",
            createNameObj< MercatorFixParts >,
        },
        {
            nullptr,
            "MercatorRailMoveParts",
            createNameObj< MercatorRailMoveParts >,
        },
        {
            nullptr,
            "MercatorRotateParts",
            createNameObj< MercatorRotateParts >,
        },
        {
            nullptr,
            "TripodBossFixParts",
            createNameObj< TripodBossFixParts >,
        },
        {
            nullptr,
            "TripodBossRailMoveParts",
            createNameObj< TripodBossRailMoveParts >,
        },
        {
            nullptr,
            "TripodBossRotateParts",
            createNameObj< TripodBossRotateParts >,
        },
        {
            nullptr,
            "TripodBossRotateParts",
            createNameObj< TripodBossRotateParts >,
        },
        {
            nullptr,
            "SimpleNormalMapObj",
            createNameObj< SimpleNormalMapObj >,
        },
        {
            nullptr,
            "SunshadeMapParts",
            MR::createSunshadeMapParts,
        },
    };

    const Model2CreateFunc* getModel2CreateFunc(const char* pName) {
        for (u32 i = 0; i < ARRAY_SIZE(cModelChangableObjCreatorTable); i++) {
            const Model2CreateFunc* pModel2CreateFunc = &cModelChangableObjCreatorTable[i];
            const bool isMatch = pModel2CreateFunc->mNamePrefix != nullptr
                                     ? MR::isEqualStringPrefix(pName, pModel2CreateFunc->mNamePrefix)
                                     : MR::isEqualString(pName, pModel2CreateFunc->mArchiveName);

            if (isMatch) {
                return pModel2CreateFunc;
            }
        }

        return nullptr;
    }
};  // namespace

namespace MR {
    CreatorFuncPtr getModelChangableObjCreator(const char* pName) {
        const Model2CreateFunc* pModel2CreateFunc = ::getModel2CreateFunc(pName);

        if (pModel2CreateFunc != nullptr) {
            return pModel2CreateFunc->mCreatorFunc;
        }

        return nullptr;
    }
};  // namespace MR
