#pragma once

#include "Game/NPC/NPCActor.hpp"
#include "Game/NPC/NPCActorItem.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/Table.hpp"
#include <nebula_geometry.hpp>

class Table;
class NPCActorItem;

class NPCParameterBase {
public:
    NPCParameterBase(const char* pName) {
        mName = pName;
    };

    const char* mName;

    virtual void read(Table*, s32);
};

// One column of an NPC's parameter table (bool, s32, f32 or a name), read into the field it is named after.
template < typename T >
class NPCParameterValue : public NPCParameterBase {
public:
    NPCParameterValue(const char* pName, T* pValue) : NPCParameterBase(pName), mValue(pValue) {}

    virtual void read(Table* pInfo, s32 entry) { pInfo->getValue(entry, mName, mValue); }

    T* mValue;
};

// Three columns, the name followed by X, Y and Z.
class NPCParameterV3f : public NPCParameterBase {
public:
    NPCParameterV3f(const char*, Vec3f*);

    virtual void read(Table*, s32);

    Vec3f* mValue;
};

class NPCParameterReader : public NPCParameterBase, public MR::Vector< MR::FixedArray< NPCParameterBase*, 48 > > {
public:
    NPCParameterReader(const char*);

    virtual void read(Table*, s32);
};

class NPCCapsParameterReader : public NPCParameterReader {
public:
    NPCCapsParameterReader(const char*);

    void init();

    NPCActorCaps mActorCaps;
};

class NPCItemParameterReader : public NPCParameterReader {
public:
    NPCItemParameterReader(const char*);

    void copy(const NPCActorItem*);
    NPCActorItem mItem;
};
