#include "nw4r/lyt/group.h"
#include "nw4r/lyt/layout.h"
#include <cstring>

namespace nw4r {
    namespace lyt {
        void Group::AppendPane(Pane* pPane) {
            if (void* pMem = Layout::AllocMemory(sizeof(detail::PaneLink))) {
                detail::PaneLink* pPaneLink = new (pMem) detail::PaneLink;
                pPaneLink->mTarget = pPane;
                mPaneLinkList.PushBack(pPaneLink);
            }
        }

        void Group::Init() {
            mbUserAllocated = false;
        }

        // The cook resolved each member name to the first pane of that name in tree order (FindPaneByName from the root).
        Group::Group(const UiGroupData* pResGroup, const ResBlockSet& rBlockSet) {
            Init();
            strncpy(mName, rBlockSet.String(pResGroup->mName), sizeof(mName) - 1);
            mName[sizeof(mName) - 1] = '\0';

            const u16* const pMembers = rBlockSet.At< u16 >(pResGroup->mPanes);
            for (u32 i = 0; i < pResGroup->mPaneCount; ++i) {
                if (Pane* pFindPane = rBlockSet.ppPanes[pMembers[i]]) {
                    AppendPane(pFindPane);
                }
            }
        }

        Group::~Group() {
            for (PaneLinkList::Iterator it = mPaneLinkList.GetBeginIter(); it != mPaneLinkList.GetEndIter();) {
                PaneLinkList::Iterator currIt = it++;
                mPaneLinkList.Erase(currIt);
                Layout::FreeMemory(&*currIt);
            }
        }

        GroupContainer::~GroupContainer() {
            for (GroupList::Iterator it = mGroupList.GetBeginIter(); it != mGroupList.GetEndIter();) {
                GroupList::Iterator currIt = it++;
                mGroupList.Erase(currIt);
                if (!currIt->IsUserAllocated()) {
                    currIt->~Group();
                    Layout::FreeMemory(&*currIt);
                }
            }
        }

        void GroupContainer::AppendGroup(Group* pGroup) {
            mGroupList.PushBack(pGroup);
        }

        Group* GroupContainer::FindGroupByName(const char* findName) {
            for (GroupList::Iterator it = mGroupList.GetBeginIter(); it != mGroupList.GetEndIter(); ++it) {
                if (detail::EqualsResName(it->GetName(), findName)) {
                    return &(*it);
                }
            }

            return nullptr;
        }
    };  // namespace lyt
};  // namespace nw4r
