#include "Game/System/GameSystemFontHolder.hpp"
#include "Game/System/Language.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/AssetFolder.hpp"
#include <cstdio>
#include <Game/Ui/ResFont.hpp>

GameSystemFontHolder::GameSystemFontHolder() : mEmbeddedFontFolder(), mEmbeddedMessageFont(), mMessageFont(), mPictureFont(), mMenuFont(), mNumberFont() {
}

Ui::Font* GameSystemFontHolder::getMessageFont() const {
    if (mEmbeddedMessageFont != nullptr) {
        return mEmbeddedMessageFont;
    }

    return mMessageFont;
}

void GameSystemFontHolder::createFontFromEmbeddedData() {
    AssetFolder* pArchive = MR::getArchive("ErrorMessageArchive");
    NebulaArena* pArena = MR::getArchiveArena("ErrorMessageArchive");

    const char* embeddedFontPath = "/LayoutData/EmbeddedFont";

    mEmbeddedFontFolder = pArchive->getSubFolder(embeddedFontPath, pArena);
    mEmbeddedMessageFont = new (pArena, 4) Ui::ResFont();
    mEmbeddedMessageFont->SetResource(mEmbeddedFontFolder->getResource("MessageFont26.brfnt"));
}

namespace {
    inline Ui::ResFont* createFont(AssetFolder* pArchive, const char* pName) {
        Ui::ResFont* pFont = new Ui::ResFont();
        pFont->SetResource(pArchive->getResource(pName));
        return pFont;
    }
};  // namespace

void GameSystemFontHolder::createFontFromFile() {
    AssetFolder* pArchive = MR::getArchive("/LayoutData/Font");

    mMessageFont = createFont(pArchive, "/MessageFont26.brfnt");

    mPictureFont = createFont(pArchive, "/PictureFont.brfnt");

    mMenuFont = createFont(pArchive, "/MenuFont64.brfnt");

    mNumberFont = createFont(pArchive, "/NumberFont.brfnt");

    mCinemaFont = createFont(pArchive, "/CinemaFont26.brfnt");

    mMessageFont->SetAlternateChar('?');
    mPictureFont->SetAlternateChar('?');
    mMenuFont->SetAlternateChar('?');
    mNumberFont->SetAlternateChar('?');
    mCinemaFont->SetAlternateChar('?');

    if (mEmbeddedMessageFont != nullptr) {
        mEmbeddedMessageFont->RemoveResource();

        delete mEmbeddedMessageFont;
        delete mEmbeddedFontFolder;

        mEmbeddedMessageFont = nullptr;
    }
}
