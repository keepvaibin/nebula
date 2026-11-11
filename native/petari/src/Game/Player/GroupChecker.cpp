#include "Game/Player/GroupChecker.hpp"
#include "Game/System/LumaCore.hpp"

GroupChecker::GroupChecker(const char* pName, u32 capacity) : NameObj(pName), mNames(capacity) {
    lumacore::setClass(this, lumacore::kClassId< GroupChecker >);
}

void GroupChecker::add(const NameObj* pObj) {
    mNames.add(pObj->mName, 0);
}

void GroupCheckManager::add(const NameObj* object, s32 index) {
    mGroups[index]->add(object);
}

bool GroupCheckManager::isExist(const NameObj* object, s32 index) {
    return mGroups[index]->mNames.find(object->mName) != MR::NameIndex::cNoValue;
}

GroupChecker::~GroupChecker() {
}

GroupCheckManager::~GroupCheckManager() {
}

GroupCheckManager::GroupCheckManager(const char* pName) : NameObj(pName) {
    lumacore::setClass(this, lumacore::kClassId< GroupCheckManager >);
    mGroups[0] = new GroupChecker("カメサーチ対象物グループ", 0x20);
    mGroups[1] = new GroupChecker("スピニングボックス反射グループ", 0x8);
}
