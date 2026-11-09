#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/NameIndex.hpp"
#include <nebula_geometry.hpp>

class PlacementLink;

/// @brief Class that contains the NamePos attributes. NamePos defines the spawn location of other actors (ie Power Stars)
class NamePosInfo {
public:
    NamePosInfo();

    const char* mName;         ///< The name of the NamePos.
    Vec3f mPosition;          ///< The position of the NamePos.
    Vec3f mRotation;         ///< The rotation of the NamePos.
    PlacementLink* mLinkInfo;  ///< The information to link this NamePos to other objects.
    const NameObj* mRegisteredObj;
};

/// @brief Class that contains the array of NamePosInfo instances.
class NamePosHolder : public NameObj {
public:
    NamePosHolder();

    bool tryRegisterLinkObj(const NameObj*, const TableIter&);
    bool find(const NameObj*, const char*, Vec3f*, Vec3f*) const;

    s32 mPosNum;           ///< The number of NamePosInfo instances.
    NamePosInfo* mInfos;  ///< The array of NamePosInfo instances.
    MR::NameIndex mIndex;  // name -> its first entry
    s32* mNextSame;        // per entry: the next entry of the same name, or -1
};

namespace MR {
    NamePosHolder* getNamePosHolder();
};  // namespace MR
