#pragma once

#include "nw4r/lyt/common.h"
#include "nw4r/lyt/pane.h"
#include <cstddef>

namespace nw4r {
    namespace lyt {
        namespace detail {
            struct PaneLink {
                UiLink< PaneLink > mLink;
                Pane* mTarget;
            };
        };  // namespace detail

        typedef UiList< detail::PaneLink > PaneLinkList;

        class Group {
        public:
            Group(const UiGroupData*, const ResBlockSet&);

            virtual ~Group();

            void Init();

            const char* GetName() const {
                return mName;
            }

            PaneLinkList& GetPaneList() {
                return mPaneLinkList;
            }

            inline void AppendPane(Pane*);

            bool IsUserAllocated() const {
                return mbUserAllocated;
            }

            UiLink< Group > mLink;
            PaneLinkList mPaneLinkList;
            char mName[17];
            u8 mbUserAllocated;
            u8 mPadding[2];
        };

        typedef UiList< Group > GroupList;

        class GroupContainer {
        public:
            GroupContainer() {
            }

            ~GroupContainer();

            void AppendGroup(Group*);

            Group* FindGroupByName(const char*);

            GroupList mGroupList;
        };
    };  // namespace lyt
};  // namespace nw4r
