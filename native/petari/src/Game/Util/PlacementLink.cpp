#include "Game/Util/PlacementLink.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"

namespace {
    const char* const sFollowInfoTagName[3] = {"MapParts_ID", "Obj_ID", "ChildObjId"};
    const char* const sInfoNameTable[3] = {"mappartsinfo", "objinfo", "childobjinfo"};

    s32 getInfoType(const Table* pInfo) {
        const char* pName = pInfo->getName();

        for (s32 i = 0; i < 3; i++) {
            if (MR::isEqualString(pName, sInfoNameTable[i])) {
                return i;
            }
        }

        return -1;
    }
};  // namespace

PlacementLink::PlacementLink(const TableIter& rIter, bool linked) {
    mLinkId = -1;
    mZoneId = -1;
    mInfoType = -1;

    if (linked) {
        setLinkedInfo(rIter);
    } else {
        setLinkInfo(rIter);
    }
}

bool PlacementLink::isValid() const {
    return mLinkId >= 0 && mZoneId >= 0 && mInfoType >= 0;
}

void PlacementLink::invalidate() {
    mLinkId = -1;
    mZoneId = -1;
    mInfoType = -1;
}

void PlacementLink::setLinkedInfo(const TableIter& rIter) {
    invalidate();

    if (rIter.isValid()) {
        s32 id = -1;
        if (rIter.getValue< s32 >("l_id", &id)) {
            mLinkId = id;
            mZoneId = MR::getPlacedZoneId(rIter);

            mInfoType = ::getInfoType(rIter.mInfo);
        }
    }
}

void PlacementLink::setLinkInfo(const TableIter& rIter) {
    invalidate();

    if (rIter.isValid()) {
        mZoneId = MR::getPlacedZoneId(rIter);

        s32 linkId = -1;
        s32 infoType = -1;
        bool found = false;

        for (s32 i = 0; i < 3; i++) {
            s32 id = -1;
            rIter.getValue< s32 >(::sFollowInfoTagName[i], &id);

            if (id >= 0 && !found) {
                linkId = id;
                infoType = i;
                found = true;
            }
        }

        if (found) {
            mLinkId = linkId;
            mInfoType = infoType;
            mZoneId = MR::getPlacedZoneId(rIter);
        }
    }
}
