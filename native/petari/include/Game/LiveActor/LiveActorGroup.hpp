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
