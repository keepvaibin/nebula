#include "Game/AreaObj/AreaObjContainer.hpp"
#include "Game/AreaObj/AreaObj.hpp"
#include "Game/AreaObj/CubeCamera.hpp"
#include "Game/AreaObj/GlaringLightArea.hpp"
#include "Game/AreaObj/ImageEffectArea.hpp"
#include "Game/AreaObj/LightAreaHolder.hpp"
#include "Game/AreaObj/WarpCube.hpp"
#include "Game/AreaObj/WaterArea.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/StringUtil.hpp"
#include <algorithm>
#include "Game/System/LumaCore.hpp"

namespace {
    struct IsManagerName {
        bool operator()(const AreaObjMgr* pManager, const char* pName) const {
            return MR::isHeadString(pName, pManager->mName);
        }
    };

    template < typename T >
    AreaObjMgr* createManager(s32 numMax, const char* pName) {
        return new T(numMax, pName);
    }

    const Name2CreateManagerFunc cCreateTable[] = {
        {
            "SwitchArea",
            0x40,
            createManager< AreaObjMgr >,
        },
        {
            "RaceJudgeCube",
            0x40,
            createManager< AreaObjMgr >,
        },
        {
            "NinForceWindCube",
            0x40,
            createManager< AreaObjMgr >,
        },
        {
            "NinAbyssCube",
            0x40,
            createManager< AreaObjMgr >,
        },
        {
            "CubeCamera",
            0xA0,
            createManager< CubeCameraMgr >,
        },
        {
            "BindEndCube",
            0x40,
            createManager< AreaObjMgr >,
        },
        {
            "EffectCylinder",
            0x40,
            createManager< AreaObjMgr >,
        },
        {
            "DeathArea",
            0x40,
            createManager< AreaObjMgr >,
        },
        {
            "WarpCube",
            0x40,
            createManager< WarpCubeMgr >,
        },
        {
            "TripodBossStepStart",
            0x40,
            createManager< AreaObjMgr >,
        },
        {
            "Water",
            0x40,
            createManager< WaterAreaMgr >,
        },
        {
            "PlaneModeCube",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "PlaneCircularModeCube",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "PipeModeCube",
            0x4,
            createManager< AreaObjMgr >,
        },
        {
            "TowerModeCylinder",
            0x4,
            createManager< AreaObjMgr >,
        },
        {
            "ShadeCube",
            0x40,
            createManager< AreaObjMgr >,
        },
        {
            "PullBackCube",
            0x40,
            createManager< AreaObjMgr >,
        },
        {
            "PullBackCylinder",
            0x40,
            createManager< AreaObjMgr >,
        },
        {
            "RestartCube",
            0x40,
            createManager< AreaObjMgr >,
        },
        {
            "ChangeBgmCube",
            0x20,
            createManager< AreaObjMgr >,
        },
        {
            "BgmProhibitArea",
            0x4,
            createManager< AreaObjMgr >,
        },
        {
            "SoundEmitterCube",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "SoundEmitterSphere",
            0x8,
            createManager< AreaObjMgr >,
        },
        {
            "PlaneCollisionCube",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "ForbidTriangleJumpCube",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "ForbidWaterSearchCube",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "QuakeEffectArea",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "HazeCube",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "AudioEffectArea",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "BigBubbleGoalArea",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "SunLightArea",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "GlaringLightArea",
            0x40,
            createManager< GlaringLightAreaMgr >,
        },
        {
            "ViewGroupCtrlCube",
            0x40,
            createManager< AreaObjMgr >,
        },
        {
            "LensFlareArea",
            0x40,
            createManager< AreaObjMgr >,
        },
        {
            "CameraRepulsiveArea",
            0x80,
            createManager< AreaObjMgr >,
        },
        {
            "LightArea",
            0x80,
            createManager< LightAreaHolder >,
        },
        {
            "FallsCube",
            0x20,
            createManager< AreaObjMgr >,
        },
        {
            "MercatorCube",
            0x1,
            createManager< AreaObjMgr >,
        },
        {
            "AstroChangeStageCube",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "ImageEffectArea",
            0x20,
            createManager< ImageEffectAreaMgr >,
        },
        {
            "BlueStarGuidanceCube",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "TicoSeedGuidanceCube",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "MessageArea",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "SmokeEffectColorArea",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "BeeWallShortDistArea",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "ExtraWallCheckArea",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "ExtraWallCheckCylinder",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "SpinGuidanceCube",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "HipDropGuidanceCube",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "TamakoroMoveGuidanceCube",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "TamakoroJumpGuidanceCube",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "BigBubbleGuidanceCube",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "HeavySteeringCube",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "NonSleepCube",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "AreaMoveSphere",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "DodoryuClosedCylinder",
            0x8,
            createManager< AreaObjMgr >,
        },
        {
            "DashChargeCylinder",
            0x8,
            createManager< AreaObjMgr >,
        },
        {
            "PlayerSeArea",
            0x8,
            createManager< AreaObjMgr >,
        },
        {
            "RasterScrollCube",
            0x8,
            createManager< AreaObjMgr >,
        },
        {
            "OnimasuCube",
            0x20,
            createManager< AreaObjMgr >,
        },
        {
            "ForbidJumpCube",
            0x8,
            createManager< AreaObjMgr >,
        },
        {
            "CollisionArea",
            0x40,
            createManager< AreaObjMgr >,
        },
        {
            "AstroOverlookArea",
            0x8,
            createManager< AreaObjMgr >,
        },
        {
            "CelestrialSphere",
            0x4,
            createManager< AreaObjMgr >,
        },
        {
            "MirrorArea",
            0x10,
            createManager< AreaObjMgr >,
        },
        {
            "DarkMatterCube",
            0x40,
            createManager< AreaObjMgr >,
        },
        {
            "DarkMatterCylinder",
            0x20,
            createManager< AreaObjMgr >,
        },
        // LumaCore's manager for mods' own areas (the factory's LumaAreaCube, LumaAreaSphere, LumaAreaCylinder): no game
        // code asks for it, so a mod's area does nothing a game area of its maker's name would (pull back, move, ...).
        {
            "LumaArea",
            0x40,
            createManager< AreaObjMgr >,
        },
    };
};  // namespace

AreaObjContainer::AreaObjContainer(const char* pName) : NameObj(pName), mIndex(), mHashesShared(false) {
    lumacore::setClass(this, lumacore::kClassId< AreaObjContainer >);
}

void AreaObjContainer::init(const TableIter& /*rIter*/) {
    mManagers.init(ARRAY_SIZE(::cCreateTable));
    mIndex.reset(ARRAY_SIZE(::cCreateTable));
    for (u32 i = 0; i < ARRAY_SIZE(::cCreateTable); i++) {
        const Name2CreateManagerFunc* pName2CreateManagerFunc = &::cCreateTable[i];
        AreaObjMgr* mgr = pName2CreateManagerFunc->mCreateFunc(pName2CreateManagerFunc->mFirstCapacity,pName2CreateManagerFunc->mName);
        mgr->init(TableIter());
        mManagers.push_back(mgr);
        mIndex.add(pName2CreateManagerFunc->mName, i);
    }

    // findHash finds only the first of a hash.
    for (u32 i = 0; i < ARRAY_SIZE(::cCreateTable); i++) {
        if (mIndex.findHash(MR::getHashCode(::cCreateTable[i].mName)) != i) {
            mHashesShared = true;
        }
    }
}

u32 AreaObjContainer::getKindNum() {
    return ARRAY_SIZE(::cCreateTable);
}

const char* AreaObjContainer::getKindName(u32 index) {
    return ::cCreateTable[index].mName;
}

// The first manager in table order whose name heads pName; null for none. Each prefix of pName is looked up by its
// hash, taken a character at a time as MR::getHashCode steps.
AreaObjMgr* AreaObjContainer::getManager(const char* pName) const {
    if (mHashesShared) {
        AreaObjMgr* const* ppManager =
            std::find_if(mManagers.begin(), mManagers.end(), [&](const auto& item) { return IsManagerName()(item, pName); });
        return ppManager != mManagers.end() ? *ppManager : nullptr;
    }

    u32 best = MR::NameIndex::cNoValue;
    u32 hash = 0;
    for (u32 length = 1; pName[length - 1] != '\0'; length++) {
        hash = static_cast< s8 >(pName[length - 1]) + hash * 31;
        const u32 index = mIndex.findHash(hash);
        if (index < best) {
            const char* pManagerName = mManagers[static_cast< int >(index)]->mName;
            if (pManagerName[length] == '\0' && MR::isHeadString(pName, pManagerName)) {
                best = index;
            }
        }
    }

    return best == MR::NameIndex::cNoValue ? nullptr : mManagers[static_cast< int >(best)];
}

AreaObj* AreaObjContainer::getAreaObj(const char* pName, const Vec3f& rVec) const {
    AreaObjMgr* pManager = getManager(pName);
    return pManager != nullptr ? pManager->find_in(rVec) : nullptr;
}

namespace MR {
    AreaObjContainer* getAreaObjContainer() {
        return MR::getSceneObj< AreaObjContainer >(SceneObj_AreaObjContainer);
    }
};  // namespace MR
