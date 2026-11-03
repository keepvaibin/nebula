#include "Game/MapObj/SpinDriverPathDrawer.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/MapObj/SpinDriverShootPath.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include "Game/Util/NativeShader.hpp"
#include <nebula_game_api.h>

namespace {
    // runtime/src/render/materials/spin_driver_path.hlsl: the path's colour texture (0) at its alpha times the mask's
    // (1), through texture matrices 0 and 1; added by source alpha where above zero, depth tested and not written, no
    // culling.
    void sendPathMaterial(JUTTexture* pColor, JUTTexture* pMask) {
        NebulaNativeMaterial material;
        NativeShader::init(material, NEBULA_SHADER_SPIN_DRIVER_PATH, NativeShader::BLEND_ADD_ALPHA, NEBULA_COMPARE_LEQUAL,
                           false, NEBULA_CULL_NONE);
        NativeShader::setTexture(material, 0, pColor);
        NativeShader::setTexture(material, 1, pMask);
        NativeShader::send(material);
        static const u8 kPlaces[8] = {0, 1, J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture,
                                      J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture};
        J3DNativeShape::setCurrent(0, kPlaces);
    }
}  // namespace

namespace {
};  // namespace

SpinDriverPathDrawInit::SpinDriverPathDrawInit()
    : NameObj("\203\130\203\163\203\223\203\150\203\211\203\103\203\157\201\133\203\214\201\133\203\213\225\140\211\346\217\211\212\372\211\273" /* スピンドライバーレール描画初期化 */), mOrangeTexture(), mGreenTexture(), mPinkTexture(), mMaskTexture(), mIsPathAtOpa() {
    MR::registerPreDrawFunction(MR::Functor(this, &SpinDriverPathDrawInit::initDraw), MR::DrawType_SpinDriverPathDrawer);

    mOrangeTexture = new JUTTexture(MR::loadTexFromArc("SpinDriverPath.arc", "NormalColor.bti"));
    mGreenTexture = new JUTTexture(MR::loadTexFromArc("SpinDriverPath.arc", "Green.bti"));
    mPinkTexture = new JUTTexture(MR::loadTexFromArc("SpinDriverPath.arc", "Pink.bti"));
    mMaskTexture = new JUTTexture(MR::loadTexFromArc("SpinDriverPath.arc", "Mask.bti"));
}

SpinDriverPathDrawer::SpinDriverPathDrawer(SpinDriverShootPath* pShootPath)
    : LiveActor("\203\160\203\130\225\140\211\346" /* パス描画 */), mShootPath(pShootPath), _90(), _94(), _98(), _9C(), mPositionCount(), _A4(), _A8(), _AC(), _B0(), mColor(-1), _B8(),
      mFadeScale(1.0f), mMaskLength(5000.0f) {
    MR::createSceneObj(SceneObj_SpinDriverPathDrawInit);
}

void SpinDriverPathDrawInit::initDraw() {
    TDDraw::cameraInit3D();

    J3DNativeShape::setPosition(0, MR::getCameraViewMtx());
    J3DPass::positionMatrix(0);
    sendPathMaterial(mOrangeTexture, mMaskTexture);
}

namespace MR {
    void setSpinDriverPathColorNormal() {
        SpinDriverPathDrawInit* pInit = MR::getSceneObj< SpinDriverPathDrawInit >(SceneObj_SpinDriverPathDrawInit);
        sendPathMaterial(pInit->mOrangeTexture, pInit->mMaskTexture);
    }

    void setSpinDriverPathColorGreen() {
        SpinDriverPathDrawInit* pInit = MR::getSceneObj< SpinDriverPathDrawInit >(SceneObj_SpinDriverPathDrawInit);
        sendPathMaterial(pInit->mGreenTexture, pInit->mMaskTexture);
    }

    void setSpinDriverPathColorPink() {
        SpinDriverPathDrawInit* pInit = MR::getSceneObj< SpinDriverPathDrawInit >(SceneObj_SpinDriverPathDrawInit);
        sendPathMaterial(pInit->mPinkTexture, pInit->mMaskTexture);
    }

    bool isDrawSpinDriverPathAtOpa() {
        if (!MR::isExistSceneObj(SceneObj_SpinDriverPathDrawInit)) {
            return false;
        }

        return MR::getSceneObj< SpinDriverPathDrawInit >(SceneObj_SpinDriverPathDrawInit)->mIsPathAtOpa;
    }

    void onDrawSpinDriverPathAtOpa() {
        MR::getSceneObj< SpinDriverPathDrawInit >(SceneObj_SpinDriverPathDrawInit)->mIsPathAtOpa = true;
    }

    void offDrawSpinDriverPathAtOpa() {
        MR::getSceneObj< SpinDriverPathDrawInit >(SceneObj_SpinDriverPathDrawInit)->mIsPathAtOpa = false;
    }
};  // namespace MR

void SpinDriverPathDrawer::init(const JMapInfoIter& rIter) {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_SpinDriverPathDrawer);

    initPositionList(75.0f, 20.0f);
    initClipping();
    initPathEnd();

    makeActorDead();
}

s32 SpinDriverPathDrawer::calcPositionCount(f32 f1, f32 f2) const {
    s32 totalSteps = mShootPath->getTotalLength() / f1;

    TVec3f pos0;
    mShootPath->calcPosition(&pos0, 0.0f);

    TVec3f dir0;
    mShootPath->calcDirection(&dir0, 0.0f, 0.01f);

    s32 count = 2;

    for (s32 i = 1; i < totalSteps; i++) {
        f32 posStep = static_cast< f32 >(i) / totalSteps;

        TVec3f posI;
        mShootPath->calcPosition(&posI, posStep);

        TVec3f pos;
        pos.killElement(posI - pos0, dir0);

        if (pos.length() > f2) {
            pos0 = posI;
            mShootPath->calcDirection(&dir0, posStep, 0.01f);

            count++;
        }
    }

    return count;
}

void SpinDriverPathDrawer::initPositionList(f32 f1, f32 f2) {
    mPositionCount = calcPositionCount(f1, f2);
    _90 = new (32) TVec3f[mPositionCount];
    _94 = new (32) TVec3f[mPositionCount];
    _98 = new (32) TVec3f[mPositionCount];
    _9C = new f32[mPositionCount];

    s32 totalSteps = mShootPath->getTotalLength() / f1;

    TVec3f pos0;
    mShootPath->calcPosition(&pos0, 0.0f);

    TVec3f dir0;
    mShootPath->calcDirection(&dir0, 0.0f, 0.01f);

    _90[0] = pos0;
    _9C[0] = 0.0f;

    MR::makeAxisVerticalZX(&_98[0], dir0);

    _94[0].cross(_98[0], dir0);
    MR::normalize(&_94[0]);

    s32 count = 1;

    for (s32 i = 1; i < totalSteps; i++) {
        f32 posStep = static_cast< f32 >(i) / totalSteps;

        TVec3f posI;
        mShootPath->calcPosition(&posI, posStep);

        TVec3f pos;
        pos.killElement(posI - pos0, dir0);

        if (pos.length() > f2) {
            pos0 = posI;

            mShootPath->calcDirection(&dir0, posStep, 0.01f);

            if (MR::abs(dir0.dot(_94[count - 1])) > MR::abs(dir0.dot(_98[count - 1]))) {
                MR::makeAxisFrontSide(&_98[count], &_94[count], dir0, _94[count - 1]);
            } else {
                MR::makeAxisFrontUp(&_94[count], &_98[count], dir0, _98[count - 1]);
            }

            _90[count] = posI;
            _9C[count] = posStep;

            count++;
        }
    }

    mShootPath->calcPosition(&_90[count], 1.0f);
    _94[count] = _94[count - 1];
    _98[count] = _98[count - 1];
    _9C[count] = 1.0f;
}

void SpinDriverPathDrawer::initClipping() {
    f32 radius = 0.0f;
    mShootPath->calcClippingInfo(&mPosition, &radius, 150.0f, 150.0f);
    MR::setClippingTypeSphere(this, radius);

    MR::validateClipping(this);
    MR::setClippingFarMax(this);
}

void SpinDriverPathDrawer::initPathEnd() {
    s32 arg1 = -1;
    MR::getRailArg1WithInit(mShootPath->mRailRider, &arg1);

    if (arg1 >= 0) {
        _B8 = arg1 / 1000.0f;
    } else {
        _B8 = 0.0f;
    }
}

f32 SpinDriverPathDrawer::calcDrawCode() const {
    if (_B8 <= 0.0f) {
        return _AC;
    }

    f32 val = 1.0f - _B8;
    return _AC < val ? _AC : val;
}

f32 SpinDriverPathDrawer::calcDrawBaseCode() const {
    if (_B8 <= 0.0f) {
        return _B0;
    }

    f32 val = _B0;

    if (val < (1.0f - _B8)) {
        return val;
    }

    return 1.0f - _B8;
}

void SpinDriverPathDrawer::setCoord(f32 coord) {
    _AC = coord;

    if (_B0 < coord) {
        _B0 = coord;
    }
}

void SpinDriverPathDrawer::setColorNormal() {
    mColor = -1;
}

void SpinDriverPathDrawer::setColorGreen() {
    mColor = 0;
}

void SpinDriverPathDrawer::setColorPink() {
    mColor = 1;
}

void SpinDriverPathDrawer::setMaskLength(f32 mask) {
    if (mask < 1.0f) {
        mMaskLength = 1.0f;
    } else {
        mMaskLength = mask;
    }
}

void SpinDriverPathDrawer::setFadeScale(f32 fade) {
    if (fade < 0.001f) {
        mFadeScale = 0.001f;
    } else {
        mFadeScale = fade;
    }
}

void SpinDriverPathDrawer::draw() const {
    switch (mColor) {
    case -1:
        MR::setSpinDriverPathColorNormal();

        break;
    case 0:
        MR::setSpinDriverPathColorGreen();

        break;
    case 1:
        MR::setSpinDriverPathColorPink();

        break;
    default:
        MR::setSpinDriverPathColorNormal();

        break;
    }

    f32 drawCode = calcDrawCode();
    f32 drawBaseCode = calcDrawBaseCode();
    f32 maskSteps = mShootPath->getTotalLength() / mMaskLength;

    TPos3f mtx;
    mtx.identity();
    mtx[1][1] = -maskSteps;
    mtx[1][3] = 0.5f + maskSteps * drawCode;

    J3DNativeShape::setTexture(1, mtx, 2);

    mtx.identity();
    mtx[1][1] = 1.0f / mFadeScale;
    mtx[1][3] = 1.0f - drawBaseCode * mtx[1][1];

    J3DNativeShape::setTexture(0, mtx, 2);

    for (u32 i = 1; i < mPositionCount; i++) {
        TVec3f prev90(_90[i - 1]), curr90(_90[i]);
        TVec3f prev94(_94[i - 1] * 100.0f), curr94(_94[i] * 100.0f);
        TVec3f prev98(_98[i - 1] * 100.0f), curr98(_98[i] * 100.0f);

        f32 prevVal = _9C[i - 1];
        f32 val = _9C[i];

        J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 6, NEBULA_SHAPE_TEXCOORD0);

        sendPoint(prev90 - prev98, 0.0f, prevVal);
        sendPoint(curr90 - curr98, 0.0f, val);
        sendPoint(prev90, 0.5f, prevVal);
        sendPoint(curr90, 0.5f, val);
        sendPoint(prev90 + prev98, 1.0f, prevVal);
        sendPoint(curr90 + curr98, 1.0f, val);

        J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 6, NEBULA_SHAPE_TEXCOORD0);

        sendPoint(prev90 - prev94, 0.0f, prevVal);
        sendPoint(curr90 - curr94, 0.0f, val);
        sendPoint(prev90, 0.5f, prevVal);
        sendPoint(curr90, 0.5f, val);
        sendPoint(prev90 + prev94, 1.0f, prevVal);
        sendPoint(curr90 + curr94, 1.0f, val);
    }
}

void SpinDriverPathDrawer::sendPoint(const TVec3f& rA1, f32 a2, f32 a3) const {
    J3DNativeDraw::position(rA1.x, rA1.y, rA1.z);
    J3DNativeDraw::texCoord(a2, a3);
}
