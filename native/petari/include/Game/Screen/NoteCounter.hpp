#pragma once

#include "Game/Screen/LayoutActor.hpp"

class LiveActor;

class NoteCounter : public LayoutActor {
public:
    /// @brief Creates a new `NoteCounter`.
    /// @param pName The pointer to the null-terminated name of the object.
    NoteCounter(const char* pName);

    virtual void init(const TableIter& rIter) override;

    void declareNoteNumMaxAndMelody(LiveActor* pHost, s32 noteNum, s32 melodyNo, f32 railLength);
    void add();
    void exeShow();
    void exeShowWait();
    void exeShowToHide();
    void exeHideToShow();
    void exeHide();
    void tryEndDisp();

    /// @brief The total number of notes in the song.
    s32 mNoteNum;

    /// @brief The cumulative number of collected notes.
    s32 mNoteAddNum;

    /// @brief The total number of missed notes.
    s32 mNoteMissNum;

    /// @brief The numeric identifier for the selected song.
    s32 mMelodyNo;

    /// @brief The length of the path that notes are distributed along.
    f32 mRailLength;

    /// @brief The pointer to the owning actor instance.
    LiveActor* mHost;
};

namespace MR {
    void declareNoteNumMaxAndMelodyToCounter(LiveActor* pHost, s32 noteNum, s32 melodyNo, f32 railLength);
    void addNoteToCounter();
    void failureNoteCollection();
};  // namespace MR
