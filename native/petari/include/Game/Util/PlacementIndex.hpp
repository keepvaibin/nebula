#pragma once

#include <nebula_types.h>

namespace MR {
    /// @brief A placement argument that names one of `count` entries of a table: the argument when it names one, else
    /// `fallback`. A stage's arguments are its author's (a mod's stage among them), and the console indexed its tables
    /// with whatever they held: ElectricRail's colour (argument 3; ElectricRailMoving's 4) wrote and read past the rail
    /// holder's four models (N-3032), and a view group cube with no group (argument 0 unset, -1) wrote before the view
    /// group table each step the player was in it (N-3031).
    template < class T >
    inline T placementIndexOr(T arg, u32 count, T fallback) {
        return arg >= 0 && static_cast< u32 >(arg) < count ? arg : fallback;
    }
}  // namespace MR
