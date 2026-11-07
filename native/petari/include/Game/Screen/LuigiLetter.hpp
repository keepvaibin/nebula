#pragma once

#include "Game/Screen/LayoutActor.hpp"

namespace nw4r {
    namespace lyt {
        class TexMap;
    };  // namespace lyt
};  // namespace nw4r

class IconAButton;

class LuigiLetter : public LayoutActor {
public:
    /// @brief Creates a new `LuigiLetter`.
    LuigiLetter(bool, const char*);

    virtual void init(const TableIter&);
    virtual void appear();
    virtual void movement();

    void exeAppear();
    void exeWait();
    void exeEnd();

private:
    nw4r::lyt::TexMap* mTexture;
    IconAButton* mAButtonIcon;
    const char* _28;
    bool _2C;
};

namespace MR {
    LuigiLetter* createLuigiLetterForTalk();
    LuigiLetter* createLuigiLetterForMenu();
};  // namespace MR
