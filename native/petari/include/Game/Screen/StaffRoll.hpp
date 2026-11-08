#pragma once

#include "Game/Screen/LayoutActor.hpp"

namespace Ui {
    class TexMap;
}  // namespace Ui

class InformationMessage;

class StaffRollLine : public LayoutActor {
public:
    /// @brief Creates a new `StaffRollLine`.
    /// @param pName A pointer to the null-terminated name of the object.
    StaffRollLine(const char* pName);

    virtual ~StaffRollLine() {
    }

    virtual void init(const TableIter& rIter);
    virtual void appear();

    void exeScroll();

    void setPositionX(f32 x) {
        mPosition.x = x;
    }

private:
    Vec2f mPosition;
};

class StaffRollPicture : public LayoutActor {
public:
    /// @brief Creates a new `StaffRollPicture`.
    /// @param pName A pointer to the null-terminated name of the object.
    StaffRollPicture(const char* pName);

    virtual ~StaffRollPicture() {
    }

    virtual void init(const TableIter& rIter);

    void initReplaceTexture();
    void startPictureAction(s32, const char*, const char*);
    void disappearAllPane();
    void exeWork();

private:
    Ui::TexMap** mReplaceTexture;
};

class StaffRoll : public LayoutActor {
public:
    /// @brief Creates a new `StaffRoll`.
    /// @param pName A pointer to the null-terminated name of the object.
    StaffRoll(const char* pName);

    virtual ~StaffRoll() {
    }

    virtual void init(const TableIter& rIter);
    virtual void appear();
    virtual void movement();
    virtual void draw() const;
    virtual void calcAnim();
    virtual void kill();

    void startInfo();
    bool isPauseOrEnd() const;
    bool isNextLineEmpty() const;
    void setNrvNextInfo();
    bool tryGenerateLine();
    void makeLine();
    StaffRollLine* getDeadLineLayout() const;
    void exePrepareDemo();
    void exePrepareBgm();
    void exeTitleLogoAppear();
    void exeTitleLogoScroll();
    void exeLineScroll();
    void exeEndLogoScroll();
    void exeAllRights();
    void exePrepareInfo();
    void exeInfoAppear();
    void exeInfoDisappear();
    void exePause();
    void exeRestart();
    void exePrepareSave();
    void exeSave();
    void exeEnd();

private:
    InformationMessage* mInfo;
    StaffRollLine** mLine;
    StaffRollPicture* mPicture;
    const char16_t* mMsg;
    u32 mMsgLineNum;
    u32 mMsgLine;
    Vec2f mTitleLogoPos;
    Vec2f mEndLogoPos;
    s32 mLineStep;
    s32 mBgmFrame;
};

namespace MR {
    StaffRoll* getStaffRoll();
};  // namespace MR
