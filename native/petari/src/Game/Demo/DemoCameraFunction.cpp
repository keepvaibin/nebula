#include "Game/Demo/DemoCameraFunction.hpp"
#include "Game/Demo/DemoCameraKeeper.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/StringUtil.hpp"

namespace DemoCameraFunction {
    bool isCameraTargetMario(const DemoCameraInfo* pInfo) {
        return pInfo->mCameraTargetName != nullptr && MR::isEqualString(pInfo->mCameraTargetName, MR::getPlayerDemoActor()->mName);
    }

    // True when it emptied an empty string.
    bool setStringNullIfEmpty(const char** ppStr) {
        if (*ppStr == nullptr || (*ppStr)[0] != '\0') {
            return false;
        }

        *ppStr = nullptr;
        return true;
    }

    void makeAnimCameraName(MR::FormattedName& rName, const DemoCameraInfo* pInfo) {
        MR::removeExtensionString(rName, pInfo->mAnimCameraName);
    }
};  // namespace DemoCameraFunction
