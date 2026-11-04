#include "Game/MapObj/DummyDisplayModel.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/MapObj/CoinRotater.hpp"
#include "Game/MapObj/PowerStar.hpp"
#include "Game/NameObj/NameObjArchiveListCollector.hpp"
#include "Game/Util.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>
#include <JSystem/JMath/JMath.hpp>

#include "Game/Util/StringUtil.hpp"
#include "Game/System/NebulaMods.hpp"

namespace {
    const DummyDisplayModelInfo cDummyDisplayModelInfoTable[15] = {
        {"Coin", {0.0f, 70.0f, 0.0f}, 15, nullptr, false},
        {"Kinopio", {0.0f, 50.0f, 0.0f}, 16, "Freeze", true},
        {"SpinDriver", {0.0f, 0.0f, 0.0f}, 11, nullptr, false},
        {"SuperSpinDriver", {0.0f, 0.0f, 0.0f}, 11, "Freeze", false},
        {"StarPieceDummy", {-30.0f, 100.0f, -30.0f}, 13, "Freeze", false},
        {"Tico", {0.0f, 50.0f, 0.0f}, 16, nullptr, true},
        {"KeySwitch", {0.0f, 0.0f, 0.0f}, 10, "InRotation", false},
        {"PowerStar", {0.0f, 0.0f, 0.0f}, 13, nullptr, false},
        {"KinokoOneUp", {0.0f, 40.0f, 0.0f}, 13, nullptr, false},
        {"Kuribo", {0.0f, 80.0f, 0.0f}, 18, nullptr, false},
        {"BlueChip", {0.0f, 0.0f, 0.0f}, 11, nullptr, false},
        {"YellowChip", {0.0f, 0.0f, 0.0f}, 11, nullptr, false},
        {"StrayTico", {0.0f, 50.0f, 0.0f}, 16, nullptr, false},
        {"GrandStar", {0.0f, 0.0f, 0.0f}, 13, nullptr, false},
        {"KinokoLifeUp", {0.0f, 40.0f, 0.0f}, 13, nullptr, false},
    };

    s32 getItemType(const JMapInfoIter& rIter) {
        s32 type = 0;

        if (MR::isValidInfo(rIter)) {
            MR::getJMapInfoArg6NoInit(rIter, &type);
        }

        return type;
    }

    DummyDisplayModel* tryCreateDummyModel(LiveActor* pHost, const JMapInfoIter& rIter, s32 a3, int itemIdx) {
        const DummyDisplayModelInfo* pInfo;
        s32 modelId = MR::getDummyDisplayModelId(rIter, a3);

        if (modelId == -1) {
            return nullptr;
        }

        if (modelId == 0 && MR::isGalaxyDarkCometAppearInCurrentStage()) {
            return nullptr;
        }

        // A mod may show one row's model for another, or none.
        const char* pShown = NebulaMods::objectBecomes(NEBULA_OBJECTS_DISPLAY_MODEL, ::cDummyDisplayModelInfoTable[modelId].mName);
        if (pShown == nullptr) {
            return nullptr;
        }
        for (s32 i = 0; i < static_cast< s32 >(sizeof(::cDummyDisplayModelInfoTable) / sizeof(::cDummyDisplayModelInfoTable[0])); i++) {
            if (MR::isEqualString(::cDummyDisplayModelInfoTable[i].mName, pShown)) {
                modelId = i;
                break;
            }
        }

        pInfo = &::cDummyDisplayModelInfoTable[modelId];
        DummyDisplayModel* mdl = new DummyDisplayModel(pHost, pInfo, itemIdx < 0 ? pInfo->_10 : itemIdx, modelId, getItemType(rIter));
        mdl->initWithoutIter();
        return mdl;
    }
};  // namespace

DummyDisplayModel::DummyDisplayModel(LiveActor* pHost, const DummyDisplayModelInfo* pInfo, int a3, s32 itemType, s32 a5)
    : PartsModel(pHost, pInfo->mName, pInfo->mName, nullptr, a3, 0) {
    mModelInfo = pInfo;
    mItemType = itemType;
    _A4 = a5;
    _A8 = 0;
    _AC = a3 == 33;
    mScale.x = 1.0f;
    mScale.y = 1.0f;
    mScale.z = 1.0f;

    switch (itemType) {
    case ITEM_TYPE_COIN: {
        if (!_AC) {
            MR::createCoinRotater();
        }
    }
    }
}

void DummyDisplayModel::init(const JMapInfoIter& rIter) {
    PartsModel::init(rIter);

    if (mModelInfo->mAnim != nullptr) {
        MR::startBck(this, mModelInfo->mAnim);
    }

    if (mModelInfo->mHasColorChange) {
        MR::startBrk(this, "ColorChange");
        MR::setBrkFrameAndStop(this, _A4);
    }

    if (mItemType != ITEM_TYPE_GRAND_STAR && MR::getLightNumMax(this) > 0) {
        MR::initLightCtrl(this);
    }

    switch (mItemType) {
    case ITEM_TYPE_STARPIECE: {
        if (!_AC) {
            MR::initShadowFromCSV(this, "Shadow");
        }

        break;
    }

    case ITEM_TYPE_POWER_STAR:
    case ITEM_TYPE_GRAND_STAR: {
        if (!_AC) {
            PowerStar::initShadowPowerStar(this, false);
        }

        PowerStar::setupColor(this, mHost, -1);
        MR::emitEffect(this, "Light");
        break;
    }

    case ITEM_TYPE_KINOPIO:
        _A8 = new LodCtrl(this, rIter);
        break;
    }

    MR::registerDemoSimpleCastAll(this);
}

void DummyDisplayModel::makeActorAppeared() {
    PartsModel::makeActorAppeared();

    if (_A8 != nullptr) {
        _A8->appear();
    }
}

void DummyDisplayModel::makeActorDead() {
    if (_A8 != nullptr) {
        _A8->kill();
    }

    PartsModel::makeActorDead();
}

void DummyDisplayModel::control() {
    switch (mItemType) {
    case ITEM_TYPE_POWER_STAR:
    case ITEM_TYPE_GRAND_STAR:
        PowerStar::requestPointLight(this, mHost, -1);
        break;
    case ITEM_TYPE_KINOPIO:
        _A8->update();
        break;
    }
}

void DummyDisplayModel::calcAndSetBaseMtx() {
    PartsModel::calcAndSetBaseMtx();
    TPos3f* m = (TPos3f*)getBaseMtx();

    TVec3f offset;
    TVec3f v19 = mModelInfo->_4;
    offset.negate(v19);
    NebulaMtxMultVec((MtxPtr)m, offset, &mPosition);
    m->setTrans(mPosition);

    switch (mItemType) {
    case ITEM_TYPE_COIN:

        if (!_AC) {
            MR::multMtx(*m, MR::getCoinRotateYMatrix(), *m);
        }

        break;
    case ITEM_TYPE_POWER_STAR:
    case ITEM_TYPE_GRAND_STAR:

        if (!_AC) {
            TPos3f rot;
            rot.makeRotate(TVec3f(0.0f, 1.0f, 0.0f), MR::toRadian(mRotation.y));
            MR::multMtx(*m, rot, *m);
        }

        break;
    }
}

namespace MR {
    DummyDisplayModel* createDummyDisplayModel(LiveActor* pHost, const JMapInfoIter& rIter, s32 a3, const TVec3f& a4, const TVec3f& a5) {
        DummyDisplayModel* mdl = ::tryCreateDummyModel(pHost, rIter, a3, -1);

        if (mdl == nullptr) {
            return nullptr;
        }

        mdl->initFixedPosition(a4, a5, nullptr);
        return mdl;
    }

    DummyDisplayModel* createDummyDisplayModel(LiveActor* pHost, const JMapInfoIter& rIter, const char* pName, s32 a4) {
        DummyDisplayModel* mdl = ::tryCreateDummyModel(pHost, rIter, a4, -1);

        if (mdl == nullptr) {
            return nullptr;
        }

        mdl->initFixedPosition(pName);
        return mdl;
    }

    DummyDisplayModel* createDummyDisplayModel(LiveActor* pHost, const JMapInfoIter& rIter, MtxPtr mtx, s32 a3, const TVec3f& a4, const TVec3f& a5) {
        DummyDisplayModel* mdl = ::tryCreateDummyModel(pHost, rIter, a3, -1);

        if (mdl == nullptr) {
            return nullptr;
        }

        mdl->initFixedPosition(mtx, a4, a5);
        return mdl;
    }

    DummyDisplayModel* createDummyDisplayModelCrystalItem(LiveActor* pHost, const JMapInfoIter& rIter, const TVec3f& a4, const TVec3f& a5) {
        DummyDisplayModel* mdl = ::tryCreateDummyModel(pHost, rIter, -1, 0x21);

        if (mdl == nullptr) {
            return nullptr;
        }

        mdl->initFixedPosition(a4, a5, nullptr);
        return mdl;
    }

    DummyDisplayModel* createDummyDisplayModelCrystalItem(LiveActor* pHost, s32 a2, const TVec3f& a3, const TVec3f& a4) {
        DummyDisplayModel* mdl = ::tryCreateDummyModel(pHost, JMapInfoIter(), a2, 0x21);
        mdl->initFixedPosition(a3, a4, nullptr);
        return mdl;
    }

    s32 getDummyDisplayModelId(const JMapInfoIter& rIter, s32 a2) {
        s32 arg = -1;

        if (MR::isValidInfo(rIter)) {
            MR::getJMapInfoArg7NoInit(rIter, &arg);
        }

        if (arg == -1 && a2 != -1) {
            arg = a2;
        }

        return arg;
    }

    s32 getDummyDisplayModelId(const LiveActor* pActor) {
        return static_cast< const DummyDisplayModel* >(pActor)->mItemType;
    }

    void makeArchiveListDummyDisplayModel(NameObjArchiveListCollector* pCollector, const JMapInfoIter& rIter) {
        s32 id = MR::getDummyDisplayModelId(rIter, -1);

        if (id >= 0) {
            pCollector->addArchive(::cDummyDisplayModelInfoTable[id].mName);
        }
    }
};  // namespace MR
