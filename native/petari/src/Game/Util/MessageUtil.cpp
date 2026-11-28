#include "Game/Util/MessageUtil.hpp"
#include "Game/Map/RaceManager.hpp"
#include "Game/NPC/TalkMessageInfo.hpp"
#include "Game/Screen/MessageTagSkipTagProcessor.hpp"
#include "Game/System/MessageHolder.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MessageTag.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/TextRuns.hpp"
#include "Game/Util/ByteView.hpp"

namespace MR {
    const char16_t* getSystemMessageDirect(const char* pMessageId) {
        TalkMessageInfo messageInfo = TalkMessageInfo();

        MessageSystem::getSystemMessageDirect(&messageInfo, pMessageId);

        return messageInfo.mMessage;
    }

    const char16_t* getGameMessageDirect(const char* pMessageId) {
        TalkMessageInfo messageInfo = TalkMessageInfo();

        MessageSystem::getGameMessageDirect(&messageInfo, pMessageId);

        return messageInfo.mMessage;
    }

    const char16_t* getLayoutMessageDirect(const char* pMessageId) {
        TalkMessageInfo messageInfo = TalkMessageInfo();

        MessageSystem::getLayoutMessageDirect(&messageInfo, pMessageId);

        return messageInfo.mMessage;
    }

    const char16_t* getCurrentGalaxyNameOnCurrentLanguage() {
        return getGalaxyNameOnCurrentLanguage(getCurrentStageName());
    }

    const char16_t* getCurrentScenarioNameOnCurrentLanguage() {
        s32 selectedScenarioNo = getCurrentSelectedScenarioNo();
        s32 scenarioNo = selectedScenarioNo != -1 ? selectedScenarioNo : getCurrentScenarioNo();

        return getScenarioNameOnCurrentLanguage(getCurrentStageName(), scenarioNo);
    }

    const char16_t* getRaceNameOnCurrentLanguage(int raceId) {
        return getGameMessageDirect(RaceManagerFunction::getRaceMessageId(raceId));
    }

    bool isExistGameMessage(const char* pMessageId) {
        TalkMessageInfo messageInfo = TalkMessageInfo();

        return MessageSystem::getGameMessageDirect(&messageInfo, pMessageId) &&
               getStringLengthWithMessageTag(messageInfo.mMessage) != 0;
    }

    namespace {
        // Where line `line` of a message's first page begins (its end when the page has fewer lines); a null message (an id
        // the text lacks) has no lines: the empty line.
        const char16_t* findMessageLine(const char16_t* pMessage, u32 line) {
            if (pMessage == nullptr) {
                return u"";
            }

            u32 currentLine = 0;
            while (*pMessage != 0) {
                if (currentLine == line) {
                    break;
                }

                if (*pMessage == cMessageTagEscape) {
                    pMessage++;
                    MessageEditorMessageTag tag(pMessage);
                    pMessage += tag.getSkipLength();

                    if (tag.isGroupTagId(MessageTagGroup_Display, cMessageTagNextPage)) {
                        break;
                    }
                } else {
                    if (*pMessage == u'\n') {
                        currentLine++;
                    }

                    pMessage++;
                }
            }
            return pMessage;
        }

        // The units of the tag whose escape is at pTag, the escape included.
        u32 tagUnits(const char16_t* pTag) {
            return 1 + MessageEditorMessageTag(pTag + 1).getSkipLength();
        }
    }  // namespace

    u32 getMessageLineLength(const char16_t* pMessage, u32 line) {
        const char16_t* pAt = findMessageLine(pMessage, line);
        u32 units = 0;
        for (;;) {
            if (*pAt == cMessageTagEscape) {
                const u32 tag = tagUnits(pAt);
                pAt += tag;
                units += tag;
            } else if (*pAt == u'\n' || *pAt == 0) {
                return units;
            } else {
                pAt++;
                units++;
            }
        }
    }

    const char16_t* getMessageLine(char16_t* pDst, u32 capacity, const char16_t* pMessage, u32 line) {
        // No room: nothing is written, not even the terminator.
        if (capacity == 0) {
            return pDst;
        }

        pMessage = findMessageLine(pMessage, line);
        // The line's characters and whole tags while they fit before the terminator; a tag that does not fit ends the copy.
        u32 room = capacity - 1;
        for (;;) {
            if (*pMessage == cMessageTagEscape) {
                const u32 tag = tagUnits(pMessage);
                if (tag > room) {
                    break;
                }
                for (u32 i = 0; i < tag; i++) {
                    *pDst++ = *pMessage++;
                }
                room -= tag;
            } else if (*pMessage == u'\n' || *pMessage == 0 || room == 0) {
                break;
            } else {
                *pDst++ = *pMessage++;
                room--;
            }
        }

        *pDst = 0;
        return pDst;
    }

    s32 countMessageLine(const char16_t* pMessage) {
        s32 count = 1;

        // No message (an id the text lacks) is one empty line.
        if (pMessage == nullptr) {
            return count;
        }

        while (*pMessage != 0) {
            if (*pMessage == cMessageTagEscape) {
                pMessage++;
                MessageEditorMessageTag tag(pMessage);
                pMessage += tag.getSkipLength();

                if (tag.isGroupTagId(MessageTagGroup_Display, cMessageTagNextPage)) {
                    break;
                }
            } else {
                if (*pMessage == u'\n') {
                    count++;
                }

                pMessage++;
            }
        }

        return count;
    }

    s32 countMessageChar(const char16_t* pMessage) {
        if (pMessage == nullptr) {
            return 0;
        }

        s32 count = 0;

        while (*pMessage != 0) {
            if (*pMessage == cMessageTagEscape) {
                pMessage++;
                MessageEditorMessageTag tag(pMessage);
                pMessage += tag.getSkipLength();

                if (tag.getGroup() == MessageTagGroup_Picture) {
                    count++;
                } else if (tag.getGroup() == MessageTagGroup_Number) {
                    count += countMessageFigure(readBytes< s32 >(tag.getParamPtr(0)));
                } else if (tag.getGroup() == MessageTagGroup_Localize) {
                    count += 3;
                } else if (tag.getGroup() == MessageTagGroup_String) {
                    count += countMessageChar(tag.getParamString());
                } else if (tag.isGroupTagId(MessageTagGroup_Display, cMessageTagNextPage)) {
                    break;
                }
            } else {
                pMessage++;
                count++;
            }
        }

        return count;
    }

    s32 countMessageFigure(s32 value) {
        u32 magnitude = MR::abs(value);
        s32 count = 1;

        while ((magnitude /= 10) != 0) {
            count++;
        }

        return count;
    }

    const char16_t* getNextMessagePage(const char16_t* pMessage) {
        // No message (an id the text lacks) has no next page.
        if (pMessage == nullptr) {
            return nullptr;
        }

        while (*pMessage != 0) {
            if (*pMessage == cMessageTagEscape) {
                pMessage++;
                MessageEditorMessageTag tag(pMessage);
                pMessage += tag.getSkipLength();

                if (tag.isGroupTagId(MessageTagGroup_Display, cMessageTagNextPage)) {
                    if (*pMessage == u'\n') {
                        pMessage++;
                    }

                    return pMessage;
                }
            } else {
                pMessage++;
            }
        }

        return nullptr;
    }

    TextRunPos findRunsPageEnd(const TextRuns& rText, const TextRunPos& rFrom) {
        for (TextRunPos pos = TextRunReader::normal(rText, rFrom); pos.mRun < rText.mRunNum; pos = TextRunReader::normal(rText, TextRunPos{pos.mRun + 1, 0, 0})) {
            const MessageRun& rRun = rText.mRuns[pos.mRun];
            if (rRun.mKind == MessageRunKind_Tag && rText.getTag(rRun).mGroup == MessageTagGroup_Display &&
                rText.getTag(rRun).mTag == cMessageTagNextPage) {
                return pos;
            }
        }
        return TextRunReader::end(rText);
    }

    bool findRunsNextPage(const TextRuns& rText, const TextRunPos& rFrom, TextRunPos* pNext) {
        const TextRunPos pageEnd = findRunsPageEnd(rText, rFrom);
        if (pageEnd.mRun >= rText.mRunNum) {
            return false;
        }
        TextRunReader reader(rText, TextRunReader::end(rText));
        reader.Set(TextRunPos{pageEnd.mRun + 1, 0, 0});
        const TextRunPos after = reader.GetPos();
        u16 code = 0;
        const MessageTagRecord* pTag = nullptr;
        *pNext = reader.Next(&code, &pTag) == TextRunReader::Item_Char && code == u'\n' ? reader.GetPos() : after;
        return true;
    }

    const char16_t* getGalaxyNameOnCurrentLanguage(const char* pGalaxyName) {
        MR::FormattedName messageId("GalaxyName_%s", pGalaxyName);

        return getGameMessageDirect(messageId.c_str());
    }

    const char16_t* getGalaxyNameShortOnCurrentLanguage(const char* pGalaxyName) {
        MR::FormattedName messageId("GalaxyNameShort_%s", pGalaxyName);

        return getGameMessageDirect(messageId.c_str());
    }

    const char16_t* getScenarioNameOnCurrentLanguage(const char* pGalaxyName, s32 scenarioNo) {
        MR::FormattedName messageId("ScenarioName_%s%d", pGalaxyName, scenarioNo);

        return getGameMessageDirect(messageId.c_str());
    }

    void getLayoutMessageID(FormattedName& rDst, const char* pSuperMessageId, const char* pSubMessageId) {
        rDst.format("Layout_%s%s", pSuperMessageId, pSubMessageId);
    }

    void makeCometMessageID(FormattedName& rDst, const char* pCometName) {
        rDst.format("CometName_%s", pCometName);
    }
};  // namespace MR
