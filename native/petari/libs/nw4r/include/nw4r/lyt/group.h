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

        class Layout;

        // A layout's group: the panes the cook named for it, in order. It, its links and the container are the
        // layout's arena objects (Layout::NewInArena) and go with it.
        class Group {
        public:
            Group(Layout&, const UiGroupData*, const ResBlockSet&);

            virtual ~Group();

            const char* GetName() const {
                return mName;
            }

            PaneLinkList& GetPaneList() {
                return mPaneLinkList;
            }

            UiLink< Group > mLink;
            PaneLinkList mPaneLinkList;
            char mName[17];
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
