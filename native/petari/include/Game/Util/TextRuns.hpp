#pragma once

#include "Game/System/MessageHolder.hpp"

// A text as runs (NMSG version 2, MessageHolder.hpp): its runs, and the arrays of code points, tag records and parameter
// units their indices name (a message file's: MessageRunFile::getText). The text writer prints and measures it with its
// *Runs functions (TextWriterBase.hpp); a tag processor meets each tag as its typed record.
struct TextRuns {
    const MessageRun* mRuns;
    u32 mRunNum;
    const char32_t* mCodes;
    const MessageTagRecord* mTags;
    const u16* mParams;

    // A text run's code points (rRun.mCount of them).
    const char32_t* getText(const MessageRun& rRun) const {
        return mCodes + rRun.mFirst;
    }
    const MessageTagRecord& getTag(const MessageRun& rRun) const {
        return mTags[rRun.mFirst];
    }
    const u16* getParams(const MessageTagRecord& rTag) const {
        return mParams + rTag.mParam;
    }
    // The tag's string: rTag.mStringNum code points and a 0.
    const char32_t* getString(const MessageTagRecord& rTag) const {
        return mCodes + rTag.mString;
    }
};

// A place in a text in runs: before the run mRun's item (a tag, or a text run's code point mCode; of a code point past
// U+FFFF, its second UTF-16 unit when mLow). A place has one form (TextRunReader::normal): a text run's end is the next
// run's start, so two places are equal when they are the same place.
struct TextRunPos {
    u32 mRun;
    u32 mCode;
    u32 mLow;
};

inline bool operator==(const TextRunPos& rA, const TextRunPos& rB) {
    return rA.mRun == rB.mRun && rA.mCode == rB.mCode && rA.mLow == rB.mLow;
}

inline bool operator!=(const TextRunPos& rA, const TextRunPos& rB) {
    return !(rA == rB);
}

inline bool operator<(const TextRunPos& rA, const TextRunPos& rB) {
    if (rA.mRun != rB.mRun) {
        return rA.mRun < rB.mRun;
    }
    return rA.mCode != rB.mCode ? rA.mCode < rB.mCode : rA.mLow < rB.mLow;
}

// Reads a text in runs item by item up to an end, as CharStrmReader reads the text of version 1: a character as the
// UTF-16 unit that text holds (a code point past U+FFFF as its two surrogates, as version 1 has them, until the font
// takes code points, G7.4), a tag as its record.
class TextRunReader {
public:
    enum Item {
        Item_End,
        Item_Char,
        Item_Tag,
    };

    TextRunReader(const TextRuns& rText, const TextRunPos& rEnd) : mText(&rText), mPos(begin()), mEnd(normal(rText, rEnd)) {
        mPos = normal(rText, mPos);
    }

    void Set(const TextRunPos& rPos) {
        mPos = normal(*mText, rPos);
    }
    const TextRunPos& GetPos() const {
        return mPos;
    }
    const TextRunPos& GetEnd() const {
        return mEnd;
    }
    const TextRuns& GetText() const {
        return *mText;
    }

    // The next item and the place after it: a character (*pCode), a tag (*ppTag), or the end.
    Item Next(u16* pCode, const MessageTagRecord** ppTag) {
        if (!(mPos < mEnd)) {
            return Item_End;
        }
        const MessageRun& rRun = mText->mRuns[mPos.mRun];
        if (rRun.mKind != MessageRunKind_Text) {
            *ppTag = &mText->getTag(rRun);
            mPos = normal(*mText, TextRunPos{mPos.mRun + 1, 0, 0});
            return Item_Tag;
        }
        const char32_t code = mText->getText(rRun)[mPos.mCode];
        if (code > 0xFFFF) {
            const u32 bits = static_cast< u32 >(code) - 0x10000;
            if (mPos.mLow == 0) {
                *pCode = static_cast< u16 >(0xD800 | ((bits >> 10) & 0x3FF));
                mPos.mLow = 1;
                return Item_Char;
            }
            *pCode = static_cast< u16 >(0xDC00 | (bits & 0x3FF));
        } else {
            *pCode = static_cast< u16 >(code);
        }
        mPos = normal(*mText, TextRunPos{mPos.mRun, mPos.mCode + 1, 0});
        return Item_Char;
    }

    static TextRunPos begin() {
        return TextRunPos{0, 0, 0};
    }
    static TextRunPos end(const TextRuns& rText) {
        return TextRunPos{rText.mRunNum, 0, 0};
    }

    // The place in its one form: past a text run's last code point (or in an empty text run) is the next run's start; a
    // tag's place has no code; past the last run is the end.
    static TextRunPos normal(const TextRuns& rText, TextRunPos pos) {
        while (pos.mRun < rText.mRunNum) {
            const MessageRun& rRun = rText.mRuns[pos.mRun];
            if (rRun.mKind == MessageRunKind_Text) {
                if (pos.mCode < rRun.mCount) {
                    if (pos.mLow != 0 && rText.getText(rRun)[pos.mCode] <= 0xFFFF) {
                        pos.mLow = 0;
                    }
                    return pos;
                }
            } else if (pos.mCode == 0 && pos.mLow == 0) {
                return pos;
            }
            pos = TextRunPos{pos.mRun + 1, 0, 0};
        }
        return end(rText);
    }

private:
    const TextRuns* mText;
    TextRunPos mPos;
    TextRunPos mEnd;
};

namespace MR {
    // The text in runs of a message file's message of that number; false for an index past the messages.
    inline bool getMessageTextRuns(const MessageRunFile& rFile, u32 index, TextRuns* pText) {
        MessageRuns runs;
        if (!rFile.getRuns(index, &runs)) {
            return false;
        }
        *pText = TextRuns{runs.mRuns, runs.mRunNum, rFile.getCodes(), rFile.getTags(), rFile.getParamUnits()};
        return true;
    }

    // The place of the first next-page tag at or after rFrom, or the end: where a page ends (in version 1,
    // getStringLengthWithMessageTag's length).
    TextRunPos findRunsPageEnd(const TextRuns& rText, const TextRunPos& rFrom);
    // The place after the first next-page tag at or after rFrom and after one line feed that follows it, as
    // getNextMessagePage gives it in version 1; false when there is no next page.
    bool findRunsNextPage(const TextRuns& rText, const TextRunPos& rFrom, TextRunPos* pNext);
}  // namespace MR
