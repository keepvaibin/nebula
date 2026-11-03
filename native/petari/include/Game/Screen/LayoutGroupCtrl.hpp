#pragma once

#include "Game/Util/Array.hpp"

namespace nw4r {
    namespace lyt {
        class Pane;
        class Group;
    };  // namespace lyt
};  // namespace nw4r

class LayoutAnmPlayer;
class LayoutManager;

class LayoutGroupCtrl {
public:
    LayoutGroupCtrl(LayoutManager* pManager, const char* pName, u32 animLayerCount);

    void movement();
    void calcAnim();
    u32 getPaneNum() const;
    nw4r::lyt::Pane* getPane(u32 idx) const;

    LayoutManager* mManager;
    nw4r::lyt::Group* mGroup;
    MR::AssignableArray< LayoutAnmPlayer* > mAnmPlayerArray;
    bool _10;
};
