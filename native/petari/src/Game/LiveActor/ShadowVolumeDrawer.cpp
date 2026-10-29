#include "Game/LiveActor/ShadowVolumeDrawer.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/ObjUtil.hpp"

namespace {
    static Color8 sShapeColor(0xC0, 0xC0, 0, 4);
    static Color8 sDebugShapeColor(0xFF, 0, 0, 0x80);
};  // namespace

ShadowVolumeDrawInit::ShadowVolumeDrawInit() : NameObj("\203\126\203\203\203\150\203\105\203\173\203\212\203\205\201\133\203\200\225\140\211\346\217\211\212\372\211\273" /* シャドウボリューム描画初期化 */) {
    MR::registerPreDrawFunction(MR::Functor(&MR::setupShadowVolumeDraw), MR::DrawType_ShadowVolume);
}

ShadowVolumeDrawer::ShadowVolumeDrawer(const char* pName) : ShadowDrawer(pName) {
    mStartDrawShapeOffset = 0.0f;
    mEndDrawShapeOffset = 0.0f;
    mIsCutDropShadow = false;
    MR::createSceneObj(SceneObj_ShadowVolumeDrawInit);
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_ShadowVolume);
}

void ShadowVolumeDrawer::setStartDrawShepeOffset(f32 offs) {
    mStartDrawShapeOffset = offs;
}

void ShadowVolumeDrawer::setEndDrawShepeOffset(f32 offs) {
    mEndDrawShapeOffset = offs;
}

void ShadowVolumeDrawer::onCutDropShadow() {
    mIsCutDropShadow = true;
}

void ShadowVolumeDrawer::offCutDropShadow() {
    mIsCutDropShadow = false;
}

void ShadowVolumeDrawer::calcBaseDropPosition(TVec3f* pVec) const {
    calcBaseDropPosition(pVec, getController());
}

void ShadowVolumeDrawer::calcBaseDropPosition(TVec3f* pPosition, const ShadowController* pController) const {
    TVec3f position;
    TVec3f direction;
    pController->getDropPos(&position);
    pController->getDropDir(&direction);
    pPosition->set(position + direction * mStartDrawShapeOffset);
}

f32 ShadowVolumeDrawer::calcBaseDropLength() const {
    return calcBaseDropLength(getController());
}

f32 ShadowVolumeDrawer::calcBaseDropLength(const ShadowController* pController) const {
    f32 length = pController->getDropLength();

    if (mIsCutDropShadow) {
        if (pController->isProjected()) {
            length = pController->getProjectionLength();
        }
    }

    f32 endOffset = mEndDrawShapeOffset;
    f32 startOffset = mStartDrawShapeOffset;
    length += -startOffset + endOffset;
    return length;
}

void ShadowVolumeDrawer::loadModelDrawMtx() const {
}

void ShadowVolumeDrawer::drawShape() const {
}

bool ShadowVolumeDrawer::isDraw() const {
    return getController()->isDraw();
}

void ShadowVolumeDrawer::draw() const {
    if (isDraw()) {
        loadModelDrawMtx();
        GXColor shapeColor = reinterpret_cast< GXColor& >(::sShapeColor);
        J3DNativeState::setTevColor(GX_TEVREG0, shapeColor);
        J3DNativeState::setColorUpdate(0);
        J3DNativeState::setDstAlpha(0, 0);
        J3DNativeState::setCullMode(GX_CULL_FRONT);
        J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_COPY);
        drawShape();
        J3DNativeState::setCullMode(GX_CULL_BACK);
        J3DNativeState::setBlendMode(GX_BM_SUBTRACT, GX_BL_ZERO, GX_BL_ZERO, GX_LO_COPY);
        drawShape();
        J3DNativeState::setCullMode(GX_CULL_NONE);
        J3DNativeState::setColorUpdate(1);
        J3DNativeState::setDstAlpha(1, 0);
    }
}

ShadowVolumeDrawInit::~ShadowVolumeDrawInit() {
}
