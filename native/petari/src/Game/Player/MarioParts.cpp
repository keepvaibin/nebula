#include "Game/Player/MarioParts.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include <nebula_math.h>
#include "Game/System/LumaCore.hpp"

MarioParts::MarioParts(LiveActor* pActor, const char* pName, const char* pModelName, bool inCrystalBox, MtxPtr pFixedMtx, MtxPtr pMtx)
    : PartsModel(pActor, pName, pModelName, pMtx, inCrystalBox ? MR::DrawBufferType_CrystalBox : MR::DrawBufferType_PlayerDecoration, true), mEffectName() {
    lumacore::setClass(this, lumacore::kClassId< MarioParts >);
    MR::initMaterialFog(this, true);
    MR::prepareMaterials(this);
    if (MR::getLightNumMax(this) > 0) {
        MR::initLightCtrl(this);
    }

    if (pFixedMtx != nullptr) {
        initFixedPosition(pFixedMtx, Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));
        offFixedPosNormalizeScale();
    }
}

MarioParts::MarioParts(LiveActor* pActor, const char* pName, const char* pModelName, MtxPtr mtx)
    : PartsModel(pActor, pName, pModelName, nullptr, MR::DrawBufferType_PlayerParts, true), mEffectName() {
    lumacore::setClass(this, lumacore::kClassId< MarioParts >);
    MR::initMaterialFog(this, true);
    MR::prepareMaterials(this);
    if (MR::getLightNumMax(this) > 0) {
        MR::initLightCtrl(this);
    }
}

void MarioParts::init(const TableIter& rIter) {
    initEffectKeeper(8, mEffectName);
    initSound(8, false);
    MR::invalidateClipping(this);
    makeActorAppeared();
    makeActorAppeared();
}
