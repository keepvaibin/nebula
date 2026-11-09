#pragma once

#include "Game/Screen/LayoutActor.hpp"
#include "Game/System/NebulaMods.hpp"

class NebulaPaneKit;

namespace Ui {
    class Pane;
    class TextBox;
}  // namespace Ui

// Plugins' lines of text over the game (draw_text): the game's message font, placed by the screen's corners at any
// aspect ratio. GameScene runs and draws it with Nebula's achievement pop-up, after every other 2D element.
class NebulaModText : public LayoutActor {
public:
    NebulaModText();

    virtual void init(const TableIter& rIter) override;

    // GameScene: the lines as plugins left them this frame, and their drawing.
    void update();
    void drawOnTop() const;

private:
    void place();

    NebulaPaneKit* mKit;
    Ui::Pane* mGroup;
    Ui::TextBox* mLines[NebulaMods::cTextLines];
    u32 mRevision;
    f32 mScaleX;
    f32 mWidth;
    f32 mHeight;
    bool mAny;
};
