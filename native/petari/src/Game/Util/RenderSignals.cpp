#include "Game/Util/RenderSignals.hpp"

namespace NebulaRender {
    uint8_t sDrawIdentities;

    void sceneMarker(uint32_t marker, const float (*view)[4]) {
        const NebulaHostServices* const host = nebula_host();
        if (marker == NEBULA_SCENE_3D_BEGIN || marker == NEBULA_SCENE_2D_BEGIN) {
            sDrawIdentities = static_cast<uint8_t>(host->option(host->user, NEBULA_OPTION_DRAW_IDENTITIES) != 0);
        }
        J3DNativeDraw::flush();
        host->scene_marker(host->user, marker, view != NULL ? &view[0][0] : NULL);
    }

}  // namespace NebulaRender
