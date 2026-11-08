#include "Game/Enemy/CannonShellBase.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include <algorithm>

CannonShellHolder::CannonShellHolder(int num) {
    mShells.init(num);
}

void CannonShellHolder::registerCannonShell(CannonShellBase* pShell) {
    mShells.push_back(pShell);
}

CannonShellBase* CannonShellHolder::getValidShell() const {
    CannonShellBase* const* found = std::find_if(mShells.begin(), mShells.end(), [](const CannonShellBase* pShell) { return MR::isDead(pShell); });

    return found != mShells.end() ? *found : nullptr;
}

void CannonShellHolder::killActiveShells() const {
    for (CannonShellBase* const* it = mShells.begin(); it != mShells.end(); it++) {
        if (!MR::isDead(*it)) {
            (*it)->kill();
        }
    }
}
