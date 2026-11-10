#include "Game/Camera/CameraCover.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Util.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/System/LumaCore.hpp"

CameraCover::CameraCover(const char* pName) : NameObj(pName), mActor(), mCoverFramesLeft(0), mIsCoverRequested(false), mCoverFrames(1) {
    lumacore::setClass(this, lumacore::kClassId< CameraCover >);
    mActor = new CaptureScreenActor(MR::DrawType_CaptureScreenCamera, "Camera");
    MR::connectToScene(this, MR::MovementType_CameraCover, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_CameraCover);
    MR::joinToNameObjGroup(this, "IgnorePauseNameObj");
    mActor->initWithoutIter();
}

void CameraCover::movement() {
    if (mIsCoverRequested && mCoverFramesLeft == 0) {
        MR::startToCaptureScreen("Camera");
        CategoryList::execute(MR::DrawType_CaptureScreenCamera);
        mCoverFramesLeft = mCoverFrames + 1;
    }

    if (mCoverFramesLeft != 0 && --mCoverFramesLeft == 0) {
        MR::endToCaptureScreen("Camera");
    }

    mIsCoverRequested = false;
}

void CameraCover::draw() const {
    if (mCoverFramesLeft != 0) {
        GameTexture texture(MR::getScreenResTIMG());

        ImageEffectLocalUtil::setupDrawTexture();
        ImageEffectLocalUtil::drawTexture(&texture, 255, (ImageEffectLocalUtil::ETexDrawType)0);
    }
}

void CameraCover::cover(u32 frames) {
    mCoverFrames = frames;
    mIsCoverRequested = true;
}
