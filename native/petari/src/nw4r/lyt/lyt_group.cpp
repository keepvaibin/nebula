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
            for (detail::PaneLink *pLink = mPaneLinkList.First(), *pNext; pLink != nullptr; pLink = pNext) {
                pNext = PaneLinkList::Next(pLink);
                mPaneLinkList.Erase(pLink);
                Layout::FreeMemory(pLink);
            }
        }

        GroupContainer::~GroupContainer() {
            for (Group *pGroup = mGroupList.First(), *pNext; pGroup != nullptr; pGroup = pNext) {
                pNext = GroupList::Next(pGroup);
                mGroupList.Erase(pGroup);
                if (!pGroup->IsUserAllocated()) {
                    pGroup->~Group();
                    Layout::FreeMemory(pGroup);
                }
            }
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
    };  // namespace lyt
};  // namespace nw4r
