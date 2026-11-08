#pragma once

#include <nebula_geometry.hpp>
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

// What the manager keeps for one pane, by the pane's id in its layout (Pane::GetId): it stays with the pane through
// every tree edit.
struct LayoutPaneState {
    LayoutPaneCtrl* mPaneCtrl;
    LayoutGroupCtrlLink* mGroupCtrlLink;
    MtxPtr mMtxRef;
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
    // The pane named exactly pName (the first in tree order), or the root when there is none: whose state a
    // control, group link or matrix copy for that name goes to.
    nw4r::lyt::Pane* findPaneOrRoot(const char*) const;
    // The pane's state; null when nothing was kept for it.
    const LayoutPaneState* findState(const nw4r::lyt::Pane*) const;
    // The pane's state, made when there is none yet.
    LayoutPaneState& stateOf(const nw4r::lyt::Pane*);
    bool isExistPaneCtrl(const char*) const;
    void addGroupCtrl(LayoutGroupCtrl*);
    bool isPointing(const nw4r::lyt::Pane*, const Vec2f&) const;
    LayoutGroupCtrl* createAndAddGroupCtrl(const char*, u32);
    s32 getIndexOfGroupCtrl(const char*) const;
    void createPaneMtxRef(const char*);
    MtxPtr getPaneMtxRef(const char*) const;
    bool isExistPaneMtxRef(const char*) const;
    bool isPointing(const char*, const Vec2f&) const;
    nw4r::lyt::AnimTransform* getAnimTransform(const char*) const;
    // A control's animation on its pane's branch, down to (not into) the next pane that has a control of its own.
    void bindPaneCtrlAnim(LayoutPaneCtrl*, nw4r::lyt::AnimTransform*);
    void unbindPaneCtrlAnim(LayoutPaneCtrl*, nw4r::lyt::AnimTransform*);
    void calcAnimWithoutLocationAdjust(const nw4r::lyt::DrawInfo&);
    // Every matrix copy (LayoutPaneState::mMtxRef) from its pane's global matrix, in screen units.
    void copyPaneMtxRefs();
    static void copyPaneMtxRef(const nw4r::lyt::Pane*, MtxPtr);
    nw4r::lyt::Group* getGroup(const char*) const;
    void initArc(const char*, const char*);
    void initDrawInfo();
    void initGroupCtrlList();
    void initTextBoxRecursive(nw4r::lyt::Pane*, nw4r::lyt::Pane*, const char*, u32);
    // Controls and group controls in tree order, each pane's own animations after its controls'.
    void animatePanes();
    // The first pane in tree order whose name matches pName's first 16 characters (nw4r's EqualsResName); the root
    // for null.
    nw4r::lyt::Pane* getPane(const char* pName) const;
    // The first pane in tree order named exactly pName.
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
    // By pane id; grows with the layout (Layout::GetPaneNum).
    LayoutPaneState* mPaneStates = nullptr;
    u32 mPaneStateNum = 0;
    u32 mGroupCtrlCount;
    LayoutGroupCtrl** mGroupCtrlList;
    char* mLayoutName;
    // This layout's anchors (LayoutAspect::prepare; count -1 until resolved), the layout's pass applies them.
    nw4r::lyt::Anchor mAnchors[16];
    s32 mAnchorCount;
};
