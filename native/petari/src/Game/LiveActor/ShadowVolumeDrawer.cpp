#include "Game/LiveActor/ShadowVolumeDrawer.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/ObjUtil.hpp"

namespace {
    static Color8 sShapeColor(0xC0, 0xC0, 0, 4);
};  // namespace

ShadowVolumeDrawInit::ShadowVolumeDrawInit() : NameObj("シャドウボリューム描画初期化") {
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

void ShadowVolumeDrawer::calcBaseDropPosition(Vec3f* pVec) const {
    calcBaseDropPosition(pVec, getController());
}

void ShadowVolumeDrawer::calcBaseDropPosition(Vec3f* pPosition, const ShadowController* pController) const {
    Vec3f position;
    Vec3f direction;
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

void ShadowVolumeDrawer::loadModelDrawMtx(TypedDraw& /*rDraw*/) const {
}

void ShadowVolumeDrawer::drawShape(TypedDraw& /*rDraw*/) const {
}

bool ShadowVolumeDrawer::isDraw() const {
    return getController()->isDraw();
}

void ShadowVolumeDrawer::draw() const {
    if (isDraw()) {
        NebulaColor8 shapeColor = ::sShapeColor;
        SiteMaterial material;
        MR::initShadowVolumeMaterial(material, shapeColor);
        material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ADD);
        material.mState.cull = NEBULA_CULL_FRONT;
        TypedDraw site(material, nullptr, MR::getCameraViewMtx());
        loadModelDrawMtx(site);
        drawShape(site);
        material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_TAKE);
        material.mState.cull = NEBULA_CULL_BACK;
        site.setMaterial(material);
        drawShape(site);
    }
}

ShadowVolumeDrawInit::~ShadowVolumeDrawInit() {
}
