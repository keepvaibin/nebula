#include "Game/NPC/MiiFacePartsHolder.hpp"
#include "MiiNative.h"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/NPC/MiiDatabase.hpp"
#include "Game/NPC/MiiFaceParts.hpp"
#include "Game/NPC/MiiFaceRecipe.hpp"
#include "Game/NameObj/NameObjAdaptor.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/System/AssetFolder.hpp"
#include "Game/System/NebulaArena.hpp"

MiiFacePartsHolder::MiiFacePartsHolder(int partsNumMax) : LiveActorGroup("Mii\212\347\203\202\203\146\203\213\225\333\216\235" /* Mii顔モデル保持 */, partsNumMax), JKRDisposer(), _34() {
}

MiiFacePartsHolder::~MiiFacePartsHolder() {
    MiiExit();
    delete _34;
}

void MiiFacePartsHolder::init(const JMapInfoIter& rIter) {
    AssetFolder* pArchive = MR::receiveArchive("/ObjectData/MiiFaceDatabase.arc");
    void* pResBuffer = pArchive->getResource("/RFL_Res.dat");
    u32 resSize = pArchive->getResSize(pResBuffer);
    _38 = MiiInitRes(pResBuffer, resSize, false);

    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_NPC, MR::DrawBufferType_None, MR::DrawType_MiiFacePartsHolder);
    MR::connectToScene(MR::createDrawAdaptor("Mii\203\202\203\146\203\213\215\304\215\354\220\254" /* Miiモデル再作成 */, MR::Functor(this, &MiiFacePartsHolder::reinitCharModel)), MR::MovementType_None,
                       MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_MiiFaceNew);
}

void MiiFacePartsHolder::calcAnim() {
    for (int i = 0; i < getObjNum(); i++) {
        LiveActor* pActor = getActor(i);

        if (pActor == nullptr) {
            continue;
        }

        if (MR::isNoCalcAnim(pActor)) {
            continue;
        }

        pActor->calcAnim();
    }

    calcViewAndEntry();
}

void MiiFacePartsHolder::calcViewAndEntry() {
    for (int i = 0; i < getObjNum(); i++) {
        LiveActor* pActor = getActor(i);

        if (pActor == nullptr) {
            continue;
        }

        if (MR::isNoCalcView(pActor)) {
            continue;
        }

        pActor->calcViewAndEntry();
    }
}

void MiiFacePartsHolder::draw() const {
    drawExtra();
}

void MiiFacePartsHolder::reinitCharModel() {
    MiiFaceParts* pParts;

    if (_38 == MiiErrcode_Success) {
        for (int i = 0; i < getObjNum(); i++) {
            pParts = static_cast< MiiFaceParts* >(getActor(i));

            if (pParts->_D0 || pParts->_D1) {
                pParts->initFaceModel();
            }
        }
    }
}

bool MiiFacePartsHolder::isInitEnd() const {
    MiiFaceParts* pParts;

    if (isError()) {
        return true;
    }

    for (int i = 0; i < getObjNum(); i++) {
        pParts = static_cast< MiiFaceParts* >(getActor(i));

        if (pParts->_D1) {
            return false;
        }
    }

    return true;
}

bool MiiFacePartsHolder::isError() const {
    return _38 != MiiErrcode_Success;
}

MiiFaceParts* MiiFacePartsHolder::createPartsFromReceipe(const char* pName, const MiiFaceRecipe& rRecipe) {
    MiiFaceParts* pParts = new MiiFaceParts(pName, rRecipe);

    MR::getSceneObj< MiiFacePartsHolder >(SceneObj_MiiFacePartsHolder)->registerActor(pParts);

    if (MR::startFunctionAsyncExecuteOnMainThread(MR::Functor< NameObj >(pParts, &NameObj::initWithoutIter), "initNameObjOnMainThread")) {
        MR::waitForEndFunctionAsyncExecute("initNameObjOnMainThread");
    }

    return pParts;
}

MiiFaceParts* MiiFacePartsHolder::createPartsFromDefault(const char* pName, u16 miiIndex) {
    return createPartsFromReceipe(pName, MiiFaceRecipe(MiiDataSource_Default, miiIndex, MiiResolution_256, MiiExpFlag_Normal | MiiExpFlag_Blink));
}

void MiiFacePartsHolder::drawEachActor(DrawPartsFuncPtr pDrawFunc, const MiiDrawCoreSetting* pSetting) const {
    MiiFaceParts* pParts;

    for (int i = 0; i < getObjNum(); i++) {
        pParts = static_cast< MiiFaceParts* >(getActor(i));

        if (MR::isDead(pParts)) {
            continue;
        }

        (pParts->*pDrawFunc)(pSetting);
    }
}

void MiiFacePartsHolder::drawExtra() const {
    MiiDrawCoreSetting setting;
    setting.matrix = 0;
    setting.reverseCulling = 0;

    MR::loadLight(1);
    setTevOpa();
    J3DPass::alphaTest(NEBULA_COMPARE_NEQUAL, 255);
    J3DPass::blend(NEBULA_FACTOR_ONE, NEBULA_FACTOR_ZERO);
    drawEachActor(&MiiFaceParts::drawOpa, &setting);
    setTevXlu();
    J3DPass::alphaTest(NEBULA_COMPARE_GREATER, 0);
    J3DPass::blend(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA);
    drawEachActor(&MiiFaceParts::drawXlu, &setting);
}

// The game's Mii NPC stages after each part (runtime/src/render/materials/mii_face.hlsl chains 2 and 3): the colour lit
// by lights 0 and 1 (clamped diffuse, no attenuation) over the material grey 165 and ambient 128; the opaque pass adds
// the rim from the alpha channel lit by light 2 (signed diffuse) and draws where the alpha is not 255.
void MiiFacePartsHolder::setTevOpa() const {
    const NebulaColor8 material = {165, 165, 165, 255};
    const NebulaColor8 ambient = {128, 128, 128, 128};
    Mii_NativeChain(MII_I_NATIVE_CHAIN_NPC_OPA, J3D_LIGHT0 | J3D_LIGHT1, J3D_DF_CLAMP, J3D_AF_NONE, ambient, material, 1);
    J3DPass::depth(true, NEBULA_COMPARE_LEQUAL, true);
}

void MiiFacePartsHolder::setTevXlu() const {
    const NebulaColor8 material = {165, 165, 165, 255};
    const NebulaColor8 ambient = {128, 128, 128, 128};
    Mii_NativeChain(MII_I_NATIVE_CHAIN_NPC_XLU, J3D_LIGHT0 | J3D_LIGHT1, J3D_DF_CLAMP, J3D_AF_NONE, ambient, material, 0);
    J3DPass::depth(true, NEBULA_COMPARE_LEQUAL, true);
}
