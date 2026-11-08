#include "Game/Ui/Bounding.hpp"
#include "Game/Ui/UiCommon.hpp"

namespace Ui {
    Bounding::Bounding(Layout& rLayout, const UiPaneData* pRes, const ResBlockSet& rBlockSet) : Pane(rLayout, pRes, rBlockSet) {
    }

    Bounding::~Bounding() {
    }

    void Bounding::DrawSelf(const DrawInfo&) {
    }
}  // namespace Ui
