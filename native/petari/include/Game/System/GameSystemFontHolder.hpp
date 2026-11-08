#pragma once


namespace Ui {
    class Font;
    class ResFont;
}  // namespace Ui

class AssetFolder;

class GameSystemFontHolder {
public:
    /// @brief Creates a new `GameSystemFontHolder`.
    GameSystemFontHolder();

    Ui::Font* getMessageFont() const;
    void createFontFromEmbeddedData();
    void createFontFromFile();

    AssetFolder* mEmbeddedFontFolder;
    Ui::ResFont* mEmbeddedMessageFont;
    Ui::ResFont* mMessageFont;
    Ui::ResFont* mPictureFont;
    Ui::ResFont* mMenuFont;
    Ui::ResFont* mNumberFont;
    Ui::ResFont* mCinemaFont;
};
