#pragma once


namespace nw4r {
    namespace ut {
        class Font;
        class ResFont;
    };  // namespace ut
};  // namespace nw4r

class AssetFolder;

class GameSystemFontHolder {
public:
    /// @brief Creates a new `GameSystemFontHolder`.
    GameSystemFontHolder();

    nw4r::ut::Font* getMessageFont() const;
    void createFontFromEmbeddedData();
    void createFontFromFile();

    AssetFolder* _4;
    nw4r::ut::ResFont* mEmbeddedMessageFont;
    nw4r::ut::ResFont* mMessageFont;
    nw4r::ut::ResFont* mPictureFont;
    nw4r::ut::ResFont* mMenuFont;
    nw4r::ut::ResFont* mNumberFont;
    nw4r::ut::ResFont* mCinemaFont;
};
