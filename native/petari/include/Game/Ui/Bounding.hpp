#pragma once

#include "Game/Ui/Pane.hpp"

namespace Ui {
    struct ResBlockSet;

    class Bounding : public Pane {
    public:
        static constexpr UiPaneKind cKind = UiPaneKind_Bounding;
        Bounding(Layout&, const UiPaneData*, const ResBlockSet&);

        virtual ~Bounding() override;
        virtual void DrawSelf(const DrawInfo&) override;
    };
}  // namespace Ui
