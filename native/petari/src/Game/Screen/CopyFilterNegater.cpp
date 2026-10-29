#include "Game/Screen/CopyFilterNegater.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/JUtility/JUTVideo.hpp>

CopyFilterNegater::CopyFilterNegater() {
    mRenderObj = JUTVideo::getManager()->getRenderMode();
    J3DNativeState::setCopyFilter(GX_FALSE, mRenderObj->sample_pattern, GX_FALSE, mRenderObj->vfilter);
}

CopyFilterNegater::~CopyFilterNegater() {
    J3DNativeState::setCopyFilter(GX_FALSE, mRenderObj->sample_pattern, GX_TRUE, mRenderObj->vfilter);
}
