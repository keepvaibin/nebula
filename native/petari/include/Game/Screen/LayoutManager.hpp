#pragma once

#include <nebula_geometry.hpp>
#include <Game/Ui/DrawInfo.hpp>
#include <Game/Ui/Layout.hpp>
#include <Game/Ui/Pane.hpp>
#include <Game/Util/Array.hpp>

namespace Ui {
    class AnimTransform;
    class Group;
    class Layout;
    class Pane;
    class TexMap;
}  // namespace Ui

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
    // keepLayoutText: a text box with no message stays as its layout file made it (not the game's XXXXXXXX in 0x100).
    LayoutManager(const char*, bool, u32, u32, bool keepLayoutText = false);

    void movement();
    void calcAnim();
    void draw() const;
    // The life the layout's panes draw under (LayoutActor::draw passes its own).
    void setDrawGeneration(u32 generation) const;
    void addPaneCtrl(LayoutPaneCtrl*);
    LayoutPaneCtrl* createAndAddRootPaneCtrl(u32);
    LayoutPaneCtrl* createAndAddPaneCtrl(const char*, u32);
    LayoutPaneCtrl* getPaneCtrl(const char*) const;
    // The pane named exactly pName (the first in tree order), or the root when there is none: whose state a
    // control, group link or matrix copy for that name goes to.
    Ui::Pane* findPaneOrRoot(const char*) const;
    // The pane's state; null when nothing was kept for it.
    const LayoutPaneState* findState(const Ui::Pane*) const;
    // The pane's state, made when there is none yet.
    LayoutPaneState& stateOf(const Ui::Pane*);
    bool isExistPaneCtrl(const char*) const;
    void addGroupCtrl(LayoutGroupCtrl*);
    bool isPointing(const Ui::Pane*, const Vec2f&) const;
    LayoutGroupCtrl* createAndAddGroupCtrl(const char*, u32);
    s32 getIndexOfGroupCtrl(const char*) const;
    void createPaneMtxRef(const char*);
    MtxPtr getPaneMtxRef(const char*) const;
    bool isExistPaneMtxRef(const char*) const;
    bool isPointing(const char*, const Vec2f&) const;
    Ui::AnimTransform* getAnimTransform(const char*) const;
    // A control's animation on its pane's branch, down to (not into) the next pane that has a control of its own.
    void bindPaneCtrlAnim(LayoutPaneCtrl*, Ui::AnimTransform*);
    void unbindPaneCtrlAnim(LayoutPaneCtrl*, Ui::AnimTransform*);
    void calcAnimWithoutLocationAdjust(const Ui::DrawInfo&);
    // Every matrix copy (LayoutPaneState::mMtxRef) from its pane's global matrix, in screen units.
    void copyPaneMtxRefs();
    static void copyPaneMtxRef(const Ui::Pane*, MtxPtr);
    Ui::Group* getGroup(const char*) const;
    void initArc(const char*, const char*);
    void initDrawInfo();
    void initGroupCtrlList();
    void initTextBoxRecursive(Ui::Pane*, Ui::Pane*, const char*, u32, bool keepLayoutText);
    // Controls and group controls in tree order, each pane's own animations after its controls'.
    void animatePanes();
    // The first pane in tree order whose name matches pName's first 16 characters (Ui::detail::EqualsResName); the root
    // for null.
    Ui::Pane* getPane(const char* pName) const;
    // The first pane in tree order named exactly pName.
    Ui::Pane* findPaneByName(const char*) const;
    void replaceIndDummyTexture();
    void removeUnnecessaryPanes(Ui::Pane*);

    LayoutHolder* mLayoutHolder;
    Ui::Layout* mLayout;
    Ui::AnimTransform** mAnimTransList;
    Ui::DrawInfo mDrawInfo;
    bool mIsScreenHidden;
    bool mParentAnimValid; // pane animations bind through the pane to its children; false (MR::invalidateParentAnim):
                           // through this manager's own pane list
    Ui::TexMap* mIndDummyTexMap;
    // By pane id; grows with the layout (Layout::GetPaneNum).
    LayoutPaneState* mPaneStates = nullptr;
    u32 mPaneStateNum = 0;
    u32 mGroupCtrlCount;
    LayoutGroupCtrl** mGroupCtrlList;
    char* mLayoutName;
    // This layout's anchors (LayoutAspect::prepare, once: mAnchorsResolved), the layout's pass applies them.
    MR::InlineVector< Ui::Anchor, 16 > mAnchors;
    bool mAnchorsResolved = false;
};
