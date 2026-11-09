#pragma once

#include "Game/Map/FileSelectIconID.hpp"
#include "Game/Screen/LayoutActor.hpp"

namespace Ui {
    class TexMap;
}  // namespace Ui

namespace MR {
    class BitArray;
};  // namespace MR

namespace MiiSelectSub {
    class Page;
};  // namespace MiiSelectSub

class ButtonPaneController;
class MiiSelectIcon;

class MiiSelect : public LayoutActor {
    friend class MiiSelectSub::Page;

public:
    /// @brief Creates a new `MiiSelect`.
    /// @param pName A pointer to the null-terminated name of the object.
    MiiSelect(const char* pName);

    /// @brief Intializes the `MiiSelect` while being placed into a scene.
    /// @param[in] rIter The reference to an iterator over a `Table`.
    virtual void init(const TableIter& rIter) override;

    virtual void calcAnim() override;
    virtual void appear() override;
    virtual void control() override;

    void disappear();
    bool isAppearing() const;
    bool isSelected();
    bool isDummySelected();
    void getSelectedID(FileSelectIconID*);
    Ui::TexMap* getSelectedMiiTexMap();
    void admitIcon();
    void prohibitIcon(const FileSelectIconID&);
    void invalidateSpecialMii(FileSelectIconID::EFellowID);
    void validateAllSpecialMii();
    void exeAppear();
    void exeWait();
    void exeScrollRight();
    void exeScrollLeft();
    void exeSelected();
    void exeDisappear();
    void exeDummySelected();
    void createButtons();
    void callbackLeft();
    void callbackRight();
    void appearButtons();
    void disappearButtons();
    void updateButtons();
    void collectValidMiiIndex();
    void createPage();
    void flipPage();
    void setCurrentPageGroupA();
    void setCurrentPageGroupB();
    void setCurrentPageNum();
    void validateAllIcon();
    void refresh();
    void getIconID(FileSelectIconID*, s32) const;
    void onSelect(s32, Ui::TexMap*);
    void onSelectDummy();
    s32 getIconNum();

    bool isExistMiiIcon() const {
        return mMiiNum != 0 || mFavoriteMiiNum != 0;
    }

    FileSelectIconID makeIconID(u32 id) const {
        FileSelectIconID iconID;
        getIconID(&iconID, id);
        return iconID;
    }

private:
    ButtonPaneController* mArrowButtons[2];
    MR::BitArray* mFellowEnabled;
    u32 mFellowIconNum;
    // The library's Miis the picker shows: its favourites first, then the others.
    u32 mFavoriteMiiNum;
    u32 mMiiNum;
    u32* mMiiIndices;
    u32 mMiiIndexRoom;
    s32 mPageBaseIndex;
    s32 mSelectedIndex;
    Ui::TexMap* mSelectedTexMap;
    MiiSelectSub::Page* mCurrentPage;
    MiiSelectSub::Page* mOtherPage;
    u8 mIsIconProhibited;
    FileSelectIconID* mProhibitedIconID;
};

namespace MiiSelectSub {
    class Page {
    public:
        /// @brief Creates a new `Page`.
        /// @param pHost A pointer to the owning actor instance.
        Page(MiiSelect* pHost);

        void refresh(s32);
        void movement();
        void calcAnim();
        void invalidateAllIcon();
        void validateAllIcon();
        void prohibitIcon(const FileSelectIconID&);

        MiiSelectIcon* mIconArray[8];
        bool mIsGroupA;
        MiiSelect* mHost;
        s32 mBaseIndex;
    };
};  // namespace MiiSelectSub
