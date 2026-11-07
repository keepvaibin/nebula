#pragma once

#include "Game/Sound/Sound.hpp"

// A sequence to play: its cooked program and the entry the sound starts at.
struct SeqData {
    SeqData() : mFile(nullptr), mEntryOp(0) {
    }

    void set(const SeqFile* pFile, u32 entryOp) {
        mFile = pFile;
        mEntryOp = entryOp;
    }

    const SeqFile* mFile;
    u32 mEntryOp;
};

// Where a manager finds its sounds' sequences. Every sequence is mapped, so the answer is there at once or not at all.
struct SeqSource {
    virtual ~SeqSource() {
    }
    virtual bool getSeqData(SoundID id, SeqData* pOut) = 0;
    // The one program whose entries all its sounds are, if so: its bounds size a player before the sound's entry is known.
    virtual const SeqFile* program() const {
        return nullptr;
    }
};
