#pragma once

#include <nw4r/lyt/common.h>

class LayoutHolder;

namespace nw4r {
    namespace lyt {
        class Layout;
        class Pane;
        class TextBox;
        class Window;
    };  // namespace lyt
};  // namespace nw4r

// Panes built again from a layout's resource, as many times as a screen needs: each copy has its own materials and the
// authored look, without children, and belongs to the screen's layout (made in it, freed with it). Nebula's screens
// draw with the game's own windows and fonts this way (NebulaAchievementBoard). The holder's archive must stay loaded
// while the copies live (a scene's layouts do).
class NebulaPaneKit {
public:
    NebulaPaneKit();

    // The holder's layout, copied into pInto; false when the holder has none.
    bool init(LayoutHolder* pHolder, nw4r::lyt::Layout* pInto);
    // A copy of the named pane under a new name (at most 16 characters); null when the layout has no such pane.
    nw4r::lyt::Pane* make(const char* pName, const char* pNewName) const;
    nw4r::lyt::Window* makeWindow(const char* pName, const char* pNewName) const;
    // A text box copy ready for the game's text: a buffer of `length` characters and the game's tag processor.
    nw4r::lyt::TextBox* makeText(const char* pName, const char* pNewName, u32 length) const;

private:
    nw4r::lyt::ResBlockSet mBlocks;  // the holder's cooked layout; none before init
    nw4r::lyt::Layout* mpInto;       // the layout the copies are made in
};
