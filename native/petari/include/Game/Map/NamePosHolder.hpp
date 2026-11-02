#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class JMapLinkInfo;

/// @brief Class that contains the NamePos attributes. NamePos defines the spawn location of other actors (ie Power Stars)
class NamePosInfo {
public:
    NamePosInfo();

    const char* mName;         ///< The name of the NamePos.
    TVec3f mPosition;          ///< The position of the NamePos.
    TVec3f mRotation;         ///< The rotation of the NamePos.
    JMapLinkInfo* mLinkInfo;  ///< The information to link this NamePos to other objects.
    const NameObj* _20;
};

/// @brief Class that contains the array of NamePosInfo instances.
class NamePosHolder : public NameObj {
public:
    NamePosHolder();

    bool tryRegisterLinkObj(const NameObj*, const JMapInfoIter&);
    bool find(const NameObj*, const char*, TVec3f*, TVec3f*) const;

    s32 mPosNum;           ///< The number of NamePosInfo instances.
    NamePosInfo* mInfos;  ///< The array of NamePosInfo instances.
};

namespace MR {
    NamePosHolder* getNamePosHolder();
};  // namespace MR
