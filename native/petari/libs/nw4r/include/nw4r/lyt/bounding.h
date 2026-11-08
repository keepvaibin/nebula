#pragma once

#include "nw4r/lyt/pane.h"

namespace nw4r {
    namespace lyt {
        struct ResBlockSet;

        class Bounding : public Pane {
        public:
            static constexpr UiPaneKind cKind = UiPaneKind_Bounding;
            Bounding(Layout&, const UiPaneData*, const ResBlockSet&);

            virtual ~Bounding();
            virtual void DrawSelf(const DrawInfo&);
        };
    };  // namespace lyt
};  // namespace nw4r
