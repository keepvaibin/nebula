#include "Game/System/ResourceHolderManager.hpp"
#include "Game/System/LayoutHolder.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/SystemUtil.hpp"

ResourceHolderManager::ResourceHolderManager() {
    mResourceArray.clear();
}

ResourceHolder* ResourceHolderManager::createAndAdd(const char* pArcName, NebulaArena*) {
    return createAndAddInner(pArcName, MR::makeObjectArchiveFileName, &ResourceHolderManager::createResourceHolder)->mResourceHolder;
}

LayoutHolder* ResourceHolderManager::createAndAddLayoutHolder(const char* pArcName, NebulaArena*) {
    return createAndAddInner(pArcName, MR::makeLayoutArchiveFileName, &ResourceHolderManager::createLayoutHolder)->mLayoutHolder;
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
    args.mArena = pArena;
    {
        NebulaArena::CurrentScope scope(pArena);
        args.mLayoutHolder = new LayoutHolder(*pArchive);
    }
    return add(name.c_str(), args)->mLayoutHolder;
}

LayoutHolder* ResourceHolderManager::createAndAddLayoutHolderRawData(const char* pArcPath) {
    CreateResourceHolderArgs args = CreateResourceHolderArgs();
    createLayoutHolder(pArcPath, &args);

    return add(pArcPath, args)->mLayoutHolder;
}

void ResourceHolderManager::removeArenaHolders(NebulaArena* pArena) {
    if (pArena == nullptr) {
        return;
    }

    for (ResourceHolderManagerName2Resource* pIter = mResourceArray.begin(); pIter != mResourceArray.end(); pIter++) {
        if (pIter->mArena != pArena) {
            continue;
        }

        if (pIter->mResourceHolder != nullptr) {
            delete pIter->mResourceHolder;
        }

        if (pIter->mLayoutHolder != nullptr) {
            delete pIter->mLayoutHolder;
        }

        pIter->mArena = nullptr;
        pIter->mLayoutHolder = nullptr;
        pIter->mResourceHolder = nullptr;
    }

    for (ResourceHolderManagerName2Resource* pIter = mResourceArray.begin(); pIter != mResourceArray.end();) {
        if (pIter->mArena == nullptr) {
            mResourceArray.erase(pIter);
        } else {
            pIter++;
        }
    }
}

ResourceHolderManagerName2Resource* ResourceHolderManager::createAndAddInner(const char* pArcName, MakeArchiveFileNameFuncPtr pMakeArchiveFileNameFunc,
                                                                             CreateHolderFunc create) {
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

    (this->*create)(archivePath.c_str(), &args);

    return add(archivePath.c_str(), args);
}

// A holder lives in its archive's arena, so it goes with the archive.
void ResourceHolderManager::createResourceHolder(const char* pArcPath, CreateResourceHolderArgs* pArgs) {
    AssetFolder* const pArchive = MR::getArchive(pArcPath);
    pArgs->mArena = MR::getArchiveArena(pArcPath);
    NebulaArena::CurrentScope scope(pArgs->mArena);

    pArgs->mResourceHolder = new ResourceHolder(*pArchive);
}

void ResourceHolderManager::createLayoutHolder(const char* pArcPath, CreateResourceHolderArgs* pArgs) {
    AssetFolder* const pArchive = MR::getArchive(pArcPath);
    pArgs->mArena = MR::getArchiveArena(pArcPath);
    NebulaArena::CurrentScope scope(pArgs->mArena);

    pArgs->mLayoutHolder = new LayoutHolder(*pArchive);
}

// Listed under the hash of the path's basename, case-free.
ResourceHolderManagerName2Resource* ResourceHolderManager::add(const char* pPath, const CreateResourceHolderArgs& rArgs) {
    ResourceHolderManagerName2Resource name2Resource = ResourceHolderManagerName2Resource();
    name2Resource.mHash = MR::getHashCodeLower(MR::getBasename(pPath));
    name2Resource.mResourceHolder = rArgs.mResourceHolder;
    name2Resource.mLayoutHolder = rArgs.mLayoutHolder;
    name2Resource.mArena = rArgs.mArena;

    mResourceArray.push_back(name2Resource);

    return &mResourceArray[mResourceArray.size() - 1];
}

ResourceHolderManagerName2Resource* ResourceHolderManager::find(const char* pName) {
    const u32 hash = MR::getHashCodeLower(pName);

    for (ResourceHolderManagerName2Resource* pIter = mResourceArray.begin(); pIter != mResourceArray.end(); pIter++) {
        if (pIter->mHash == hash) {
            return pIter;
        }
    }

    return nullptr;
}

ResourceHolderManagerName2Resource::ResourceHolderManagerName2Resource() : mResourceHolder(), mLayoutHolder(), mArena() {
}

ResourceHolderManagerName2Resource& ResourceHolderManagerName2Resource::operator=(const ResourceHolderManagerName2Resource& rOther) {
    mResourceHolder = rOther.mResourceHolder;
    mLayoutHolder = rOther.mLayoutHolder;
    mHash = rOther.mHash;
    mArena = rOther.mArena;

    return *this;
}

CreateResourceHolderArgs::CreateResourceHolderArgs() : mResourceHolder(), mLayoutHolder(), mArena() {
}
