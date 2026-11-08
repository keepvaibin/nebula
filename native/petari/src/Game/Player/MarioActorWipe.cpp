#include "Game/Player/MarioActor.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"

void MarioActor::drawPreWipe() const {
    if (!mDarkMaskOn) {
        if (!mPreWipeOn) {
            return;
        }
        f32 width = MR::getScreenWidth();
        f32 height = static_cast< s32 >(NebulaFrameClock::cScreenLines);
        SiteMaterial material;
        TDDraw::setupNative(&material, 0, 1, 2);
        Vec3f origin(0.0f, 0.0f, 0.0f);
        Vec3f size(width, height, 0.0f);
        TypedDraw site = TDDraw::site(material, 2);
        TDDraw::drawFillBox(site, origin, size, mPreWipeTimer > 128 ? 224 : (mPreWipeTimer + (mPreWipeTimer >> 1)) + (mPreWipeTimer >> 2));
    }
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    MR::loadActorLight(this);
    drawMarioModel();
    MR::drawInitFor2DModel();
}
