#include "Game/Player/MarioActor.hpp"
#include "Game/Util/NativeShader.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"

void MarioActor::drawPreWipe() const {
    if (!_1C3) {
        if (!_A61) {
            return;
        }
        f32 width = MR::getScreenWidth();
        f32 height = static_cast< s32 >(NebulaFrameClock::cScreenUnitsDown);
        NebulaNativeMaterial material;
        TDDraw::setupNative(&material, 0, 1, 2);
        NativeShader::send(material);
        TVec3f origin(0.0f, 0.0f, 0.0f);
        TVec3f size(width, height, 0.0f);
        TDDraw::drawFillBox(origin, size, _A6C > 128 ? 224 : (_A6C + (_A6C >> 1)) + (_A6C >> 2));
    }
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    MR::loadActorLight(this);
    drawMarioModel();
    MR::drawInitFor2DModel();
}
