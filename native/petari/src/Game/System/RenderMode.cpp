#include "Game/System/RenderMode.hpp"
#include "Game/System/NebulaSystemSettings.hpp"

namespace MR {
    bool isAspectRatioFlag16Per9() {
        return NebulaSystemSettings::isWidescreen();
    }
};  // namespace MR
