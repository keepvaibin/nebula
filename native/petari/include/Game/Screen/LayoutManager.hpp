#pragma once

#include <JSystem/JGeometry/TVec.hpp>
#include <nw4r/lyt/drawInfo.h>
#include <nw4r/lyt/layout.h>
#include <nw4r/lyt/pane.h>

namespace nw4r {
    namespace lyt {
        class AnimTransform;
        class Group;
        class Layout;
        class Pane;
        class TexMap;
    };  // namespace lyt
};  // namespace nw4r

class LayoutGroupCtrl;
class LayoutHolder;
class LayoutPaneCtrl;

struct LayoutGroupCtrlLink {
    LayoutGroupCtrl* mGroupCtrl;
    LayoutGroupCtrlLink* mNext;
};

struct LayoutPaneInfo {
    const char* mName;
    LayoutPaneCtrl* mPaneCtrl;
    LayoutGroupCtrlLink* mGroupCtrlLink;
    MtxPtr mMtxRef;
    u32 mChildCount;
    nw4r::lyt::Pane* mPane;
};

class LayoutManager {
public:
    LayoutManager(const char*, bool, u32, u32);

    void movement();
    void calcAnim();
    void draw() const;
    void addPaneCtrl(LayoutPaneCtrl*);
    LayoutPaneCtrl* createAndAddRootPaneCtrl(u32);
    LayoutPaneCtrl* createAndAddPaneCtrl(const char*, u32);
    LayoutPaneCtrl* getPaneCtrl(const char*) const;
    s32 getIndexOfPane(const char*) const;
    bool isExistPaneCtrl(const char*) const;
    void addGroupCtrl(LayoutGroupCtrl*);
    bool isPointing(const nw4r::lyt::Pane*, const TVec2f&) const;
    LayoutGroupCtrl* createAndAddGroupCtrl(const char*, u32);
    s32 getIndexOfGroupCtrl(const char*) const;
    void createPaneMtxRef(const char*);
    MtxPtr getPaneMtxRef(const char*) const;
    bool isExistPaneMtxRef(const char*) const;
    bool isPointing(const char*, const TVec2f&) const;
    nw4r::lyt::AnimTransform* getAnimTransform(const char*) const;
    void bindPaneCtrlAnim(LayoutPaneCtrl*, nw4r::lyt::AnimTransform*);
    void bindPaneCtrlAnimSub(u32&, nw4r::lyt::AnimTransform*);
    void unbindPaneCtrlAnim(LayoutPaneCtrl*, nw4r::lyt::AnimTransform*);
    void unbindPaneCtrlAnimSub(u32&, nw4r::lyt::AnimTransform*);
    void calcAnimWithoutLocationAdjust(const nw4r::lyt::DrawInfo&);
    nw4r::lyt::Group* getGroup(const char*) const;
    void initArc(const char*, const char*);
    void initDrawInfo();
    void initPaneInfo();
    // The pane list again after code changed the layout's tree (moved or added panes); before any pane or group
    // control exists.
    void rebuildPaneInfo();
    void initPaneInfoRecursive(u32&, nw4r::lyt::Pane*);
    u32 countPanes(nw4r::lyt::Pane*);
    void initGroupCtrlList();
    void initTextBoxRecursive(nw4r::lyt::Pane*, nw4r::lyt::Pane*, const char*, u32);
    void animateRecursive(u32&, nw4r::lyt::Pane*);
    nw4r::lyt::Pane* getPane(const char* pName) const {
        if (pName == nullptr) {
            return mLayout->mpRootPane;
        } else {
            return mLayout->mpRootPane->FindPaneByName(pName, true);
        }
    }

    nw4r::lyt::Pane* findPaneByName(const char*) const;
    void replaceIndDummyTexture();
    void removeUnnecessaryPanes(nw4r::lyt::Pane*);

    LayoutHolder* mLayoutHolder;
    nw4r::lyt::Layout* mLayout;
    nw4r::lyt::AnimTransform** mAnimTransList;
    nw4r::lyt::DrawInfo mDrawInfo;
    bool mIsScreenHidden;
    bool _61;
    nw4r::lyt::TexMap* mIndDummyTexMap;
    u32 mPaneCount;
    LayoutPaneInfo* mPaneInfoList;
    u32 mGroupCtrlCount;
    LayoutGroupCtrl** mGroupCtrlList;
    char* mLayoutName;
    // LayoutAspect::fit's rules for this layout and their panes (count -1 until looked up).
    const void* mFitRules;
    s32 mFitRuleCount;
    nw4r::lyt::Pane* mFitPanes[8];
};
