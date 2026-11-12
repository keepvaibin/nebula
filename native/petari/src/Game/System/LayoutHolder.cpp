#include "Game/System/LayoutHolder.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/System/AssetFolder.hpp"
#include "Game/System/LumaFont.hpp"
#include <nebula_game_api.h>

#include "Game/Util/StringUtil.hpp"

LayoutHolder::LayoutHolder(AssetFolder& rArchive) : Ui::ResourceAccessor(), mArchive(&rArchive) {
    initializeArc();
}

void* LayoutHolder::GetResource(u32 resourceKind, const char* pName, u32* pResourceInfo) {
    void* pResource = nullptr;

    switch (resourceKind) {
    case 'blyt':
        pResource = mLayoutRes.getRes(pName);
        break;
    case 'anim':
        pResource = mAnimRes.getRes(pName);
        break;
    default:

        if (!MR::isEqualSubString(pName, ".brfnt")) {
            pResource = mResOther.getRes(pName);
        }

        break;
    }

    if (pResourceInfo != nullptr) {
        if (pResource != nullptr) {
            *pResourceInfo = static_cast< u32* >(pResource)[1];
        } else {
            *pResourceInfo = 0;
        }
    }

    return pResource;
}

Ui::Font* LayoutHolder::GetFont(const char* pName) {
    // A mod's own font (Luma/<id>/<path>.brfnt, plan 3.8 layer 2).
    if (Ui::Font* pModFont = luma::font::find(pName)) {
        return pModFont;
    }

    if (!MR::strcasecmp(pName, "MessageFont26.brfnt")) {
        return MR::getFontOnCurrentLanguage();
    }

    if (!MR::strcasecmp(pName, "MenuFont64.brfnt")) {
        return MR::getMenuFont();
    }

    if (!MR::strcasecmp(pName, "NumberFont.brfnt")) {
        return MR::getNumberFont();
    }

    if (!MR::strcasecmp(pName, "PictureFont.brfnt")) {
        return MR::getPictureFont();
    }

    if (MR::strcasecmp(pName, "CinemaFont26.brfnt") == 0) {
        return MR::getCinemaFont();
    }

    return MR::getFontOnCurrentLanguage();
}

bool LayoutHolder::isAnimationHashEqual(u32 hash, u32 fileID) const {
    const ResFileInfo* const pInfo = mAnimRes.getFileInfo(fileID);
    return pInfo != nullptr && pInfo->isEqualHashCode(hash);
}

void LayoutHolder::initializeArc() {
    u32 counts[NEBULA_ASSET_KIND_COUNT] = {};
    count(mArchive->getEntry(), counts);

    u32 other = 0;
    for (u32 kind = 0; kind < NEBULA_ASSET_KIND_COUNT; kind++) {
        if (&tableOf(kind) == &mResOther) {
            other += counts[kind];
        }
    }

    if (counts[NEBULA_ASSET_KIND_LAYOUT] != 0) {
        mLayoutRes.newFileInfoTable(counts[NEBULA_ASSET_KIND_LAYOUT]);
    }
    if (counts[NEBULA_ASSET_KIND_LAYOUT_ANIMATION] != 0) {
        mAnimRes.newFileInfoTable(counts[NEBULA_ASSET_KIND_LAYOUT_ANIMATION]);
    }
    if (other != 0) {
        mResOther.newFileInfoTable(other);
    }

    mount(mArchive->getEntry());
}

ResTable& LayoutHolder::tableOf(u32 kind) {
    switch (kind) {
    case NEBULA_ASSET_KIND_LAYOUT:
        return mLayoutRes;
    case NEBULA_ASSET_KIND_LAYOUT_ANIMATION:
        return mAnimRes;
    default:
        return mResOther;
    }
}

void LayoutHolder::count(s32 folder, u32* pCounts) const {
    AssetFolder::Item item;

    for (u32 i = 0; mArchive->getItem(folder, i, &item); i++) {
        if (item.mIsFolder) {
            count(item.mEntry, pCounts);
        } else {
            pCounts[item.mKind < NEBULA_ASSET_KIND_COUNT ? item.mKind : NEBULA_ASSET_KIND_OTHER]++;
        }
    }
}

void LayoutHolder::mount(s32 folder) {
    AssetFolder::Item item;

    for (u32 i = 0; mArchive->getItem(folder, i, &item); i++) {
        if (item.mIsFolder) {
            mount(item.mEntry);
        } else {
            ResFileInfo* pInfo = tableOf(item.mKind).add(item.mName, item.mData, false);
            pInfo->mData = item.mData;
            pInfo->mSize = item.mSize;
        }
    }
}
