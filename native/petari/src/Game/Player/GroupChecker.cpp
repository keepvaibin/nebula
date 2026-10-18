#include "Game/Player/GroupChecker.hpp"
#include "Game/Util/HashUtil.hpp"

GroupChecker::GroupChecker(const char* pName, u32 a2) : NameObj(pName) {
    mHashTable = new HashSortTable(a2);
}

void GroupChecker::initAfterPlacement() {
    mHashTable->sort();
}

void GroupChecker::add(const NameObj* pObj) {
    const char* name = pObj->mName;
    u32 hashCode = MR::getHashCode(name);
    mHashTable->add(name, 0, true);
}

void GroupCheckManager::add(const NameObj* object, s32 index) {
    mGroups[index]->add(object);
}

bool GroupCheckManager::isExist(const NameObj* object, s32 index) {
    return mGroups[index]->mHashTable->search(object->mName, static_cast< HashSortValue* >(nullptr));
}

GroupChecker::~GroupChecker() {
}

GroupCheckManager::~GroupCheckManager() {
}

GroupCheckManager::GroupCheckManager(const char* pName) : NameObj(pName) {
    mGroups[0] = new GroupChecker("\203\112\203\201\203\124\201\133\203\140\221\316\217\333\225\250\203\117\203\213\201\133\203\166" /* カメサーチ対象物グループ */, 0x20);
    mGroups[1] = new GroupChecker("\203\130\203\163\203\152\203\223\203\117\203\173\203\142\203\116\203\130\224\275\216\313\203\117\203\213\201\133\203\166" /* スピニングボックス反射グループ */, 0x8);
    _14 = 2;
}