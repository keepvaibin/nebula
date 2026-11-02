#include "Game/Util/MessageUtil.hpp"
#include "Game/Map/RaceManager.hpp"
#include "Game/NPC/TalkMessageInfo.hpp"
#include "Game/Screen/MessageTagSkipTagProcessor.hpp"
#include "Game/System/MessageHolder.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstdio>
#include "Game/Util/ByteView.hpp"

#define MESSAGE_ID_BUFFER_SIZE 256

namespace MR {
    const char16_t* getSystemMessageDirect(const char* pMessageId) {
        TalkMessageInfo messageInfo = TalkMessageInfo();

        MessageSystem::getSystemMessageDirect(&messageInfo, pMessageId);

        return reinterpret_cast< char16_t* >(messageInfo._0);
    }

    const char16_t* getGameMessageDirect(const char* pMessageId) {
        TalkMessageInfo messageInfo = TalkMessageInfo();

        MessageSystem::getGameMessageDirect(&messageInfo, pMessageId);

        return reinterpret_cast< char16_t* >(messageInfo._0);
    }

    const char16_t* getLayoutMessageDirect(const char* pMessageId) {
        TalkMessageInfo messageInfo = TalkMessageInfo();

        MessageSystem::getLayoutMessageDirect(&messageInfo, pMessageId);

        return reinterpret_cast< char16_t* >(messageInfo._0);
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
               getStringLengthWithMessageTag(reinterpret_cast< char16_t* >(messageInfo._0)) != 0;
    }

    const char16_t* getMessageLine(char16_t* pDst, u32 capacity, const char16_t* pMessage, u32 line) {
        u32 currentLine = 0;

        while (*pMessage != 0) {
            if (currentLine == line) {
                break;
            }

            if (*pMessage == 0x1A) {
                pMessage++;
                MessageEditorMessageTag tag(pMessage);
                pMessage += tag.getSkipLength();

                if (tag.isGroupTagId(1, 1)) {
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
            if (*pMessage == 0x1A) {
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

        while (*pMessage != 0) {
            if (*pMessage == 0x1A) {
                pMessage++;
                MessageEditorMessageTag tag(pMessage);
                pMessage += tag.getSkipLength();

                if (tag.isGroupTagId(1, 1)) {
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
            if (*pMessage == 0x1A) {
                pMessage++;
                MessageEditorMessageTag tag(pMessage);
                pMessage += tag.getSkipLength();

                if (tag.getGroup() == 3) {
                    count++;
                } else if (tag.getGroup() == 6) {
                    count += countMessageFigure(readBytes< s32 >(tag.getParamPtr(0)));
                } else if (tag.getGroup() == 5) {
                    count += 3;
                }
                else if (tag.getGroup() == 7) {
                    count += countMessageChar(tag.getParamString());
                } else if (tag.isGroupTagId(1, 1)) {
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
        while (*pMessage != 0) {
            if (*pMessage == 0x1A) {
                pMessage++;
                MessageEditorMessageTag tag(pMessage);
                pMessage += tag.getSkipLength();

                if (tag.isGroupTagId(1, 1)) {
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
        char messageId[MESSAGE_ID_BUFFER_SIZE];
        nebula_snprintf(messageId, sizeof(messageId), "GalaxyName_%s", pGalaxyName);

        return getGameMessageDirect(messageId);
    }

    const char16_t* getGalaxyNameShortOnCurrentLanguage(const char* pGalaxyName) {
        char messageId[MESSAGE_ID_BUFFER_SIZE];
        nebula_snprintf(messageId, sizeof(messageId), "GalaxyNameShort_%s", pGalaxyName);

        return getGameMessageDirect(messageId);
    }

    const char16_t* getScenarioNameOnCurrentLanguage(const char* pGalaxyName, s32 scenarioNo) {
        char messageId[MESSAGE_ID_BUFFER_SIZE];
        nebula_snprintf(messageId, sizeof(messageId), "ScenarioName_%s%d", pGalaxyName, scenarioNo);

        return getGameMessageDirect(messageId);
    }

    void getLayoutMessageID(char* pDst, const char* pSuperMessageId, const char* pSubMessageId) {
        nebula_snprintf(pDst, MESSAGE_ID_BUFFER_SIZE, "Layout_%s%s", pSuperMessageId, pSubMessageId);
    }

    void makeCometMessageID(char* pDst, u32 bufferSize, const char* pCometName) {
        nebula_snprintf(pDst, bufferSize, "CometName_%s", pCometName);
    }
};  // namespace MR
