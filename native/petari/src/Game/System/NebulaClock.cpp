#include "Game/System/NebulaClock.hpp"

#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace NebulaClock {
    s64 nowNs() {
        const NebulaHostServices* const host = nebula_host();
        return static_cast< s64 >(host->clock_ns(host->user));
    }
}  // namespace NebulaClock
