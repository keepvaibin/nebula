#include "Game/System/NebulaMods.hpp"
#include "Game/System/MessageHolder.hpp"
#include "Game/NPC/TalkMessageInfo.hpp"
#include "Game/NPC/TalkNodeCtrl.hpp"
#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/System/NebulaText.hpp"
#include <cstring>
#include "Game/System/AssetFolder.hpp"

namespace {
    const char* const cSystemMessageArc = "/Memory/SystemMessage";
    const char* const cMessageArc = "/MessageData/Message";
}  // namespace

bool MessageData::getMessageDirect(TalkMessageInfo* pMessageInfo, const char* pMessage) const {
    if (const char16_t* pWords = NebulaMods::text(pMessage)) {
        pMessageInfo->mMessage = reinterpret_cast< u8* >(const_cast< char16_t* >(pWords));
        return true;
    }

    // Nebula's own talk (Nebula_<id>: the Achievement Luma's): an Observatory Luma's talk settings (a normal talk in
    // its balloon, with its voice and camera) and Nebula's words in the played language.
    static const char cNebula[] = "Nebula_";
    if (MR::isEqualStringPrefix(pMessage, cNebula)) {
        const char16_t* pText = NebulaText::getTalk(pMessage + sizeof(cNebula) - 1);
        const s32 templateIndex = findMessageIndex("AstroGalaxy_Tico034");
        if (pText == nullptr || templateIndex < 0 || static_cast< u32 >(templateIndex) >= getMessageNum()) {
            return false;
        }
        getMessage(pMessageInfo, 0, templateIndex);
        pMessageInfo->mMessage = reinterpret_cast< u8* >(const_cast< char16_t* >(pText));
        return true;
    }
    s32 messageIndex = findMessageIndex(pMessage);

    if (messageIndex >= 0 && static_cast< u32 >(messageIndex) < getMessageNum()) {
        getMessage(pMessageInfo, 0, messageIndex);

        return true;
    }

    return false;
}

bool MessageData::getMessage(TalkMessageInfo* pMessageInfo, u32, u32 index) const {
    const MessageRecord& rRecord = mMessages[index];
    pMessageInfo->mMessage = reinterpret_cast< u8* >(const_cast< char16_t* >(mText + rRecord.mText));
    pMessageInfo->mCameraSetID = rRecord.mCameraSetID;
    pMessageInfo->mSoundId = rRecord.mSoundId;
    pMessageInfo->mCameraType = rRecord.mCameraType;
    pMessageInfo->mTalkType = rRecord.mTalkType;
    pMessageInfo->mAreaId = rRecord.mAreaId;
    pMessageInfo->mAlreadyReadId = rRecord.mAlreadyReadId;
    pMessageInfo->mBalloonType = rRecord.mBalloonType;

    return true;
}

TalkNode* MessageData::findNode(const char* pMessage) const {
    s32 messageIndex = findMessageIndex(pMessage);

    for (u32 i = 0; i < mFile->mNodeNum; i++) {
        TalkNode* pNode = &mNodes[i];

        if (pNode->mNodeType == 1 && static_cast< s32 >(pNode->mIndex) == messageIndex) {
            return pNode;
        }
    }

    return nullptr;
}

TalkNode* MessageData::getNode(u32 index) const {
    return &mNodes[index];
}

TalkNode* MessageData::getBranchNode(u32 index) const {
    return &mNodes[mBranches[index]];
}

bool MessageData::isValidBranchNode(u32 index) const {
    return mBranches[index] != cNoNode;
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
    : mIDTable(nullptr), mFile(nullptr), mMessages(nullptr), mText(nullptr), mNodes(nullptr), mBranches(nullptr) {
    AssetFolder* pArchive = MR::getArchive(pArchiveName);

    u8* msgData = (u8*)pArchive->getResource("Message.bmg");
    u8* mapData = (u8*)pArchive->getResource("MessageId.tbl");

    mIDTable = new Table();
    mIDTable->attach(mapData);

    mFile = reinterpret_cast< const MessageFileHeader* >(msgData);
    if (mFile->mMagic[0] != 'N' || mFile->mMagic[1] != 'M' || mFile->mMagic[2] != 'S' || mFile->mMagic[3] != 'G') {
        NEBULA_PANIC("%s: Message.bmg the importer did not cook", pArchiveName);
    }
    mMessages = reinterpret_cast< const MessageRecord* >(msgData + mFile->mMessageOffset);
    mText = reinterpret_cast< const char16_t* >(msgData + mFile->mTextOffset);
    mNodes = reinterpret_cast< TalkNode* >(msgData + mFile->mNodeOffset);
    mBranches = reinterpret_cast< const u32* >(msgData + mFile->mBranchOffset);
}

s32 MessageData::findMessageIndex(const char* pMessage) const {
    TableIter iter = mIDTable->findElementBinary("MessageId", pMessage);
    if (iter == mIDTable->end()) {
        return -1;
    }

    s32 messageIndex = -1;
    iter.getValue("Index", &messageIndex);

    return messageIndex;
}

void MessageHolder::initSystemData() {
    mSystemMessageData = new MessageData(::cSystemMessageArc);
}

void MessageHolder::initGameData() {
    mGameMessageData = new MessageData(::cMessageArc);
}
