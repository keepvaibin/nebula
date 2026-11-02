#pragma once

#include "Game/Util/Array.hpp"
#include <JSystem/JGeometry.hpp>

namespace nw4r {
    namespace lyt {
        class Pane;
    };  // namespace lyt
};  // namespace nw4r

class J3DFrameCtrl;
class LayoutAnmPlayer;
class LayoutManager;

class LayoutPaneCtrl {
public:
    LayoutPaneCtrl(LayoutManager* pHost, const char* pPaneName, u32 animLayerNum);

    void movement();
    void calcAnim();
    void start(const char* pAnimName, u32 layer);
    void stop(u32 layer);
    bool isAnimStopped(u32 layer) const;
    void reflectFollowPos();
    J3DFrameCtrl* getFrameCtrl(u32 layer) const;
    void recalcChildGlobalMtx(nw4r::lyt::Pane* pPane);

    LayoutManager* mHost;
    nw4r::lyt::Pane* mPane;
    s32 mPaneIndex;
    MR::AssignableArray< LayoutAnmPlayer* > mAnmPlayerArray;
    s32 mFollowType;
    const TVec2f* mFollowPos;
};
