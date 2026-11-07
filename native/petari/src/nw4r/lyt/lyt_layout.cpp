#include "nw4r/lyt/animation.h"
#include "nw4r/lyt/bounding.h"
#include "nw4r/lyt/common.h"
#include "nw4r/lyt/group.h"
#include "nw4r/lyt/layout.h"
#include "nw4r/lyt/material.h"
#include "nw4r/lyt/pane.h"
#include "nw4r/lyt/picture.h"
#include "nw4r/lyt/textBox.h"
#include "nw4r/lyt/util.h"
#include "nw4r/lyt/window.h"
#include "nw4r/ut/Rect.h"

namespace {
    using namespace nw4r;
    using namespace nw4r::lyt;
    void SetTagProcessorImpl(Pane* pPane, ut::TagProcessorBase< char16_t >* pTagProcessor) {
        if (TextBox* pTextBox = ut::DynamicCast< TextBox* >(pPane)) {
            pTextBox->SetTagProcessor(pTagProcessor);
        }

        for (PaneList::Iterator it = pPane->GetChildList().GetBeginIter(); it != pPane->GetChildList().GetEndIter(); ++it) {
            SetTagProcessorImpl(&(*it), pTagProcessor);
        }
    }
}  // namespace

namespace nw4r {
    namespace lyt {
        Layout::Layout() : mpRootPane(nullptr), mpGroupContainer(nullptr), mLayoutSize(0.0f, 0.0f), _20(0) {
        }

        Layout::~Layout() {
            if (mpGroupContainer) {
                mpGroupContainer->~GroupContainer();
                FreeMemory(mpGroupContainer);
            }

            if (mpRootPane != nullptr && !mpRootPane->IsUserAllocated()) {
                mpRootPane->~Pane();
                FreeMemory(mpRootPane);
            }

            for (AnimTransformList::Iterator it = mAnimTransList.GetBeginIter(); it != mAnimTransList.GetEndIter();) {
                AnimTransformList::Iterator currIt = it++;
                mAnimTransList.Erase(currIt);
                currIt->~AnimTransform();
                FreeMemory(&*currIt);
            }
        }

        // A cooked layout (UiData.hpp): panes in tree order, so each one's parent is built before it; groups name their
        // panes by index (the first of each name, as FindPaneByName from the root found them).
        bool Layout::Build(const void* lytResBuf, ResourceAccessor* pResAcsr) {
            const UiLayoutHeader* const pHead = static_cast< const UiLayoutHeader* >(lytResBuf);
            if (pHead->mMagic != cUiLayoutMagic || pHead->mVersion != cUiDataVersion) {
                return false;
            }

            _20 = (pHead->mFlags & UiLayoutFlag_Centered) != 0 ? ORIGINTYPE_CENTER : ORIGINTYPE_TOPLEFT;
            mLayoutSize = Size(pHead->mWidth, pHead->mHeight);

            Pane** const ppPanes = static_cast< Pane** >(AllocMemory(sizeof(Pane*) * (pHead->mPaneCount + 1)));
            const ResBlockSet resBlockSet = {pHead, pResAcsr, ppPanes};
            for (u32 i = 0; i < pHead->mPaneCount; ++i) {
                const UiPaneData* const pRes = resBlockSet.PaneAt(i);
                Pane* const pPane = BuildPaneObj(pRes, resBlockSet);
                ppPanes[i] = pPane;
                if (pPane == nullptr) {
                    continue;
                }
                if (mpRootPane == nullptr) {
                    mpRootPane = pPane;
                }
                if (pRes->mParent != cUiNone16 && ppPanes[pRes->mParent] != nullptr) {
                    ppPanes[pRes->mParent]->AppendChild(pPane);
                }
            }

            if ((pHead->mFlags & UiLayoutFlag_GroupContainer) != 0) {
                mpGroupContainer = NewObj< GroupContainer >();
                const UiGroupData* const pGroups = resBlockSet.At< UiGroupData >(pHead->mGroups);
                for (u32 i = 0; mpGroupContainer != nullptr && i < pHead->mGroupCount; ++i) {
                    if (Group* pGroup = NewObj< Group >(&pGroups[i], resBlockSet)) {
                        mpGroupContainer->AppendGroup(pGroup);
                    }
                }
            }

            FreeMemory(ppPanes);
            return true;
        }

        AnimTransform* Layout::CreateAnimTransform() {
            AnimTransformBasic* const pAnimTrans = NewObj< AnimTransformBasic >();
            if (pAnimTrans != nullptr) {
                mAnimTransList.PushBack(pAnimTrans);
            }
            return pAnimTrans;
        }

        AnimTransform* Layout::CreateAnimTransform(const void* animResBuf, ResourceAccessor* pResAcsr) {
            return CreateAnimTransform(AnimResource(animResBuf), pResAcsr);
        }

        AnimTransform* Layout::CreateAnimTransform(const AnimResource& animRes, ResourceAccessor* pResAcsr) {
            const UiAnimationHeader* const pAnimBlock = animRes.GetResourceBlock();

            if (pAnimBlock == nullptr) {
                return nullptr;
            }

            AnimTransform* const pAnimTrans = CreateAnimTransform();
            if (pAnimTrans != nullptr) {
                pAnimTrans->SetResource(pAnimBlock, pResAcsr);
            }
            return pAnimTrans;
        }

        void Layout::BindAnimation(AnimTransform* pAnimTrans) {
            if (mpRootPane != nullptr) {
                mpRootPane->BindAnimation(pAnimTrans, true);
            }
        }

        void Layout::UnbindAnimation(AnimTransform* pAnimTrans) {
            if (mpRootPane != nullptr) {
                mpRootPane->UnbindAnimation(pAnimTrans, true);
            }
        }

        void Layout::UnbindAllAnimation() {
            UnbindAnimation(0);
        }

        void Layout::SetAnimationEnable(AnimTransform* pAnimTrans, bool bEnable) {
            if (mpRootPane != nullptr) {
                mpRootPane->SetAnimationEnable(pAnimTrans, bEnable, true);
            }
        }

        void Layout::CalculateMtx(const DrawInfo& rInfo) {
            if (mpRootPane == nullptr) {
                return;
            }

            mpRootPane->CalculateMtx(rInfo);
        }

        void Layout::Draw(const DrawInfo& rInfo) {
            if (mpRootPane == nullptr) {
                return;
            }

            mpRootPane->Draw(rInfo);
        }

        void Layout::Animate(u32 opt) {
            if (mpRootPane == nullptr) {
                return;
            }

            mpRootPane->Animate(opt);
        }

        const ut::Rect Layout::GetLayoutRect() const {
            if (_20 == 1) {
                return ut::Rect(-mLayoutSize.width / 2, mLayoutSize.height / 2, mLayoutSize.width / 2, -mLayoutSize.height / 2);
            }

            return ut::Rect(0.0f, 0.0f, mLayoutSize.width, mLayoutSize.height);
        }

        void Layout::SetTagProcessor(ut::TagProcessorBase< char16_t >* pTagProcessor) {
            SetTagProcessorImpl(mpRootPane, pTagProcessor);
        }
        Pane* Layout::BuildPaneObj(const UiPaneData* pRes, const ResBlockSet& resBlockSet) {
            switch (pRes->mKind) {
            case UiPaneKind_Pane:
                return NewObj< Pane >(pRes, resBlockSet);
            case UiPaneKind_Picture:
                return NewObj< Picture >(pRes, resBlockSet);
            case UiPaneKind_TextBox:
                return NewObj< TextBox >(pRes, resBlockSet);
            case UiPaneKind_Window:
                return NewObj< Window >(pRes, resBlockSet);
            case UiPaneKind_Bounding:
                return NewObj< Bounding >(pRes, resBlockSet);
            }
            return nullptr;
        }
    }  // namespace lyt
}  // namespace nw4r
