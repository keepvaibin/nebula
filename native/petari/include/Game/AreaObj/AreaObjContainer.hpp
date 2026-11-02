#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class AreaObj;
class AreaObjMgr;

typedef AreaObjMgr* (*ManagerCreatorFuncPtr)(s32, const char*);

struct Name2CreateManagerFunc {
    const char* mName;
    s32 _4;
    ManagerCreatorFuncPtr mCreateFunc;
};

class AreaObjContainer : public NameObj {
public:
    /// @brief Creates a new `AreaObjContainer`.
    /// @param pName A pointer to the null-terminated name of the object.
    AreaObjContainer(const char* pName);

    /// @brief Intializes the `AreaObjContainer` while being placed into a scene.
    /// @param rIter The reference to an iterator over a `JMapInfo`.
    virtual void init(const JMapInfoIter& rIter);

    AreaObjMgr* getManager(const char*) const;
    AreaObj* getAreaObj(const char*, const TVec3f&) const;

private:
    AreaObjMgr* mManagerArray[0x50];
    u32 mNumManagers;
};

namespace MR {
    AreaObjContainer* getAreaObjContainer();

    template < typename T >
    T* getAreaObj(const char* pName, const TVec3f& rVec) {
        return static_cast< T* >(getAreaObjContainer()->getAreaObj(pName, rVec));
    }
};  // namespace MR
