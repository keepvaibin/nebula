#include "Game/System/NebulaHost.hpp"


namespace NebulaHost {
    bool codePatch(u32 id, u32* pValue) {
        static bool sAsked[NEBULA_PATCH_COUNT];
        static bool sActive[NEBULA_PATCH_COUNT];
        static u32 sValues[NEBULA_PATCH_COUNT];

        if (id == 0 || id >= NEBULA_PATCH_COUNT) {
            return false;
        }

        if (!sAsked[id]) {
            uint32_t value = 0;
            sActive[id] = nebula_host()->code_patch(nebula_host()->user, id, &value) != 0;
            sValues[id] = value;
            sAsked[id] = true;
        }

        if (sActive[id] && pValue != nullptr) {
            *pValue = sValues[id];
        }

        return sActive[id];
    }

    void objectName(const void* pObject, const char* pName) {
        nebula_host()->object_name(nebula_host()->user, objectId(pObject), pName);
    }
}  // namespace NebulaHost
