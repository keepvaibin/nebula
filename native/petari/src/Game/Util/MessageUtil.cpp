#include "Game/Util/MessageUtil.hpp"
#include "Game/Map/RaceManager.hpp"
#include "Game/NPC/TalkMessageInfo.hpp"
#include "Game/Screen/MessageTagSkipTagProcessor.hpp"
#include "Game/System/MessageHolder.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MessageTag.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"
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

    const char16_t* getMessageLine(char16_t* pDst, u32 capacity, const char16_t* pMessage, u32 line) {
        u32 currentLine = 0;

        // No message (an id the text lacks) has no lines: the empty line.
        if (pMessage == nullptr) {
            pMessage = u"";
        }

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

        for (u32 count = 0; count < capacity; count++) {
            if (*pMessage == cMessageTagEscape) {
                *pDst++ = *pMessage++;
                MessageEditorMessageTag tag(pMessage);

                for (s32 i = 0; i < static_cast< s32 >(tag.getSkipLength()); i++) {
                    *pDst = *pMessage;
                    pMessage++;
                    pDst++;
                }
            }

            if (*pMessage == u'\n' || *pMessage == 0) {
                break;
            }

            *pDst = *pMessage;

            if (count < capacity - 1) {
                pDst++;
                pMessage++;
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
