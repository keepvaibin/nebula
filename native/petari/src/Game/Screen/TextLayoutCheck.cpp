#include "Game/Screen/TextLayoutCheck.hpp"
#include "Game/Screen/CustomTagProcessor.hpp"
#include "Game/System/AssetFolder.hpp"
#include "Game/System/Language.hpp"
#include "Game/System/LayoutHolder.hpp"
#include "Game/System/MessageHolder.hpp"
#include "Game/Ui/Layout.hpp"
#include "Game/Ui/Pane.hpp"
#include "Game/Ui/TextBox.hpp"
#include "Game/Ui/WideTextWriter.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/TextRuns.hpp"
#include <nebula_module.h>
#include <string.h>

namespace {
    // A glyph as the writer printed it.
    struct GlyphRecord {
        Ui::CharCode mCode;
        const Ui::Font* mFont;
        f32 mX;
        f32 mY;
        f32 mZ;
        f32 mScaleX;
        f32 mScaleY;
        f32 mFixedWidth;  // -1: the font's own widths
        u32 mColors[4];

        bool operator==(const GlyphRecord& rOther) const {
            return mCode == rOther.mCode && mFont == rOther.mFont && same(mX, rOther.mX) && same(mY, rOther.mY) && same(mZ, rOther.mZ) &&
                   same(mScaleX, rOther.mScaleX) && same(mScaleY, rOther.mScaleY) && same(mFixedWidth, rOther.mFixedWidth) &&
                   memcmp(mColors, rOther.mColors, sizeof(mColors)) == 0;
        }

        // The same number, bit for bit.
        static bool same(f32 a, f32 b) {
            return __builtin_bit_cast(u32, a) == __builtin_bit_cast(u32, b);
        }
    };

    static_assert(sizeof(Ui::CharWriter::VertexColor) == sizeof(u32) * 4, "four colours of four bytes");

    // The glyphs of one layout, in the order printed.
    class LayoutRecorder : public Ui::GlyphRecorder {
    public:
        virtual void glyph(const Ui::CharWriter& rWriter, Ui::CharCode code, f32 x, f32 y) override {
            GlyphRecord& rRecord = mGlyphs.room(mNum + 1)[mNum];
            mNum++;
            rRecord.mCode = code;
            rRecord.mFont = rWriter.GetFont();
            rRecord.mX = x;
            rRecord.mY = y;
            rRecord.mZ = rWriter.mCursorPos.z;
            rRecord.mScaleX = rWriter.GetScaleH();
            rRecord.mScaleY = rWriter.GetScaleV();
            rRecord.mFixedWidth = rWriter.IsWidthFixed() ? rWriter.GetFixedWidth() : -1.0f;
            memcpy(rRecord.mColors, &rWriter.mVertexColor, sizeof(rRecord.mColors));
        }

        MR::GrowBuffer< GlyphRecord > mGlyphs;
        u32 mNum;
        Ui::Rect mRect;
        f32 mWidth;
    };

    // Made at the first test (a pointer: the module runs no constructor of a file's object).
    LayoutRecorder* sOld;
    LayoutRecorder* sNew;
    // The talk balloon's layout the pages are laid out in, built at the first test as LayoutManager::initArc builds it
    // (no LayoutManager: its text boxes' messages are set through ReplaceTagProcessor::Replace, which reads the scene's
    // RaceManager, and the harness calls no function that needs a scene object the stage may not have).
    Ui::Layout* sBalloon;
    // Which languages' system messages the test has mounted (made at the first test).
    bool* sMounted;
    // The pages that differ, told in full up to this many.
    const u32 cToldDiffers = 8;
    // The system messages: a folder a language in this archive (GameSystemObjHolder::initMessageResource mounts the
    // current language's as /Memory/SystemMessage); the test mounts each under a name of its own at its first call.
    const char* const cSystemArchive = "ErrorMessageArchive";

    // The first text box at or under pPane whose name begins "Txt" (the balloon's text; "Sha" is its shadow), or null.
    Ui::TextBox* findTextBox(Ui::Pane* pPane) {
        Ui::TextBox* const pBox = Ui::PaneCast< Ui::TextBox >(pPane);
        if (pBox != nullptr && MR::isEqualStringPrefix(pBox->mName, "Txt")) {
            return pBox;
        }
        for (Ui::Pane* pChild : pPane->GetChildList()) {
            if (Ui::TextBox* const pFound = findTextBox(pChild)) {
                return pFound;
            }
        }
        return nullptr;
    }

    // A page laid out as TextBox::DrawSelf prints it: the box's writer, a processor of its own, nothing drawn. pText: the
    // text of version 1 (length units); else the runs from rBegin to rEnd.
    void layOut(LayoutRecorder* pRecorder, Ui::TextBox* pBox, bool isNumber, const char16_t* pText, int length, const TextRuns* pRuns,
                const TextRunPos& rBegin, const TextRunPos& rEnd) {
        pRecorder->mNum = 0;
        CustomTagProcessor processor(pBox);
        processor.mIsText = MR::isEqualStringPrefix(pBox->mName, "Txt");  // as LayoutCoreUtil::initTextBoxPane sets it
        Ui::WideTextWriter writer;
        writer.SetSilent(true);
        pBox->SetupTextWriter(&writer, isNumber);
        writer.SetTagProcessor(&processor);
        Ui::CharWriter::sGlyphRecorder = pRecorder;
        Ui::Rect rect;
        if (pRuns == nullptr) {
            writer.CalcStringRect(&rect, pText, length);
        } else {
            writer.CalcRunsRect(&rect, *pRuns, rBegin, rEnd);
        }
        rect = pBox->PlaceTextRect(rect);
        pBox->SetupTextColors(&writer);
        writer.SetCursor(rect.left, rect.top);
        pRecorder->mWidth = pRuns == nullptr ? writer.Print(pText, length) : writer.PrintRuns(*pRuns, rBegin, rEnd);
        pRecorder->mRect = rect;
        Ui::CharWriter::sGlyphRecorder = nullptr;
    }

    // The first difference of the two layouts, or none.
    bool sameLayouts(u32* pAt) {
        const LayoutRecorder& rOld = *sOld;
        const LayoutRecorder& rNew = *sNew;
        const u32 num = rOld.mNum < rNew.mNum ? rOld.mNum : rNew.mNum;
        for (u32 i = 0; i < num; i++) {
            if (!(rOld.mGlyphs[i] == rNew.mGlyphs[i])) {
                *pAt = i;
                return false;
            }
        }
        *pAt = num;
        return rOld.mNum == rNew.mNum && GlyphRecord::same(rOld.mWidth, rNew.mWidth) && GlyphRecord::same(rOld.mRect.left, rNew.mRect.left) &&
               GlyphRecord::same(rOld.mRect.top, rNew.mRect.top) && GlyphRecord::same(rOld.mRect.right, rNew.mRect.right) &&
               GlyphRecord::same(rOld.mRect.bottom, rNew.mRect.bottom);
    }

    void tellDiffer(const char* pFile, const MessageData& rData, u32 index, u32 page, u32 at) {
        const char* const pName = rData.findMessageName(index);
        const LayoutRecorder& sOld = *::sOld;
        const LayoutRecorder& sNew = *::sNew;
        nebula_log("text layout: differs: %s %s page %u: %u and %u glyphs, width %.3f and %.3f, rect %.3f %.3f %.3f %.3f and %.3f %.3f %.3f %.3f\n",
                   pFile, pName != nullptr ? pName : "?", page, sOld.mNum, sNew.mNum, sOld.mWidth, sNew.mWidth, sOld.mRect.left, sOld.mRect.top,
                   sOld.mRect.right, sOld.mRect.bottom, sNew.mRect.left, sNew.mRect.top, sNew.mRect.right, sNew.mRect.bottom);
        if (at < sOld.mNum && at < sNew.mNum) {
            const GlyphRecord& rOld = sOld.mGlyphs[at];
            const GlyphRecord& rNew = sNew.mGlyphs[at];
            nebula_log("text layout: differs: glyph %u: U+%04X at %.3f %.3f scale %.3f %.3f colour %08X, and U+%04X at %.3f %.3f scale %.3f %.3f colour %08X\n", at,
                       static_cast< unsigned >(rOld.mCode), rOld.mX, rOld.mY, rOld.mScaleX, rOld.mScaleY, static_cast< unsigned >(rOld.mColors[0]),
                       static_cast< unsigned >(rNew.mCode), rNew.mX, rNew.mY, rNew.mScaleX, rNew.mScaleY, static_cast< unsigned >(rNew.mColors[0]));
        }
    }

    struct Count {
        u32 mFiles;
        u32 mMessages;
        u32 mPages;
        u32 mGlyphs;
        u32 mDiffer;
    };

    // Every page of every message of one message file.
    void checkFile(Ui::TextBox* pBox, const char* pArchive, const char* pFile, Count* pAll) {
        const MessageData* const pData = new MessageData(pArchive);
        Count count = {1, 0, 0, 0, 0};
        for (u32 index = 0; index < pData->getMessageNum(); index++) {
            TextRuns runs;
            if (!MR::getMessageTextRuns(pData->getRunFile(), index, &runs)) {
                count.mDiffer++;
                continue;
            }
            count.mMessages++;
            const char16_t* pPage = pData->mText + pData->mMessages[index].mText;
            TextRunPos pos = TextRunReader::begin();
            for (u32 page = 0;; page++) {
                const int length = MR::getStringLengthWithMessageTag(pPage);
                bool isNumber = length > 0;
                for (int i = 0; i < length && isNumber; i++) {
                    isNumber = pPage[i] >= u'0' && pPage[i] <= u'9';
                }
                layOut(sOld, pBox, isNumber, pPage, length, nullptr, pos, pos);
                layOut(sNew, pBox, isNumber, nullptr, 0, &runs, pos, MR::findRunsPageEnd(runs, pos));
                count.mPages++;
                count.mGlyphs += sOld->mNum;
                u32 at = 0;
                if (!sameLayouts(&at)) {
                    if (pAll->mDiffer + count.mDiffer < cToldDiffers) {
                        tellDiffer(pFile, *pData, index, page, at);
                    }
                    count.mDiffer++;
                }
                const char16_t* const pNextPage = MR::getNextMessagePage(pPage);
                TextRunPos next;
                const bool hasNext = MR::findRunsNextPage(runs, pos, &next);
                if ((pNextPage != nullptr) != hasNext) {
                    nebula_log("text layout: differs: %s message %u page %u: the next page is in one form only\n", pFile, index, page);
                    count.mDiffer++;
                    break;
                }
                if (pNextPage == nullptr) {
                    break;
                }
                pPage = pNextPage;
                pos = next;
            }
        }
        nebula_log("text layout: %s: %u messages, %u pages, %u glyphs, %u differ\n", pFile, count.mMessages, count.mPages, count.mGlyphs,
                   count.mDiffer);
        pAll->mFiles += count.mFiles;
        pAll->mMessages += count.mMessages;
        pAll->mPages += count.mPages;
        pAll->mGlyphs += count.mGlyphs;
        pAll->mDiffer += count.mDiffer;
    }
}  // namespace

namespace MR {
    u32 checkTextLayout() {
        if (sBalloon == nullptr) {
            MR::FormattedName archive;
            MR::makeLayoutArchiveFileNameFromPrefix(archive, "TalkBalloonStretch", true);
            MR::FormattedName name;
            MR::removeExtensionString(name, archive.c_str());
            const char* const pName = MR::getBasename(name.c_str());
            LayoutHolder* const pHolder = MR::createAndAddLayoutHolder(pName);
            const MR::FormattedName resource("%s.brlyt", pName);
            sBalloon = new Ui::Layout();
            sBalloon->Build(pHolder->GetResource('blyt', resource.c_str(), nullptr), pHolder);
            sOld = new LayoutRecorder();
            sNew = new LayoutRecorder();
        }
        Ui::Pane* const pText = sBalloon->FindPane("Text00");
        Ui::TextBox* const pBox = pText != nullptr ? findTextBox(pText) : nullptr;
        if (pBox == nullptr) {
            nebula_log("text layout: the talk balloon has no text box under Text00\n");
            return 1;
        }
        Count all = {0, 0, 0, 0, 0};
        // Each language's game messages and system messages.
        AssetFolder* const pSystem = getArchive(cSystemArchive);
        NebulaArena* const pSystemArena = getArchiveArena(cSystemArchive);
        for (u32 i = 0; i < getLanguageNum(); i++) {
            const char* const pPrefix = getLanguagePrefixByIndex(i);
            const MR::FormattedName game("/%s/MessageData/Message", pPrefix);
            if (isFileExist(game.c_str(), false)) {
                checkFile(pBox, game.c_str(), game.c_str(), &all);
            }
            const MR::FormattedName system("/%s/MessageData/System", pPrefix);
            if (pSystem != nullptr && pSystem->findFolder(system.c_str()) >= 0) {
                const MR::FormattedName mounted("/Memory/TextLayoutCheck/%s", pPrefix);
                if (sMounted == nullptr) {
                    sMounted = new bool[getLanguageNum()]();
                }
                if (!sMounted[i]) {
                    addArchive(pSystem->getSubFolder(system.c_str(), pSystemArena), pSystemArena, mounted.c_str());
                    sMounted[i] = true;
                }
                checkFile(pBox, mounted.c_str(), system.c_str(), &all);
            }
        }
        nebula_log("text layout: %u files, %u messages, %u pages, %u glyphs, %u differ\n", all.mFiles, all.mMessages, all.mPages, all.mGlyphs,
                   all.mDiffer);
        return all.mFiles != 0 ? all.mDiffer : 1;
    }
}  // namespace MR
