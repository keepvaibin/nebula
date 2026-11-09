#pragma once

#include "Game/Screen/LayoutActor.hpp"
#include "Game/System/NerveExecutor.hpp"
#include "Game/Util/StringUtil.hpp"

namespace FileSelectInfoSub {
    class SlideState;
    class CharaState;
};  // namespace FileSelectInfoSub

class FileSelectInfo : public LayoutActor {
public:
    /// @brief Creates a new `FileSelectInfo`.
    /// @param nameBufferSize The number of wide characters to allocate memory for in the file name buffer.
    /// @param pName The pointer to the null-terminated name of the object.
    FileSelectInfo(s32 nameBufferSize, const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void control() override;

    void disappear();
    void slide();
    void slideBack();
    void setInfo(u16*, s32, s32, s32, bool, bool, bool, const char16_t*, const char16_t*, s32);
    void change();
    void forceChange();
    void exeAppear();
    void exeDisplay();
    void exeDisappear();
    void reflectInfo();

private:
    s32 mNumber;
    s32 mStarNum;
    s32 mStarPieceNum;
    s32 mNameBufferSize;
    char16_t* mName;
    MR::Text16 mDateMessage;
    MR::Text16 mTimeMessage;
    s32 mMissNum;
    bool mIsSelectedMarioPrev;
    bool mIsSelectedMario;
    bool mIsViewNormalEnding;
    bool mIsViewCompleteEnding;
    FileSelectInfoSub::SlideState* mSlideState;
    FileSelectInfoSub::CharaState* mCharaState;
};

namespace FileSelectInfoSub {
    class SlideState : public NerveExecutor {
    public:
        /// @brief Creates a new `SlideState`.
        /// @param pHost TODO.
        SlideState(FileSelectInfo* pHost);

        void exeNormalPos();
        void exeSliding();
        void exeSlidePos();
        void exeSlidingBack();

    private:
        FileSelectInfo* mHost;
    };

    class CharaState : public NerveExecutor {
    public:
        /// @brief Creates a new `CharaState`.
        /// @param pHost TODO.
        CharaState(FileSelectInfo* pHost);

        void exeMario();
        void exeToLuigi();
        void exeLuigi();
        void exeToMario();

    private:
        FileSelectInfo* mHost;
    };
};  // namespace FileSelectInfoSub
