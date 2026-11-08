#pragma once

#include "Game/Util/Array.hpp"

class CreateResourceHolderArgs;
class NebulaArena;
class LayoutHolder;
class ResourceHolder;
class ResourceHolderManager;

namespace MR {
    class FormattedName;
}

typedef bool (*MakeArchiveFileNameFuncPtr)(MR::FormattedName&, const char*);
typedef void (*FuncPtrB)(const char*, CreateResourceHolderArgs*);
typedef void (ResourceHolderManager::*FuncPtrC)(const char*, CreateResourceHolderArgs*);

class CreateResourceHolderArgs {
public:
    CreateResourceHolderArgs();

    ResourceHolder* mResourceHolder;
    LayoutHolder* mLayoutHolder;
    NebulaArena* mHeap;
};

class ResourceHolderManagerName2Resource {
public:
    ResourceHolderManagerName2Resource();

    /// @brief Copies the contents from `other` to `this`.
    /// @param other A reference to the `ResourceHolderManagerName2Resource` to copy.
    /// @return A reference to `this`.
    ResourceHolderManagerName2Resource& operator=(const ResourceHolderManagerName2Resource& other);

    ResourceHolder* mResourceHolder;
    LayoutHolder* mLayoutHolder;
    u32 mHash;
    NebulaArena* mHeap;
};

class ResourceHolderManager {
public:
    ResourceHolderManager();

    ResourceHolder* createAndAdd(const char*, NebulaArena*);
    LayoutHolder* createAndAddLayoutHolder(const char*, NebulaArena*);
    LayoutHolder* createAndAddLayoutHolderRawData(const char*);
    LayoutHolder* createAndAddGameLayoutHolder(const char* pArcName);
    void removeIfIsEqualHeap(NebulaArena*);
    static void startCreateResourceHolder(const char*, CreateResourceHolderArgs*);
    static void startCreateLayoutHolder(const char*, CreateResourceHolderArgs*);

private:
    ResourceHolderManagerName2Resource* createAndAddInner(const char*, MakeArchiveFileNameFuncPtr, FuncPtrB);
    void createResourceHolder(const char*, CreateResourceHolderArgs*);
    void createLayoutHolder(const char*, CreateResourceHolderArgs*);
    ResourceHolderManagerName2Resource* add(const char*, const CreateResourceHolderArgs&);
    ResourceHolderManagerName2Resource* find(const char*);

private:
    MR::Vector< MR::FixedArray< ResourceHolderManagerName2Resource, 512 > > mResourceArray;
};
