#include "Game/Ui/Group.hpp"
#include "Game/Ui/Layout.hpp"
#include <cstring>

namespace Ui {
    // The cook resolved each member name to the first pane of that name in tree order (FindPaneByName from the root).
    Group::Group(Layout& rLayout, const UiGroupData* pResGroup, const ResBlockSet& rBlockSet) {
        mName.set(rBlockSet.String(pResGroup->mName));

        const u16* const pMembers = rBlockSet.At< u16 >(pResGroup->mPanes);
        for (u32 i = 0; i < pResGroup->mPaneCount; ++i) {
            if (Pane* pFindPane = rBlockSet.ppPanes[pMembers[i]]) {
                detail::PaneLink* const pPaneLink = new (rLayout.ArenaAlloc(sizeof(detail::PaneLink))) detail::PaneLink;
                pPaneLink->mTarget = pFindPane;
                mPaneLinkList.PushBack(pPaneLink);
            }
        }
    }

    Group::~Group() {
    }

    GroupContainer::~GroupContainer() {
    }

    void GroupContainer::AppendGroup(Group* pGroup) {
        mGroupList.PushBack(pGroup);
    }

    Group* GroupContainer::FindGroupByName(const char* findName) {
        for (Group* pGroup : mGroupList) {
            if (detail::EqualsResName(pGroup->GetName(), findName)) {
                return pGroup;
            }
        }

        return nullptr;
    }
}  // namespace Ui
