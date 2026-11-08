#include "nw4r/lyt/bounding.h"
#include "nw4r/lyt/common.h"

namespace nw4r {
    namespace lyt {
        Bounding::Bounding(Layout& rLayout, const UiPaneData* pRes, const ResBlockSet& rBlockSet) : Pane(rLayout, pRes, rBlockSet) {
        }

        Bounding::~Bounding() {
        }

        void Bounding::DrawSelf(const DrawInfo&) {
        }

    };  // namespace lyt
};  // namespace nw4r
