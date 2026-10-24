#include "Game/System/MessageHolder.hpp"
#include "Game/NPC/TalkMessageInfo.hpp"
#include "Game/NPC/TalkNodeCtrl.hpp"
#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#ifdef TARGET_PC
#include "Game/System/NebulaText.hpp"
#include <cstring>
#endif
#include <JSystem/JKernel/JKRArchive.hpp>

#define SYSTEMMESSAGE_ARC "/Memory/SystemMessage.arc"
#define MESSAGE_ARC "/MessageData/Message.arc"

namespace {
    u8* getBlock(u32 magic, u8* pData) {
        u32 numBlocks = *reinterpret_cast< NEBULA_BE(u32)* >(pData + 0xc);
        pData += 0x20;

        for (u32 i = 0; i < numBlocks; i++) {
            u32 blockMagic = *reinterpret_cast< NEBULA_BE(u32)* >(pData);

            if (blockMagic == magic) {
                return pData;
            }

            u32 blockSize = *reinterpret_cast< NEBULA_BE(u32)* >(pData + 4);
            pData += blockSize;
        }

        return nullptr;
    }

#ifdef TARGET_PC
    // The last byte of the header's padding, set once the text is in host order.
    const u32 cTextConvertedMarkOffset = 0x1F;

    // DAT1 to host-order UTF-16, in place: every 16-bit unit, except the unit after each 0x1A escape, which holds the
    // tag's length and group bytes (read as bytes, MessageEditorMessageTag). The tag id and parameters are swapped as
    // 16-bit units like the text around them, the form the game's own tags take (MR::addNumberFontTag).
    void convertMessageText(u8* pData, MessageDataBlock* pBlock) {
        if (pData[cTextConvertedMarkOffset] != 0) {
            return;
        }
        pData[cTextConvertedMarkOffset] = 1;

        u8* pText = reinterpret_cast< u8* >(pBlock + 1);
        u8* pEnd = reinterpret_cast< u8* >(pBlock) + pBlock->mBlockSize;
        while (pText + 2 <= pEnd) {
            const u16 unit = nebula_load_be16(pText);
            memcpy(pText, &unit, 2);
            pText += 2;
            if (unit != 0x1A || pText + 2 > pEnd) {
                continue;
            }

            const u32 tagLength = pText[0];
            u8* pTagEnd = pText - 2 + tagLength;
            pTagEnd = pTagEnd > pEnd ? pEnd : pTagEnd;
            for (pText += 2; pText + 2 <= pTagEnd; pText += 2) {
                const u16 value = nebula_load_be16(pText);
                memcpy(pText, &value, 2);
            }
            pText = pTagEnd;
        }
    }
#endif
};  // namespace

bool MessageData::getMessageDirect(TalkMessageInfo* pMessageInfo, const char* pMessage) const {
#ifdef TARGET_PC
    // Nebula's own talk (Nebula_<id>: the Achievement Luma's): an Observatory Luma's talk settings (a normal talk in
    // its balloon, with its voice and camera) and Nebula's words in the played language.
    static const char cNebula[] = "Nebula_";
    if (strncmp(pMessage, cNebula, sizeof(cNebula) - 1) == 0) {
        const wchar_t* pText = NebulaText::getTalk(pMessage + sizeof(cNebula) - 1);
        const s32 templateIndex = findMessageIndex("AstroGalaxy_Tico034");
        if (pText == nullptr || templateIndex < 0 || templateIndex >= mInfoBlock->mItemCount) {
            return false;
        }
        getMessage(pMessageInfo, 0, templateIndex);
        pMessageInfo->_0 = reinterpret_cast< u8* >(const_cast< wchar_t* >(pText));
        return true;
    }
#endif
    s32 messageIndex = findMessageIndex(pMessage);

    if (messageIndex >= 0 && messageIndex < mInfoBlock->mItemCount) {
        getMessage(pMessageInfo, 0, messageIndex);

        return true;
    }

    return false;
}

bool MessageData::getMessage(TalkMessageInfo* pMessageInfo, u16, u16 infoToolIndex) const {
    u8* pInfoTool = getMessageInfoTool(infoToolIndex);
    pMessageInfo->_0 = reinterpret_cast< u8* >(mDataBlock + 1) + *reinterpret_cast< NEBULA_BE(u32)* >(pInfoTool);
    pMessageInfo->mCameraSetID = *reinterpret_cast< NEBULA_BE(u16)* >(pInfoTool + 4);
    pMessageInfo->_6 = *(pInfoTool + 6);
    pMessageInfo->mCameraType = *(pInfoTool + 7);
    pMessageInfo->mTalkType = *(pInfoTool + 8);
    pMessageInfo->_A = *(pInfoTool + 0xa);
    pMessageInfo->_B = *(pInfoTool + 0xb);
    pMessageInfo->mBalloonType = *(pInfoTool + 9);

    return true;
}

TalkNode* MessageData::findNode(const char* pMessage) const {
    s32 messageIndex = findMessageIndex(pMessage);

    for (int i = 0; i < mFlowBlock->mNodeCount; i++) {
        TalkNode* pNode = reinterpret_cast< TalkNode* >(mFlowBlock + 1) + i;

        if (pNode->mNodeType == 1 && pNode->mIndex == messageIndex) {
            return pNode;
        }
    }

    return nullptr;
}

TalkNode* MessageData::getNode(u32 index) const {
    return reinterpret_cast< TalkNode* >(mFlowBlock + 1) + index;
}

TalkNode* MessageData::getBranchNode(u32 index) const {
    return reinterpret_cast< TalkNode* >(mFlowBlock + 1) + _14[index];
}

bool MessageData::isValidBranchNode(u32 index) const {
    return _14[index] != 0xffff;
}

u8* MessageData::getMessageInfoTool(int index) const {
    return reinterpret_cast< u8* >(mInfoBlock + 1) + mInfoBlock->mItemSize * index;
}

MessageHolder::MessageHolder() : mSystemMessageData(nullptr), mGameMessageData(nullptr), mSceneMessageData(nullptr) {
}

void MessageHolder::initSceneData() {
    mSceneMessageData = mGameMessageData;
}

void MessageHolder::destroySceneData() {
    mSceneMessageData = nullptr;
}

bool MessageSystem::getSystemMessageDirect(TalkMessageInfo* pMessageInfo, const char* pMessageId) {
    return MR::getGameSystemObjHolder()->mMessageHolder->mSystemMessageData->getMessageDirect(pMessageInfo, pMessageId);
}

bool MessageSystem::getGameMessageDirect(TalkMessageInfo* pMessageInfo, const char* pMessageId) {
    return MR::getGameSystemObjHolder()->mMessageHolder->mGameMessageData->getMessageDirect(pMessageInfo, pMessageId);
}

bool MessageSystem::getLayoutMessageDirect(TalkMessageInfo* pMessageInfo, const char* pMessageId) {
    return MR::getGameSystemObjHolder()->mMessageHolder->mGameMessageData->getMessageDirect(pMessageInfo, pMessageId);
}

MessageData* MessageSystem::getSceneMessageData() {
    return MR::getGameSystemObjHolder()->mMessageHolder->mSceneMessageData;
}

MessageData::MessageData(const char* pArchiveName)
    : mIDTable(nullptr), mInfoBlock(nullptr), mDataBlock(nullptr), _C(0), mFlowBlock(nullptr), _14(nullptr), _18(nullptr), mFLI1Block(nullptr) {
    JKRArchive* pArchive = nullptr;
    JKRHeap* pHeap = nullptr;
    MR::getMountedArchiveAndHeap(pArchiveName, &pArchive, &pHeap);

    u8* msgData = (u8*)pArchive->getResource("Message.bmg");
    u8* mapData = (u8*)pArchive->getResource(QUESTIONMARK_MAGIC, "MessageId.tbl");

    mIDTable = new JMapInfo();
    mIDTable->attach(mapData);

    mInfoBlock = (MessageInfoBlock*)::getBlock('INF1', msgData);
    mDataBlock = (MessageDataBlock*)::getBlock('DAT1', msgData);
#ifdef TARGET_PC
    if (mDataBlock != nullptr) {
        convertMessageText(msgData, mDataBlock);
    }
#endif
    mFlowBlock = (MessageFlowBlock*)::getBlock('FLW1', msgData);

    if (mFlowBlock != nullptr) {
        _14 = reinterpret_cast< NEBULA_BE(u16)* >(reinterpret_cast< TalkNode* >(mFlowBlock + 1) + mFlowBlock->mNodeCount);
        _18 = reinterpret_cast< u8* >(_14 + mFlowBlock->_A);
    }

    mFLI1Block = (MessageFLI1Block*)::getBlock('FLI1', msgData);
}

inline JMapInfoIter end(const JMapInfo* pInfo) {
    return JMapInfoIter(pInfo, pInfo->getNumEntries());
}

s32 MessageData::findMessageIndex(const char* pMessage) const {
    JMapInfoIter iter = mIDTable->findElementBinary("MessageId", pMessage);

    // This *should* be mIDTable->end(), but I have had trouble
    // getting the compiler to inline that (see comment in JMapInfo.hpp)
    if (iter == end(mIDTable)) {
        return -1;
    }

    s32 messageIndex = -1;
    iter.getValue("Index", &messageIndex);

    return messageIndex;
}

void MessageHolder::initSystemData() {
    mSystemMessageData = new MessageData(SYSTEMMESSAGE_ARC);
}

void MessageHolder::initGameData() {
    MR::mountArchive(MESSAGE_ARC, nullptr);

    mGameMessageData = new MessageData(MESSAGE_ARC);
}
