#include "Game/System/ArenaOwned.hpp"
#include "Game/System/NebulaArena.hpp"

ArenaOwned::ArenaOwned() {
    mArena = NebulaArena::findFromRoot(this);

    if (mArena) {
        mArena->mOwnedList.append(this);
    }
}

ArenaOwned::~ArenaOwned() {
    if (mArena) {
        mArena->mOwnedList.remove(this);
    }
}
