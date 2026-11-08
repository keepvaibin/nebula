#include "Game/Sound/ChordInfo.hpp"
#include <cstring>

bool ChordTable::setResource(const void* pFile) {
    // The cooked table: a word (0), "CITS", the size, the chord and scale counts (16-bit), then their offset tables.
    const u8* file = static_cast< const u8* >(pFile);
    if (file == nullptr || file[4] != 'C' || file[5] != 'I' || file[6] != 'T' || file[7] != 'S') {
        return false;
    }
    const u8* base = file + 12;
    u16 chords;
    u16 scales;
    std::memcpy(&chords, base + 0, sizeof(chords));
    std::memcpy(&scales, base + 2, sizeof(scales));
    mBase = file;
    mChordCount = chords;
    mScaleCount = scales;
    mChordOffsets = reinterpret_cast< const u32* >(base + 4);
    mScaleOffsets = mChordOffsets + mChordCount;
    return true;
}

bool ChordInfo::load(const void* pFile, bool keepCurrent) {
    initParams();
    mTable.mLoaded = mTable.setResource(pFile);
    if (!mTable.mLoaded) {
        return false;
    }
    if (keepCurrent) {
        setCurChord(mCurChordIndex);
        setCurScale(mCurScaleIndex);
    } else {
        setCurChord(0);
        setCurScale(0);
    }
    return true;
}

void ChordInfo::invalidate() {
    initParams();
}

bool ChordInfo::setCurChord(s32 index) {
    if (!mTable.mLoaded) {
        return false;
    }
    if (index >= mTable.mChordCount) {
        return false;
    }
    mCurChord = mTable.getChord(index);
    expandChordNote();
    mCurChordIndex = index;
    return true;
}

bool ChordInfo::setCurScale(s32 index) {
    if (!mTable.mLoaded) {
        return false;
    }
    if (index >= mTable.mScaleCount) {
        return false;
    }
    mCurScale = mTable.getScale(index);
    mCurScaleUp = mTable.mBase + mCurScale->mUpOffset;
    mCurScaleDown = mTable.mBase + mCurScale->mDownOffset;
    mCurScaleIndex = index;
    return true;
}

u8 ChordInfo::getBassNote() const {
    if (mCurChord->mBass == NULL_NOTE) {
        return mCurChord->mChordTone[0];
    }

    return mCurChord->mBass;
}

// The first of the chord's four tones (isChordTone) or its note list (an octave up when below the root) loStep..hiStep
// semitones over the root, as a pitch class; NULL_NOTE for none or no chord. isSkipThird passes over a note equal to the
// third.
u8 ChordInfo::findInterval(bool isChordTone, u8 loStep, u8 hiStep, bool isSkipThird) {
    if (!isAvailable()) {
        return NULL_NOTE;
    }

    u8 root = getRoot();
    u8 lo = root + loStep;
    u8 hi = root + hiStep;
    const u8* pNotes = isChordTone ? mCurChord->mChordTone : mChordNoteList;
    s32 noteNum = isChordTone ? 4 : NUM_CHORD_NOTES;

    for (s32 i = 0; i < noteNum; i++) {
        u8 note = pNotes[i];

        if (note == NULL_NOTE) {
            continue;
        }

        if (note < root) {
            note += OCTAVE;
        }

        if (note >= lo && note <= hi && !(isSkipThird && note == getThird())) {
            return note % OCTAVE;
        }
    }

    return NULL_NOTE;
}

u8 ChordInfo::getThird() {
    return findInterval(true, 3, 5, false);
}

u8 ChordInfo::getFifth() {
    return findInterval(true, 6, 8, false);
}

u8 ChordInfo::getSeventh() {
    return findInterval(true, 9, 11, false);
}

u8 ChordInfo::getSixth() {
    return findInterval(false, 8, 9, false);
}

u8 ChordInfo::getNinth() {
    return findInterval(false, 1, 3, true);
}

u8 ChordInfo::getEleventh() {
    return findInterval(false, 5, 6, false);
}

bool ChordInfo::isOnChord(u8 note, bool includeAdd, bool includeBass) {
    s32 pitchClass = note % OCTAVE;

    // Check chord tones
    for (s32 i = 0; i < 4; i++) {
        if (pitchClass == getChordTone(i)) {
            return true;
        }
    }

    // Check add notes
    if (includeAdd) {
        for (s32 i = 0; i < 3; i++) {
            if (pitchClass == getAddNote(i)) {
                return true;
            }
        }
    }

    // Check bass
    if (includeBass) {
        if (pitchClass == getBassNote()) {
            return true;
        }
    }

    return false;
}

u8 ChordInfo::getNearestChordNoteDir(u8 note, bool up) {
    if (isOnChord(note, true, true)) {
        return note;
    }

    s32 octave = note / OCTAVE;
    s32 pitchClass = note % OCTAVE;
    s32 minDist = 0x7FFF;
    s32 nearest = -1;

    if (up) {
        for (s32 i = 0; i < NUM_CHORD_NOTES; i++) {
            s32 n = mChordNoteList[i];

            if (n == NULL_NOTE) {
                continue;
            }

            if (n < pitchClass) {
                n += OCTAVE;
            }

            s32 dist = n - pitchClass;

            if (dist < minDist) {
                minDist = dist;
                nearest = n;
            }
        }
    } else {
        for (s32 i = 0; i < NUM_CHORD_NOTES; i++) {
            s32 note = mChordNoteList[i];

            if (note == NULL_NOTE) {
                continue;
            }

            s32 orig = note;

            if (note > pitchClass) {
                note -= OCTAVE;
            }

            s32 dist = pitchClass - note;

            if (dist < minDist) {
                minDist = dist;
                nearest = orig;
            }
        }
    }

    if (nearest < 0) {
        nearest = pitchClass;
    }

    nearest += octave * OCTAVE;
    return nearest;
}

u8 ChordInfo::getNearestChordNote(u8 note) {
    if (isOnChord(note, true, true)) {
        return note;
    }

    u8 upNote = getNearestChordNoteDir(note, true);
    u8 downNote = getNearestChordNoteDir(note, false);

    if (upNote < downNote) {
        return upNote;
    }

    return downNote;
}

u8 ChordInfo::getUpperNoteOnChord(u8 note, s32 steps) {
    s32 n = note;

    if (!isOnChord(n, true, true)) {
        n = getNearestChordNoteDir(note, true);
        steps--;
    }

    if (steps <= 0) {
        return n;
    }

    s32 pitchClass = n % OCTAVE;
    s32 octave = n / OCTAVE;

    s32 idx = getChordNoteIndex(pitchClass);
    s32 wrapCount;
    idx = addIndexInChordNoteList(idx, steps, wrapCount);

    wrapCount += octave;

    return mChordNoteList[idx] + wrapCount * OCTAVE;
}

u8 ChordInfo::getLowerNoteOnChord(u8 note, s32 steps) {
    s32 n = note;

    if (!isOnChord(n, true, true)) {
        n = getNearestChordNoteDir(note, false);
        steps--;
    }

    if (steps <= 0) {
        return n;
    }

    s32 pitchClass = n % OCTAVE;
    s32 octave = n / OCTAVE;

    s32 idx = getChordNoteIndex(pitchClass);
    s32 wrapCount;
    idx = subIndexInChordNoteList(idx, steps, wrapCount);

    wrapCount += octave;

    if (wrapCount < 0) {
        wrapCount = 0;
    }

    return mChordNoteList[idx] + wrapCount * OCTAVE;
}

s32 ChordInfo::getOnScaleType(u8 note) {
    bool onUp = false;
    bool onDown = false;
    s32 pitchClass = note % OCTAVE;

    for (s32 i = 0; i < OCTAVE; i++) {
        if (pitchClass == getScaleNoteUp(i)) {
            onUp = true;
            break;
        }
    }

    for (s32 i = 0; i < OCTAVE; i++) {
        if (pitchClass == getScaleNoteDown(i)) {
            onDown = true;
            break;
        }
    }

    if (onUp) {
        return onDown ? ScaleType_UpDown : ScaleType_Up;
    } else {
        return onDown ? ScaleType_Down : ScaleType_None;
    }
}

u8 ChordInfo::getNearestScaleNoteDir(u8 note, bool up) {
    if (up) {
        if (getOnScaleType(note) == ScaleType_Up) {
            return note;
        }
    } else {
        if (getOnScaleType(note) == ScaleType_Down) {
            return note;
        }
    }

    s32 octave = note / OCTAVE;
    s32 pitchClass = note % OCTAVE;
    s32 minDist = 0x7FFF;
    s32 nearest = -1;

    if (up) {
        for (s32 i = 0; i < OCTAVE; i++) {
            s32 n = getScaleNoteUp(i);

            if (n == NULL_NOTE) {
                continue;
            }

            if (n < pitchClass) {
                n += OCTAVE;
            }

            s32 dist = n - pitchClass;

            if (dist < minDist) {
                minDist = dist;
                nearest = n;
            }
        }
    } else {
        for (s32 i = 0; i < OCTAVE; i++) {
            s32 n = getScaleNoteDown(i);

            if (n == NULL_NOTE) {
                continue;
            }

            s32 orig = n;

            if (n > pitchClass) {
                n -= OCTAVE;
            }

            s32 dist = pitchClass - n;

            if (dist < minDist) {
                minDist = dist;
                nearest = orig;
            }
        }
    }

    if (nearest < 0) {
        nearest = pitchClass;
    }

    nearest += octave * OCTAVE;
    return nearest;
}

u8 ChordInfo::getNearestScaleNote(u8 note) {
    if (getOnScaleType(note) == ScaleType_UpDown) {
        return note;
    }

    u8 upNote = getNearestScaleNoteDir(note, true);
    u8 downNote = getNearestScaleNoteDir(note, false);

    if (upNote < downNote) {
        return upNote;
    }

    return downNote;
}

u8 ChordInfo::getUpperNoteOnScale(u8 note, s32 steps) {
    s32 n = note;

    if ((getOnScaleType(n) & 1) == 0) {  // not up
        n = getNearestScaleNoteDir(note, true);
        steps--;
    }

    if (steps <= 0) {
        return n;
    }

    s32 pitchClass = n % OCTAVE;
    s32 octave = n / OCTAVE;

    s32 idx = getScaleNoteIndex(pitchClass, true);
    s32 wrapCount;
    idx = addIndexInScaleNoteList(idx, steps, wrapCount);

    wrapCount += octave;

    return getScaleNoteUp(idx) + wrapCount * OCTAVE;
}

u8 ChordInfo::getLowerNoteOnScale(u8 note, s32 steps) {
    s32 n = note;

    if ((getOnScaleType(n) & 2) == 0) {  // not down
        n = getNearestScaleNoteDir(note, false);
        steps--;
    }

    if (steps <= 0) {
        return n;
    }

    s32 pitchClass = n % OCTAVE;
    s32 octave = n / OCTAVE;

    s32 idx = getScaleNoteIndex(pitchClass, false);
    s32 wrapCount;
    idx = subIndexInScaleNoteList(idx, steps, wrapCount);

    wrapCount += octave;

    if (wrapCount < 0) {
        wrapCount = 0;
    }

    return getScaleNoteDown(idx) + wrapCount * OCTAVE;
}

void ChordInfo::expandChordNote() {
    // Reset note list
    for (s32 i = 0; i < NUM_CHORD_NOTES; i++) {
        mChordNoteList[i] = NULL_NOTE;
    }

    // Copy chord tones
    s32 idx = 0;
    for (s32 i = 0; i < 4; i++) {
        mChordNoteList[idx++] = getChordTone(i);
    }

    // Copy add notes
    for (s32 i = 0; i < 3; i++) {
        mChordNoteList[idx++] = getAddNote(i);
    }

    // Add bass note
    mChordNoteList[idx] = getBassNote();

    // Bubble sort ascending
    s32 lastSwap = NUM_CHORD_NOTES - 1;
    while (lastSwap >= 1) {
        s32 newLastSwap = -1;

        for (s32 i = 1; i <= lastSwap; i++) {
            if (mChordNoteList[i] < mChordNoteList[i - 1]) {
                u8 tmp = mChordNoteList[i];
                mChordNoteList[i] = mChordNoteList[i - 1];
                mChordNoteList[i - 1] = tmp;
                newLastSwap = i - 1;
            }
        }

        lastSwap = newLastSwap;
    }

    // Remove duplicates
    for (s32 i = 0; i < NUM_CHORD_NOTES - 1; i++) {
        if (mChordNoteList[i] == mChordNoteList[i + 1]) {
            for (s32 j = i + 1; j < NUM_CHORD_NOTES - 1; j++) {
                mChordNoteList[j] = mChordNoteList[j + 1];
            }
            mChordNoteList[NUM_CHORD_NOTES - 1] = NULL_NOTE;
        }
    }
}

s32 ChordInfo::getChordNoteIndex(u8 pitchClass) {
    for (s32 i = 0; i < NUM_CHORD_NOTES; i++) {
        if (mChordNoteList[i] == pitchClass) {
            return i;
        }
    }

    return -1;
}

s32 ChordInfo::addIndexInChordNoteList(s32 idx, s32 count, s32& wrapCount) {
    wrapCount = 0;

    for (s32 c = 0; c < count; c++) {
        idx++;

        if (idx >= NUM_CHORD_NOTES) {
            wrapCount++;
            idx = 0;
        }

        s32 skip = 0;

        while (mChordNoteList[idx] == NULL_NOTE) {
            skip++;
            idx++;

            if (skip >= NUM_CHORD_NOTES) {
                return -1;
            }

            if (idx >= NUM_CHORD_NOTES) {
                wrapCount++;
                idx = 0;
            }
        }
    }

    return idx;
}

s32 ChordInfo::subIndexInChordNoteList(s32 idx, s32 count, s32& wrapCount) {
    wrapCount = 0;

    for (s32 c = 0; c < count; c++) {
        idx--;

        if (idx < 0) {
            wrapCount--;
            idx = NUM_CHORD_NOTES - 1;
        }

        s32 skip = 0;

        while (mChordNoteList[idx] == NULL_NOTE) {
            skip++;
            idx--;

            if (skip >= NUM_CHORD_NOTES) {
                return -1;
            }

            if (idx < 0) {
                wrapCount--;
                idx = NUM_CHORD_NOTES - 1;
            }
        }
    }

    return idx;
}

s32 ChordInfo::getScaleNoteIndex(u8 pClass, bool up) {
    s32 pitchClass = pClass;
    if (up) {
        for (s32 i = 0; i < OCTAVE; i++) {
            if (pitchClass == getScaleNoteUp(i)) {
                return i;
            }
        }
    } else {
        for (s32 i = 0; i < OCTAVE; i++) {
            if (pitchClass == getScaleNoteDown(i)) {
                return i;
            }
        }
    }

    return -1;
}

s32 ChordInfo::addIndexInScaleNoteList(s32 idx, s32 count, s32& wrapCount) {
    s32 noteIndex = idx;
    wrapCount = 0;

    for (s32 c = 0; c < count; c++) {
        noteIndex++;

        if (noteIndex >= OCTAVE) {
            wrapCount++;
            noteIndex = 0;
        }

        s32 skip = 0;

        while (getScaleNoteUp(noteIndex) == NULL_NOTE) {
            skip++;
            noteIndex++;

            if (skip >= OCTAVE) {
                return -1;
            }

            if (noteIndex >= OCTAVE) {
                wrapCount++;
                noteIndex = 0;
            }
        }
    }

    return noteIndex;
}

s32 ChordInfo::subIndexInScaleNoteList(s32 idx, s32 count, s32& wrapCount) {
    s32 noteIndex = idx;
    wrapCount = 0;

    for (s32 c = 0; c < count; c++) {
        noteIndex--;

        if (noteIndex < 0) {
            wrapCount--;
            noteIndex = OCTAVE - 1;
        }

        s32 skip = 0;

        while (getScaleNoteDown(noteIndex) == NULL_NOTE) {
            skip++;
            noteIndex--;

            if (skip >= OCTAVE) {
                return -1;
            }

            if (noteIndex < 0) {
                wrapCount--;
                noteIndex = OCTAVE - 1;
            }
        }
    }

    return noteIndex;
}

void ChordInfo::initParams() {
    mCurChord = nullptr;
    mCurScale = nullptr;
    mCurScaleUp = nullptr;
    mCurScaleDown = nullptr;
    for (s32 i = 0; i < NUM_CHORD_NOTES; i++) {
        mChordNoteList[i] = NULL_NOTE;
    }
}
