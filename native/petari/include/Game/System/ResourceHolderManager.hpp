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

class CreateResourceHolderArgs {
public:
    CreateResourceHolderArgs();

    ResourceHolder* mResourceHolder;
    LayoutHolder* mLayoutHolder;
    NebulaArena* mArena;
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
    NebulaArena* mArena;
};

class ResourceHolderManager {
public:
    ResourceHolderManager();

    ResourceHolder* createAndAdd(const char* pArcName, NebulaArena*);
    LayoutHolder* createAndAddLayoutHolder(const char* pArcName, NebulaArena*);
    LayoutHolder* createAndAddLayoutHolderRawData(const char* pArcPath);
    LayoutHolder* createAndAddGameLayoutHolder(const char* pArcName);
    // Drops the holders made in pArena (it is going).
    void removeArenaHolders(NebulaArena* pArena);

private:
    typedef void (ResourceHolderManager::*CreateHolderFunc)(const char*, CreateResourceHolderArgs*);

    ResourceHolderManagerName2Resource* createAndAddInner(const char* pArcName, MakeArchiveFileNameFuncPtr, CreateHolderFunc create);
    void createResourceHolder(const char* pArcPath, CreateResourceHolderArgs*);
    void createLayoutHolder(const char* pArcPath, CreateResourceHolderArgs*);
    ResourceHolderManagerName2Resource* add(const char* pPath, const CreateResourceHolderArgs&);
    ResourceHolderManagerName2Resource* find(const char* pName);

private:
    MR::InlineVector< ResourceHolderManagerName2Resource, 512 > mResourceArray;
};
