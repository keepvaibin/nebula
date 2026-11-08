#pragma once

#include "Game/Util/Array.hpp"
#include <nebula_geometry.hpp>
#include <Game/Ui/Pane.hpp>

class FrameCtrl;
class LayoutAnmPlayer;
class LayoutManager;

// A pane's animation layers, and its follow: with a follow position (MR::setFollowPos) it is its pane's PaneFollow,
// placing the pane on that point in the layout's matrix pass before the pane's children are placed.
class LayoutPaneCtrl : public Ui::PaneFollow {
public:
    LayoutPaneCtrl(LayoutManager* pHost, const char* pPaneName, u32 animLayerNum);

    void movement();
    void calcAnim();
    void start(const char* pAnimName, u32 layer);
    void stop(u32 layer);
    bool isAnimStopped(u32 layer) const;
    void Follow(const Ui::Pane& rPane, Mtx34f& rWorld) const override;
    // Follows pFollowPos from the next pass on; null stops.
    void setFollowPos(const Vec2f* pFollowPos);
    FrameCtrl* getFrameCtrl(u32 layer) const;

    LayoutManager* mHost;
    Ui::Pane* mPane;
    // The pane whose state holds this control (LayoutManager::addPaneCtrl): its branch is what bind and unbind walk.
    Ui::Pane* mStatePane;
    MR::AssignableArray< LayoutAnmPlayer* > mAnmPlayerArray;
    s32 mFollowType;
    const Vec2f* mFollowPos;
};
