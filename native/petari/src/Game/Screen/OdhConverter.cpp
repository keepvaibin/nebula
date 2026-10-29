#include "Game/Screen/OdhConverter.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include "Game/NameObj/NameObjAdaptor.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/odh.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include <JSystem/JKernel/JKRSolidHeap.hpp>
#include <JSystem/JUtility/JUTVideo.hpp>

namespace {
    const u32 cCaptureWidth = 488;
    const u32 cCaptureHeight = 328;
    const f32 cCaptureOffsetX = 34.0f;
    const f32 cCaptureOffsetY = 44.0f;
    const u32 cLimitSize = 40960;
    const s32 cQuality = 80;
    const u32 cTexPosX = 0;
    const u32 cTexPosY = 0;
    // The readback channel of the capture (after the star pointer's 1, the talk's 2 and the lens flare's 3 and 4).
    const u32 cReadbackChannel = 5;

    OdhConverter* getConverter() {
        return MR::getSceneObj< OdhConverter >(SceneObj_OdhConverter);
    }
};  // namespace

OdhConverter::OdhConverter()
    : LayoutActor("ODH-jpeg\225\317\212\267" /* ODH-jpeg変換 */, true), mCaptureWidth(::cCaptureWidth), mCaptureHeight(::cCaptureHeight), mLimitSize(::cLimitSize),
      mIsRequestedCapture(false), mCapturePending(false), mCaptureSerial(0), mImage(nullptr), mCaptureImage(nullptr) {
}

void OdhConverter::init(const JMapInfoIter& rIter) {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_LayoutOnPause);
    MR::CurrentHeapRestorer heapRestorer(MR::getSceneHeapGDDR3());

    mImage = new (32) u8[mLimitSize];
    mCaptureImage = new (32) u8[mCaptureWidth * mCaptureHeight * 3];
}

void OdhConverter::draw() const {
}

bool OdhConverter::convert() {
    if (!mCapturePending) {
        // A read asks the renderer for the channel's captures; the capture is the picture drawn so far.
        (void)J3DNativeView::readbackColor(::cReadbackChannel, 0, 0);
        mCaptureSerial = J3DNativeView::readbackSerial(::cReadbackChannel);
        J3DNativeView::captureReadback(::cReadbackChannel);
        mCapturePending = true;
        return false;
    }
    if (J3DNativeView::readbackSerial(::cReadbackChannel) == mCaptureSerial) {
        return false;
    }

    // The capture as the encoder takes the console's EFB copy: RGB565 in 4x4 tiles, big-endian.
    u8* pScreenTexImage = MR::getScreenTexImage();
    const u32 stride = (mCaptureWidth & ~3) * 8;
    for (u32 y = 0; y < mCaptureHeight; y++) {
        for (u32 x = 0; x < mCaptureWidth; x++) {
            const u32 argb = J3DNativeView::readbackColor(::cReadbackChannel, ::cTexPosX + x, ::cTexPosY + y);
            const u16 pixel = static_cast< u16 >(((argb >> 8) & 0xF800) | ((argb >> 5) & 0x07E0) | ((argb >> 3) & 0x001F));
            u8* pOut = pScreenTexImage + (y / 4) * stride + (y & 3) * 8 + (x & ~3) * 8 + (x & 3) * 2;
            pOut[0] = static_cast< u8 >(pixel >> 8);
            pOut[1] = static_cast< u8 >(pixel);
        }
    }
    mCapturePending = false;
    mImageSize = ODHEncodeRGB565(pScreenTexImage, mImage, mCaptureWidth, mCaptureHeight, mLimitSize, ::cQuality, mCaptureImage);
    return true;
}

namespace MR {
    void createOdhConverter() {
        createSceneObj(SceneObj_OdhConverter);
    }

    void requestCaptureOdhImage() {
        return ::getConverter()->requestCapture();
    }

    bool isRequestedCaptureOdhImage() {
        if (!isExistSceneObj(SceneObj_OdhConverter)) {
            return false;
        }

        return ::getConverter()->isRequestedCapture();
    }

    void captureOdhImage() {
        ::getConverter()->capture();
    }

    void setPortCaptureOdhImage() {
        Mtx44 mtx;
        f32 bottom;
        f32 right;
        f32 top;
        f32 left;

        top = getFrameBufferHeight() * 0.5f - ::cCaptureOffsetX;
        left = -getFrameBufferWidth() * 0.5f + ::cCaptureOffsetY;
        right = getFrameBufferWidth() + left;
        bottom = top - getFrameBufferHeight();

        NebulaMtxOrtho(mtx, top, bottom, left, right, -1000.0f, 1000.0f);
        J3DNativeView::setProjection(mtx, GX_ORTHOGRAPHIC);
    }

    const u8* getOdhImage() {
        if (!isExistSceneObj(SceneObj_OdhConverter)) {
            return nullptr;
        }

        return ::getConverter()->getImage();
    }

    u32 getOdhImageSize() {
        if (!isExistSceneObj(SceneObj_OdhConverter)) {
            return nullptr;
        }

        return ::getConverter()->getImageSize();
    }

    NameObjAdaptor* createAdaptorAndConnectToWiiMessageBoard(const char* pParam1, const FunctorBase& rFunc) {
        NameObjAdaptor* pDrawAdaptor = createDrawAdaptor(pParam1, rFunc);

        connectToScene(pDrawAdaptor, -1, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_MessageBoardCapture);

        return pDrawAdaptor;
    }
};  // namespace MR
