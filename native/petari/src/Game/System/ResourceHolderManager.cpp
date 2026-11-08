#include "Game/System/ResourceHolderManager.hpp"
#include "Game/System/LayoutHolder.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/Functor.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/SystemUtil.hpp"


ResourceHolderManager::ResourceHolderManager() {
    mResourceArray.clear();
}

ResourceHolder* ResourceHolderManager::createAndAdd(const char* pArcName, NebulaArena*) {
    return createAndAddInner(pArcName, MR::makeObjectArchiveFileName, startCreateResourceHolder)->mResourceHolder;
}

LayoutHolder* ResourceHolderManager::createAndAddLayoutHolder(const char* pParam1, NebulaArena*) {
    return createAndAddInner(pParam1, MR::makeLayoutArchiveFileName, startCreateLayoutHolder)->mLayoutHolder;
}

// The game's own copy of the layout archive pArcName under a mod's, listed under its own name ("<name>|game", which no
// archive has) so the mod's copy and it never stand for each other. Null when no mod's copy is over the game's, or the
// game has none.
LayoutHolder* ResourceHolderManager::createAndAddGameLayoutHolder(const char* pArcName) {
    MR::FormattedName name("%s|game", pArcName);
    ResourceHolderManagerName2Resource* pFound = find(name.c_str());
    if (pFound != nullptr) {
        return pFound->mLayoutHolder;
    }

    MR::FormattedName gamePath;
    if (!MR::makeGameLayoutArchiveFileName(gamePath, pArcName)) {
        return nullptr;
    }
    NebulaArena* pArena = nullptr;
    AssetFolder* const pArchive = MR::getGameArchive(gamePath.c_str(), &pArena);
    // With no mod's copy over the game's, the holder the game has is its own.
    MR::FormattedName heldPath;
    MR::makeLayoutArchiveFileName(heldPath, pArcName);
    if (pArchive == nullptr || pArchive == MR::getArchive(heldPath.c_str())) {
        return nullptr;
    }

    CreateResourceHolderArgs args = CreateResourceHolderArgs();
    args.mHeap = pArena;
    {
        MR::CurrentHeapRestorer heapRestorer(pArena);
        args.mLayoutHolder = new LayoutHolder(*pArchive);
    }
    return add(name.c_str(), args)->mLayoutHolder;
}

LayoutHolder* ResourceHolderManager::createAndAddLayoutHolderRawData(const char* pParam1) {
    FuncPtrC create = &ResourceHolderManager::createLayoutHolder;
    CreateResourceHolderArgs args = CreateResourceHolderArgs();
    (this->*create)(pParam1, &args);

    return add(pParam1, args)->mLayoutHolder;
}

void ResourceHolderManager::removeIfIsEqualHeap(NebulaArena* pHeap) {
    if (pHeap == nullptr) {
        return;
    }

    for (ResourceHolderManagerName2Resource* pIter = mResourceArray.begin(); pIter != mResourceArray.end(); pIter++) {
        if (pIter->mHeap != pHeap) {
            continue;
        }

        if (pIter->mResourceHolder != nullptr) {
            delete pIter->mResourceHolder;
        }

        if (pIter->mLayoutHolder != nullptr) {
            delete pIter->mLayoutHolder;
        }

        pIter->mHeap = nullptr;
        pIter->mLayoutHolder = nullptr;
        pIter->mResourceHolder = nullptr;
    }

    for (ResourceHolderManagerName2Resource* pIter = mResourceArray.begin(); pIter != mResourceArray.end();) {
        if (pIter->mHeap == nullptr) {
            mResourceArray.erase(pIter);
        } else {
            pIter++;
        }
    }
}

void ResourceHolderManager::startCreateResourceHolder(const char* pParam1, CreateResourceHolderArgs* pArgs) {
    SingletonHolder< ResourceHolderManager >::get()->createResourceHolder(pParam1, pArgs);
}

void ResourceHolderManager::startCreateLayoutHolder(const char* pParam1, CreateResourceHolderArgs* pArgs) {
    SingletonHolder< ResourceHolderManager >::get()->createLayoutHolder(pParam1, pArgs);
}

ResourceHolderManagerName2Resource* ResourceHolderManager::createAndAddInner(const char* pArcName,
                                                                             MakeArchiveFileNameFuncPtr pMakeArchiveFileNameFunc, FuncPtrB pParam3) {
    ResourceHolderManagerName2Resource* pName2Resource = find(pArcName);

    if (pName2Resource != nullptr) {
        return pName2Resource;
    }

    MR::FormattedName archivePath;
    (*pMakeArchiveFileNameFunc)(archivePath, pArcName);
    if (MR::getArchive(archivePath.c_str()) == nullptr) {
        NEBULA_PANIC("%s cannot be loaded", archivePath.c_str());
    }

    CreateResourceHolderArgs args = CreateResourceHolderArgs();

    (*pParam3)(archivePath.c_str(), &args);

    return add(archivePath.c_str(), args);
}

// A holder lives in its archive's arena, so it goes with the archive.
void ResourceHolderManager::createResourceHolder(const char* pParam1, CreateResourceHolderArgs* pArgs) {
    AssetFolder* const pArchive = MR::getArchive(pParam1);
    pArgs->mHeap = MR::getArchiveArena(pParam1);
    MR::CurrentHeapRestorer heapRestorer(pArgs->mHeap);

    pArgs->mResourceHolder = new ResourceHolder(*pArchive);
}

void ResourceHolderManager::createLayoutHolder(const char* pParam1, CreateResourceHolderArgs* pArgs) {
    AssetFolder* const pArchive = MR::getArchive(pParam1);
    pArgs->mHeap = MR::getArchiveArena(pParam1);
    MR::CurrentHeapRestorer heapRestorer(pArgs->mHeap);

    pArgs->mLayoutHolder = new LayoutHolder(*pArchive);
}

ResourceHolderManagerName2Resource* ResourceHolderManager::add(const char* pParam1, const CreateResourceHolderArgs& rArgs) {
    ResourceHolderManagerName2Resource name2Resource = ResourceHolderManagerName2Resource();
    name2Resource.mHash = MR::getHashCodeLower(MR::getBasename(pParam1));
    name2Resource.mResourceHolder = rArgs.mResourceHolder;
    name2Resource.mLayoutHolder = rArgs.mLayoutHolder;
    name2Resource.mHeap = rArgs.mHeap;

    mResourceArray.push_back(name2Resource);

    return &mResourceArray[mResourceArray.size() - 1];
}

ResourceHolderManagerName2Resource* ResourceHolderManager::find(const char* pParam1) {
    ResourceHolderManagerName2Resource name2Resource = ResourceHolderManagerName2Resource();
    name2Resource.mHash = MR::getHashCodeLower(pParam1);

    ResourceHolderManagerName2Resource* pIter;
    ResourceHolderManagerName2Resource* pEnd = mResourceArray.end();

    for (pIter = mResourceArray.begin(); pIter != pEnd && pIter->mHash != name2Resource.mHash; pIter++) {
    }

    if (pIter == pEnd) {
        return nullptr;
    }

    return pIter;
}

ResourceHolderManagerName2Resource::ResourceHolderManagerName2Resource() : mResourceHolder(), mLayoutHolder(), mHeap() {
}

ResourceHolderManagerName2Resource& ResourceHolderManagerName2Resource::operator=(const ResourceHolderManagerName2Resource& rOther) {
    mResourceHolder = rOther.mResourceHolder;
    mLayoutHolder = rOther.mLayoutHolder;
    mHash = rOther.mHash;
    mHeap = rOther.mHeap;

    return *this;
}

CreateResourceHolderArgs::CreateResourceHolderArgs() : mResourceHolder(), mLayoutHolder(), mHeap() {
}
