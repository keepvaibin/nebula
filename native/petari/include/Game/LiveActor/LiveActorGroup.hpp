#pragma once

#include "Game/NameObj/NameObjGroup.hpp"

class LiveActor;

class LiveActorGroup : public NameObjGroup {
public:
    // capacity > 0; the group grows past it.
    LiveActorGroup(const char* pName, int capacity);

    void registerActor(LiveActor* pActor);
    LiveActor* getActor(int index) const;
    // The first dead member in join order, or null.
    LiveActor* getDeadActor() const;
    s32 getLivingActorNum() const;
    // Appears the dead members.
    void appearAll();
    void killAll();

    bool hasActor(const LiveActor* pActor) const {
        for (int i = 0; i < getObjNum(); i++) {
            if (getActor(i) == pActor) {
                return true;
            }
        }

        return false;
    }

    // Every member before mDeadFrom was alive at the last search for a dead one, made when LiveActor::sDeathCount was
    // mDeadCheck. Members only join at the end, and no actor dies but through makeActorDead, so while the count is the
    // same they still are, and the next search starts there (P-137). The plain module's.
    mutable s32 mDeadFrom;
    mutable u64 mDeadCheck;
};

template < typename T >
class DeriveActorGroup : public LiveActorGroup {
public:
    DeriveActorGroup(const char* pName, int capacity) : LiveActorGroup(pName, capacity) {
    }

    T* getMember(int index) const {
        return static_cast< T* >(getActor(index));
    }

    T* getDeadMember() const {
        return static_cast< T* >(getDeadActor());
    }
};
