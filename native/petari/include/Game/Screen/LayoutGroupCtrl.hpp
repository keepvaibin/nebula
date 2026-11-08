#pragma once

#include "Game/Util/Array.hpp"

namespace Ui {
    class Pane;
    class Group;
}  // namespace Ui

class LayoutAnmPlayer;
class LayoutManager;

class LayoutGroupCtrl {
public:
    LayoutGroupCtrl(LayoutManager* pManager, const char* pName, u32 animLayerCount);

    void movement();
    void calcAnim();
    u32 getPaneNum() const;
    Ui::Pane* getPane(u32 idx) const;

    LayoutManager* mManager;
    Ui::Group* mGroup;
    MR::AssignableArray< LayoutAnmPlayer* > mAnmPlayerArray;
};
