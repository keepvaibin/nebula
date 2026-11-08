#include "Game/System/NebulaHost.hpp"

namespace {
    // Asked once per id; the host's answer holds for the run.
    bool sAsked[NEBULA_PATCH_COUNT];
    bool sActive[NEBULA_PATCH_COUNT];
    u32 sValues[NEBULA_PATCH_COUNT];
};  // namespace

namespace NebulaHost {
    bool codePatch(u32 id, u32* pValue) {
        if (id == 0 || id >= NEBULA_PATCH_COUNT) {
            return false;
        }

        if (!sAsked[id]) {
            const NebulaHostServices* const pHost = nebula_host();
            uint32_t value = 0;
            sActive[id] = pHost->code_patch(pHost->user, id, &value) != 0;
            sValues[id] = value;
            sAsked[id] = true;
        }

        if (sActive[id] && pValue != nullptr) {
            *pValue = sValues[id];
        }

        return sActive[id];
    }

}  // namespace NebulaHost
