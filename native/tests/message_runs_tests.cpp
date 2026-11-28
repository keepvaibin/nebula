// SPDX-License-Identifier: GPL-3.0-only
// The runs of a message file (NMSG version 2, Game/System/MessageHolder.hpp MessageRunFile) as the game reads them, on
// the file the cook writes (message_runs_fixture.inc, crates/nebula-import cook/message.rs tests::fixture, which that
// crate's test the_game_reads_the_runs_the_cook_writes keeps equal to the cook's bytes): each message's runs, written back
// in the in-text form of version 1, are the file's own text to its 0; and attach refuses a file whose runs are not whole.
#include "Game/System/MessageHolder.hpp"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <vector>

namespace {
    int sFailures;

    void expect(bool ok, const char* pWhat, u32 index) {
        if (!ok) {
            printf("FAIL %s (%u)\n", pWhat, static_cast< unsigned >(index));
            sFailures++;
        }
    }

    alignas(4) const u8 cFixture[] = {
#include "message_runs_fixture.inc"
    };

    void putUnits(std::vector< u16 >* pOut, char32_t code) {
        if (code >= 0x10000) {
            const u32 c = code - 0x10000;
            pOut->push_back(static_cast< u16 >(0xD800 | (c >> 10)));
            pOut->push_back(static_cast< u16 >(0xDC00 | (c & 0x3FF)));
        } else {
            pOut->push_back(static_cast< u16 >(code));
        }
    }

    // The message's runs in the form of version 1, with its 0.
    std::vector< u16 > unitsOf(const MessageRunFile& rFile, const MessageRuns& rRuns) {
        std::vector< u16 > out;
        std::vector< char32_t > plain;
        for (u32 r = 0; r < rRuns.mRunNum; r++) {
            const MessageRun& rRun = rRuns.mRuns[r];
            if (rRun.mKind == MessageRunKind_Text) {
                for (u32 i = 0; i < rRun.mCount; i++) {
                    putUnits(&out, rFile.getText(rRun)[i]);
                    plain.push_back(rFile.getText(rRun)[i]);
                }
            } else {
                const MessageTagRecord& rTag = rFile.getTag(rRun);
                out.push_back(0x1A);
                out.push_back(static_cast< u16 >((6 + 2 * rTag.mParamNum) | (rTag.mGroup << 8)));
                out.push_back(rTag.mTag);
                for (u32 i = 0; i < rTag.mParamNum; i++) {
                    out.push_back(rFile.getParams(rTag)[i]);
                }
            }
        }
        out.push_back(0);
        // The plain text is the text runs, one after another, and a 0.
        expect(plain.size() == rRuns.mPlainNum && rRuns.mPlainText[rRuns.mPlainNum] == 0 &&
                   (plain.empty() || memcmp(plain.data(), rRuns.mPlainText, plain.size() * sizeof(char32_t)) == 0),
               "the plain text is the text runs", rRuns.mRunNum);
        return out;
    }

    void testRunsAreTheText() {
        MessageRunFile file;
        expect(file.attach(cFixture, static_cast< u32 >(sizeof(cFixture))), "the cook's file attaches", 0);
        const MessageFileHeader* pHeader = reinterpret_cast< const MessageFileHeader* >(cFixture);
        const MessageRecord* pMessages = reinterpret_cast< const MessageRecord* >(cFixture + pHeader->mMessageOffset);
        const u16* pText = reinterpret_cast< const u16* >(cFixture + pHeader->mTextOffset);
        expect(file.getMessageNum() == pHeader->mMessageNum && file.getMessageNum() == 4, "a list a message", file.getMessageNum());
        u32 tags = 0;
        for (u32 m = 0; m < file.getMessageNum(); m++) {
            MessageRuns runs;
            expect(file.getRuns(m, &runs), "a message's runs", m);
            const std::vector< u16 > units = unitsOf(file, runs);
            expect(pMessages[m].mText + units.size() <= pHeader->mTextNum &&
                       memcmp(units.data(), pText + pMessages[m].mText, units.size() * sizeof(u16)) == 0,
                   "the runs are the text to its 0", m);
            for (u32 r = 0; r < runs.mRunNum; r++) {
                tags += runs.mRuns[r].mKind == MessageRunKind_Tag;
            }
        }
        MessageRuns none;
        expect(!file.getRuns(file.getMessageNum(), &none), "no runs past the messages", 0);
        expect(tags == 7, "the fixture's tags", tags);

        // The strings the tags name: the first message's ruby reads U+3054, its sound "SE_A"; the last message's number
        // font text "12". A colour names none.
        MessageRuns first;
        file.getRuns(0, &first);
        const MessageTagRecord& rRuby = file.getTag(first.mRuns[1]);
        expect(rRuby.mGroup == 255 && rRuby.mTag == 2 && rRuby.mStringNum == 1 && file.getString(rRuby)[0] == 0x3054 &&
                   file.getString(rRuby)[1] == 0,
               "the ruby's reading", rRuby.mStringNum);
        const MessageTagRecord& rSound = file.getTag(first.mRuns[3]);
        const char32_t cSound[] = {'S', 'E', '_', 'A', 0};
        expect(rSound.mGroup == 2 && rSound.mStringNum == 4 && memcmp(file.getString(rSound), cSound, sizeof(cSound)) == 0,
               "the sound's name", rSound.mStringNum);
        expect(first.mRuns[4].mKind == MessageRunKind_Text && first.mRuns[4].mCount == 2 && file.getText(first.mRuns[4])[0] == 0x1F600 &&
                   file.getText(first.mRuns[4])[1] == 0xD800,
               "a character past U+FFFF is one code point, a lone surrogate keeps its value", first.mRuns[4].mCount);
        const MessageTagRecord& rColour = file.getTag(first.mRuns[5]);
        expect(rColour.mGroup == 255 && rColour.mTag == 0 && rColour.mStringNum == 0 && file.getString(rColour)[0] == 0,
               "a colour names no string", rColour.mStringNum);
        MessageRuns font;
        file.getRuns(3, &font);
        const MessageTagRecord& rFont = file.getTag(font.mRuns[0]);
        expect(font.mRunNum == 1 && rFont.mGroup == 10 && rFont.mStringNum == 2 && file.getString(rFont)[0] == '1' &&
                   file.getString(rFont)[1] == '2',
               "the number font's text", rFont.mStringNum);
    }

    // A copy of the fixture with one u32 changed at that offset.
    bool attachesWith(size_t offset, u32 value, size_t size = sizeof(cFixture)) {
        alignas(4) static u8 sCopy[sizeof(cFixture)];
        memcpy(sCopy, cFixture, sizeof(cFixture));
        memcpy(sCopy + offset, &value, sizeof(value));
        MessageRunFile file;
        const bool attached = file.attach(sCopy, static_cast< u32 >(size));
        expect(attached == (file.getMessageNum() != 0), "attach leaves nothing attached when it refuses", static_cast< u32 >(offset));
        return attached;
    }

    void testBrokenRunsAreRefused() {
        const MessageFileHeader* pHeader = reinterpret_cast< const MessageFileHeader* >(cFixture);
        const u32 runs = pHeader->mRunsOffset;
        const MessageRunsHeader* pRuns = reinterpret_cast< const MessageRunsHeader* >(cFixture + runs);
        const size_t version = offsetof(MessageFileHeader, mVersion);
        expect(attachesWith(version, cMessageFileVersion), "the fixture unchanged", 0);
        expect(!attachesWith(version, 1), "a file of version 1", 1);
        expect(!attachesWith(offsetof(MessageFileHeader, mRunsOffset), static_cast< u32 >(sizeof(cFixture))), "runs past the file", 2);
        expect(!attachesWith(version, cMessageFileVersion, sizeof(cFixture) - 2), "a file cut short", 3);
        expect(!attachesWith(runs + offsetof(MessageRunsHeader, mListNum), pRuns->mListNum + 1), "a list more than the messages", 4);
        expect(!attachesWith(runs + offsetof(MessageRunsHeader, mCodeNum), 0x40000000u), "code points past the file", 5);
        // The first message's first run, its text's count past the code points.
        expect(!attachesWith(pRuns->mRunOffset + offsetof(MessageRun, mCount), pRuns->mCodeNum + 1), "a text run past the code points", 6);
        // Its second run, a tag, naming a record past the tags, then made a kind of no run.
        expect(!attachesWith(pRuns->mRunOffset + sizeof(MessageRun) + offsetof(MessageRun, mFirst), pRuns->mTagNum), "a tag run past the records", 7);
        expect(!attachesWith(pRuns->mRunOffset + sizeof(MessageRun) + offsetof(MessageRun, mKind), 2), "a run of no kind", 8);
        // The first tag's parameters past the units; its string with no 0 after it.
        expect(!attachesWith(pRuns->mTagOffset + offsetof(MessageTagRecord, mParamNum), pRuns->mParamNum + 1), "parameters past the units", 9);
        expect(!attachesWith(pRuns->mTagOffset + offsetof(MessageTagRecord, mStringNum), 0), "a string with no 0 after it", 10);
        // The first message's plain text with no 0 after it.
        expect(!attachesWith(pRuns->mListOffset + offsetof(MessageRunList, mPlainNum), 1), "a plain text with no 0 after it", 11);
        MessageRunFile none;
        expect(!none.attach(nullptr, 0) && !none.attach(cFixture, static_cast< u32 >(sizeof(MessageFileHeader) - 1)), "no file", 12);
    }
}  // namespace

int main() {
    testRunsAreTheText();
    testBrokenRunsAreRefused();
    if (sFailures != 0) {
        printf("%d failure(s)\n", sFailures);
        return 1;
    }
    printf("message runs: all passed\n");
    return 0;
}
