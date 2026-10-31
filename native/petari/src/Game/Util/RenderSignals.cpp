#include "Game/Util/RenderSignals.hpp"

namespace NebulaRender {
    uint8_t sDrawIdentities;

    void sceneMarker(uint32_t marker, const float (*view)[4]) {
        const NebulaHostServices* const host = nebula_host();
        if (marker == NEBULA_SCENE_3D_BEGIN || marker == NEBULA_SCENE_2D_BEGIN) {
            sDrawIdentities = static_cast<uint8_t>(host->option(host->user, NEBULA_OPTION_DRAW_IDENTITIES) != 0);
        }
        host->scene_marker(host->user, marker, view != NULL ? &view[0][0] : NULL);
    }

    void stripeParticle(uint64_t particle, uint32_t ageLifetime, uint8_t strip) {
        if (sDrawIdentities != 0) {
            const NebulaHostServices* const host = nebula_host();
            host->stripe_particle(host->user, particle, ageLifetime, strip);
        }
    }
}  // namespace NebulaRender
