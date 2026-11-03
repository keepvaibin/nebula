#pragma once

#include "Game/Screen/LayoutActor.hpp"

namespace nw4r {
    namespace lyt {
        class TexMap;
    };  // namespace lyt
};  // namespace nw4r

class ButtonPaneController;
class IconAButton;
class NameObjArchiveListCollector;
class PictureBookCloseButton;

class PictureBookLayout : public LayoutActor {
public:
    PictureBookLayout(s32 chapterMin, s32 chapterMax, bool isRosettaReading);

    virtual void init(const JMapInfoIter& rIter);
    virtual void appear();
    virtual void kill();
    virtual void control();

    static void makeArchiveList(NameObjArchiveListCollector* pCollector, s32 chapterMin, s32 chapterMax, bool isRosettaReading);
    static s32 getChapterMax();

    void initTexture();
    void initContentsButton();
    bool updateText();
    void updateTexture();
    bool textNext();
    bool pageNext();
    bool chapterNext();
    void updateTexMapChapterBase();
    bool isReadedCurrentText() const;
    s32 getReadSpeed() const;
    bool isBookEndCurrentText() const;
    void setTextAlpha(f32 alpha);
    s32 getChapterNumberMax() const;
    bool isValidCloseButton() const;
    bool isSelectedCloseButton() const;
    s32 getCurrentMaxTextIndex() const;
    void exeOpen();
    void exeContentsSelect();
    void exeContentsFadeOut();
    void exeFadeIn();
    void exeWaitNoText();
    void exeFadeInText();
    void exeWaitWithText();
    void exeFadeOutText();
    void exePageNext();
    void exeFadeOut();
    void exeClose();
    void hideContents();
    f32 getFadeInAlphaTextBG(f32 alpha) const;
    f32 getFadeOutAlphaTextBG(f32 alpha) const;

    // inline bool isAlreadyReadPage() const;
    // inline bool isNotReadPage() const;

private:
    s32 mChapterMin;
    s32 mChapterMax;
    s32 mChapterRosettaMax;
    s32 mChapterNo;
    s32 mPageNo;
    s32 mTextIndex;
    s32 mNotReadedChapterNo;
    s32 mNotReadedPageNo;
    s32 mNotReadedTextIndex;
    nw4r::lyt::TexMap** _44;
    nw4r::lyt::TexMap** _48;
    nw4r::lyt::TexMap* mTitleTexMap;
    nw4r::lyt::TexMap* mCoverFrontTexMap;
    nw4r::lyt::TexMap* mCoverBackTexMap;
    s32 mNextItemDir;
    bool mIsNextItemFast;
    IconAButton* mIconAButton;
    ButtonPaneController** mContentsButtonPaneController;
    PictureBookCloseButton* mCloseButton;
};
