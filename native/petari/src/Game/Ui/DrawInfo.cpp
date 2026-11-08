#include "Game/Ui/DrawInfo.hpp"
#include <nebula_geometry.hpp>
#include <cstring>

namespace Ui {
    DrawInfo::DrawInfo() : mLocationAdjustScale(1.0f, 1.0f), mGlobalAlpha(1.0f) {
        memset(&mFlag, 0, sizeof(mFlag));
        mViewMtx.identity();
    }

    DrawInfo::~DrawInfo() {
    }
}  // namespace Ui
