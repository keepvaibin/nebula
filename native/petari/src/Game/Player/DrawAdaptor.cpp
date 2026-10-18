#include "Game/Player/DrawAdaptor.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ObjUtil.hpp"

DrawAdaptor::DrawAdaptor(const MR::FunctorBase& rFunc, int drawType) : NameObj("\203\150\203\215\201\1332D" /* ドロー2D */), mFunc(rFunc.clone(nullptr)) {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, drawType);
}

void DrawAdaptor::draw() const {
    (*mFunc)();
}
