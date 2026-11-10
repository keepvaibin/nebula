#include "Game/Player/ModelHolder.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Render/Model.hpp"
#include <nebula_geometry.hpp>
#include <nebula_math.h>
#include "Game/System/LumaCore.hpp"

ModelHolder::ModelHolder(const char* pName, bool b) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< ModelHolder >);
    initModelManagerWithAnm(pName, nullptr, b);
    MR::invalidateClipping(this);
    kill();
}

void ModelHolder::updateMtx(MtxPtr mtxPtr) {
    Model* model = MR::getModel(this);
    NebulaMtxCopy(mtxPtr, model->mBaseTransformMtx);
    model->calc();      // 0x10
    model->viewCalc();  // 0x1C
}
