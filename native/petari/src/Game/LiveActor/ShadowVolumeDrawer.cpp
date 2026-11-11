#include "Game/LiveActor/ShadowVolumeDrawer.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    static Color8 sShapeColor(0xC0, 0xC0, 0, 4);
    // Room for the draws of one pass of one shape (a model file's meshes); the front pass, the back pass and each variant
    // start a range of their own.
    NEBULA_TUNABLE(u32, cPartsPerPass, 256);
};  // namespace

ShadowVolumeDrawInit::ShadowVolumeDrawInit() : NameObj("シャドウボリューム描画初期化") {
    lumacore::setClass(this, lumacore::kClassId< ShadowVolumeDrawInit >);
}

ShadowVolumeDrawer::ShadowVolumeDrawer(const char* pName) : ShadowDrawer(pName) {
    lumacore::setClass(this, lumacore::kClassId< ShadowVolumeDrawer >);
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
        // The host's life and the shape's run of smooth steps name the draws: a host renewed in place, or a shape that
        // jumped, is never blended from where it was (world-space shapes carry no teleport test of their own).
        beginDrawObject();
        // For a renderer that traces this shadow instead of filling its volume: where it falls and whose it is.
        // (A drawer whose own isDraw does not ask a controller may have none: its shadow stays the game's.)
        if (const ShadowController* pController = getController()) {
            Vec3f position;
            Vec3f direction;
            calcBaseDropPosition(&position, pController);
            pController->getDropDir(&direction);
            NebulaFrameShadowDrop drop = {};
            drop.position[0] = position.x;
            drop.position[1] = position.y;
            drop.position[2] = position.z;
            drop.length = calcBaseDropLength(pController);
            drop.direction[0] = direction.x;
            drop.direction[1] = direction.y;
            drop.direction[2] = direction.z;
            const LiveActor* pHost = pController->getHost();
            const Model* pModel = pHost != nullptr && pHost->mModelManager != nullptr ? pHost->mModelManager->getModel() : nullptr;
            drop.caster = pModel != nullptr ? pModel->getSlot() : 0;
            drop.volume = Frame::objectSlot();
            Frame::shadowDrop(drop);
        }
        NebulaColor8 shapeColor = ::sShapeColor;
        SiteMaterial material;
        MR::initShadowVolumeMaterial(material, shapeColor);
        material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ADD);
        material.mState.cull = NEBULA_CULL_FRONT;
        TypedDraw site(material, nullptr, MR::getCameraViewMtx());
        loadModelDrawMtx(site);
        // The two passes draw one shape: each pass pairs with its own pass of the frame before, whatever the shape wrote
        // (its draw count can change), and both pair or neither does.
        const u32 first = shapeVariant() * 2 * ::cPartsPerPass;
        site.setPart(first);
        drawShape(site);
        material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_TAKE);
        material.mState.cull = NEBULA_CULL_BACK;
        site.setMaterial(material);
        site.setPart(first + ::cPartsPerPass);
        drawShape(site);
    }
}

ShadowVolumeDrawInit::~ShadowVolumeDrawInit() {
}
