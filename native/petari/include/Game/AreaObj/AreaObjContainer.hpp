#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/NameIndex.hpp"
#include <nebula_geometry.hpp>

class AreaObj;
class AreaObjMgr;

typedef AreaObjMgr* (*ManagerCreatorFuncPtr)(s32, const char*);

struct Name2CreateManagerFunc {
    const char* mName;
    s32 mFirstCapacity;  // the manager grows past it
    ManagerCreatorFuncPtr mCreateFunc;
};

class AreaObjContainer : public NameObj {
public:
    AreaObjContainer(const char* pName);

    /// @brief Intializes the `AreaObjContainer` while being placed into a scene.
    /// @param rIter The reference to an iterator over a `Table`.
    virtual void init(const TableIter& rIter) override;

    AreaObjMgr* getManager(const char*) const;
    // The kinds of area the game makes (its manager table), for the area registry (LumaRegistry).
    static u32 getKindNum();
    static const char* getKindName(u32 index);
    AreaObj* getAreaObj(const char*, const Vec3f&) const;

private:
    MR::Vector< MR::AssignableArray< AreaObjMgr* > > mManagers;
    MR::NameIndex mIndex;  // manager name -> index in mManagers
    bool mHashesShared;    // two manager names share a hash: lookups scan
};

namespace MR {
    AreaObjContainer* getAreaObjContainer();

    template < typename T >
    T* getAreaObj(const char* pName, const Vec3f& rVec) {
        return static_cast< T* >(getAreaObjContainer()->getAreaObj(pName, rVec));
    }
};  // namespace MR
