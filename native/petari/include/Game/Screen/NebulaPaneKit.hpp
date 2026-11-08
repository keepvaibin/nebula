#pragma once

#include <Game/Ui/UiCommon.hpp>

class LayoutHolder;

namespace Ui {
    class Layout;
    class Pane;
    class TextBox;
    class Window;
}  // namespace Ui

// Panes built again from a layout's resource, as many times as a screen needs: each copy has its own materials and the
// authored look, without children, and belongs to the screen's layout (made in it, freed with it). Nebula's screens
// draw with the game's own windows and fonts this way (NebulaAchievementBoard). The holder's archive must stay loaded
// while the copies live (a scene's layouts do).
//
// Under a mod's copy of the archive the screens never lose a pane: one the mod's lacks, or has as another kind, comes
// from the game's own copy (opened the first time one is missing), and a mod's copy with no usable layout gives way
// whole. Each such pane is logged.
class NebulaPaneKit {
public:
    NebulaPaneKit();

    // The layout of pHolder, the holder of the layout archive pArchive ("SysInfoWindow": a name the kit keeps),
    // copied into pInto; false when neither it nor the game's own copy has one.
    bool init(LayoutHolder* pHolder, const char* pArchive, Ui::Layout* pInto);
    // As above, with the archive's holder.
    bool init(const char* pArchive, Ui::Layout* pInto);
    // A copy of the named pane under a new name (at most 16 characters); null when neither copy has such a pane.
    Ui::Pane* make(const char* pName, const char* pNewName) const;
    Ui::Window* makeWindow(const char* pName, const char* pNewName) const;
    // A text box copy ready for the game's text: a buffer of `length` characters and the game's tag processor.
    Ui::TextBox* makeText(const char* pName, const char* pNewName, u32 length) const;

private:
    // The named pane of kind `kind` (-1 any) in the blocks, or none.
    static u32 find(const Ui::ResBlockSet& rBlocks, const char* pName, s32 kind);
    Ui::Pane* build(const char* pName, const char* pNewName, s32 kind) const;

    Ui::ResBlockSet mBlocks;        // the holder's cooked layout; none before init
    mutable Ui::ResBlockSet mGame;  // the game's own copy's, under a mod's; none until a pane is missing
    mutable bool mGameOpened;
    const char* mpArchive;            // the archive's name
    Ui::Layout* mpInto;        // the layout the copies are made in
};
