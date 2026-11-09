#pragma once

#include "Game/Ui/UiCommon.hpp"
#include "Game/Ui/Pane.hpp"
#include <cstddef>

namespace Ui {
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
        NameText< 17 > mName;
    };

    typedef UiList< Group > GroupList;

    class GroupContainer {
    public:
        GroupContainer() {
        }

        ~GroupContainer();

        void AppendGroup(Group*);


        GroupList mGroupList;
    };
}  // namespace Ui
