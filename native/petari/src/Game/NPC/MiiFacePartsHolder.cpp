#include "Game/NPC/MiiFacePartsHolder.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/NPC/MiiFaceParts.hpp"
#include "Game/NPC/MiiFaceRecipe.hpp"
#include "Game/NameObj/NameObjAdaptor.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/AssetFolder.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MotionSignal.hpp"
#include "Game/Util/ObjUtil.hpp"
#include <ModelFileNumbers.h>
#include "Game/System/LumaCore.hpp"

MiiFacePartsHolder::MiiFacePartsHolder(int partsNumMax) : LiveActorGroup("Mii顔モデル保持", partsNumMax), mReady(false) {
    lumacore::setClass(this, lumacore::kClassId< MiiFacePartsHolder >);
}

MiiFacePartsHolder::~MiiFacePartsHolder() {
    Mii::clearParts();
}

void MiiFacePartsHolder::init(const TableIter& /*rIter*/) {
    AssetFolder* pArchive = MR::getArchive("/ObjectData/MiiFaceDatabase");
    const void* pParts = pArchive != nullptr ? pArchive->getResource("/MiiParts.npt") : nullptr;
    mReady = pParts != nullptr && Mii::setParts(pParts, pArchive->getResSize(pParts));

    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_NPC, MR::DrawBufferType_None, MR::DrawType_MiiFacePartsHolder);
    MR::connectToScene(MR::createDrawAdaptor("Miiモデル再作成", MR::Functor(this, &MiiFacePartsHolder::buildWaitingHeads)), MR::MovementType_None,
                       MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_MiiFaceNew);
}

void MiiFacePartsHolder::calcAnim() {
    for (int i = 0; i < getObjNum(); i++) {
        LiveActor* pActor = getActor(i);

        if (pActor == nullptr || MR::isNoCalcAnim(pActor)) {
            continue;
        }
        pActor->calcAnim();
    }
    calcViewAndEntry();
}

void MiiFacePartsHolder::calcViewAndEntry() {
    for (int i = 0; i < getObjNum(); i++) {
        LiveActor* pActor = getActor(i);

        if (pActor == nullptr || MR::isNoCalcView(pActor)) {
            continue;
        }
        pActor->calcViewAndEntry();
    }
}

void MiiFacePartsHolder::buildWaitingHeads() {
    if (!mReady) {
        return;
    }
    for (int i = 0; i < getObjNum(); i++) {
        static_cast< MiiFaceParts* >(getActor(i))->buildIfWaiting();
    }
}

bool MiiFacePartsHolder::isInitEnd() const {
    if (isError()) {
        return true;
    }
    for (int i = 0; i < getObjNum(); i++) {
        if (static_cast< MiiFaceParts* >(getActor(i))->isWaitingToBuild()) {
            return false;
        }
    }
    return true;
}

bool MiiFacePartsHolder::isError() const {
    return !mReady;
}

MiiFaceParts* MiiFacePartsHolder::createPartsFromReceipe(const char* pName, const MiiFaceRecipe& rRecipe) {
    MiiFaceParts* pParts = new MiiFaceParts(pName, rRecipe);

    MR::getSceneObj< MiiFacePartsHolder >(SceneObj_MiiFacePartsHolder)->registerActor(pParts);
    pParts->initWithoutIter();
    return pParts;
}

MiiFaceParts* MiiFacePartsHolder::createPartsFromDefault(const char* pName, u32 index) {
    return createPartsFromReceipe(
        pName, MiiFaceRecipe(Mii::SourceDefault, index, 256, Mii::expressionBit(Mii::ExpressionNormal) | Mii::expressionBit(Mii::ExpressionBlink)));
}

void MiiFacePartsHolder::drawEachActor(DrawPartsFuncPtr pDrawFunc, const Mii::Style& rStyle) const {
    for (int i = 0; i < getObjNum(); i++) {
        MiiFaceParts* pParts = static_cast< MiiFaceParts* >(getActor(i));

        if (!MR::isDead(pParts)) {
            // Each head draws as an object of its own (its slot, its life), the translucent pass after the opaque pass's
            // numbers, so a head dead or not yet built does not renumber the others and the heads' places pair with themselves.
            MotionSignal::drawObject(pParts->mDrawSlot, pParts->mMotionEpoch);
            Frame::setPart(rStyle.chain == Mii::ChainNpcXlu ? 16u : 0u);
            (pParts->*pDrawFunc)(rStyle);
        }
    }
}

// The game's Mii NPC stages after each part (mii_face.hlsl chains 2 and 3): the colour lit by lights 0 and 1 (clamped
// diffuse, no attenuation) over the material grey 165 and ambient 128; the opaque pass adds the rim from the alpha
// channel lit by light 2 (signed diffuse) and draws where the alpha is not 255.
void MiiFacePartsHolder::draw() const {
    MR::loadLight(1);
    Mii::Style style = {};
    style.lightMask = MODEL_FILE_LIGHT0 | MODEL_FILE_LIGHT1;
    style.diffuse = MODEL_FILE_DIFFUSE_CLAMP;
    style.atten = MODEL_FILE_ATTEN_NONE;
    style.ambient = NebulaColor8{128, 128, 128, 128};
    style.material = NebulaColor8{165, 165, 165, 255};

    style.chain = Mii::ChainNpcOpa;
    style.alphaTest = 1;
    style.pass = PassStates::use(NEBULA_FACTOR_ONE, NEBULA_FACTOR_ZERO, 1, NEBULA_COMPARE_LEQUAL, 1, NEBULA_CULL_NONE, 1, 2);
    drawEachActor(&MiiFaceParts::drawOpa, style);

    style.chain = Mii::ChainNpcXlu;
    style.alphaTest = 0;
    style.pass = PassStates::use(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA, 1, NEBULA_COMPARE_LEQUAL, 1, NEBULA_CULL_NONE, 1, 2);
    drawEachActor(&MiiFaceParts::drawXlu, style);
}
