#pragma once

#include "Game/Util/Array.hpp"
#include <nebula_geometry.hpp>
#include <nw4r/lyt/pane.h>

class FrameCtrl;
class LayoutAnmPlayer;
class LayoutManager;

// A pane's animation layers, and its follow: with a follow position (MR::setFollowPos) it is its pane's PaneFollow,
// placing the pane on that point in the layout's matrix pass before the pane's children are placed.
class LayoutPaneCtrl : public nw4r::lyt::PaneFollow {
public:
    LayoutPaneCtrl(LayoutManager* pHost, const char* pPaneName, u32 animLayerNum);

    void movement();
    void calcAnim();
    void start(const char* pAnimName, u32 layer);
    void stop(u32 layer);
    bool isAnimStopped(u32 layer) const;
    void Follow(const nw4r::lyt::Pane& rPane, nw4r::math::MTX34& rWorld) const override;
    // Follows pFollowPos from the next pass on; null stops.
    void setFollowPos(const Vec2f* pFollowPos);
    FrameCtrl* getFrameCtrl(u32 layer) const;

    LayoutManager* mHost;
    nw4r::lyt::Pane* mPane;
    // The pane whose state holds this control (LayoutManager::addPaneCtrl): its branch is what bind and unbind walk.
    nw4r::lyt::Pane* mStatePane;
    MR::AssignableArray< LayoutAnmPlayer* > mAnmPlayerArray;
    s32 mFollowType;
    const Vec2f* mFollowPos;
};
