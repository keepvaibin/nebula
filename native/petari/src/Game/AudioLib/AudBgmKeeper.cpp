#include "Game/AudioLib/AudBgmKeeper.hpp"
#include "Game/AudioLib/AudBgm.hpp"

namespace {
    // Takes the first free slot: its bit goes on in rActive.
    template < typename T, u32 N >
    T* takeFree(T (&rBgms)[N], u8& rActive) {
        for (u32 i = 0; i < N; i++) {
            const u8 mask = static_cast< u8 >(1u << i);
            if ((rActive & mask) == 0) {
                rActive |= mask;
                return &rBgms[i];
            }
        }
        return nullptr;
    }

    template < typename T, u32 N >
    bool tryRelease(T (&rBgms)[N], u8& rActive, const AudBgm* pBgm) {
        for (u32 i = 0; i < N; i++) {
            if (&rBgms[i] == pBgm) {
                rBgms[i].init();
                rActive &= static_cast< u8 >(~(1u << i));
                return true;
            }
        }
        return false;
    }
};  // namespace

AudBgmKeeper::AudBgmKeeper() : mSingleBgmActiveFlags(0), mMultiBgmActiveFlags(0) {
}

AudBgm* AudBgmKeeper::get(BgmType type) {
    switch (type) {
    case BgmType_Single:
        return getValidSingleBgm();
    case BgmType_Multi:
        return getValidMultiBgm();
    }

    return nullptr;
}

void AudBgmKeeper::release(AudBgm* pBgm) {
    if (!::tryRelease(mSingleBgm, mSingleBgmActiveFlags, pBgm)) {
        ::tryRelease(mMultiBgm, mMultiBgmActiveFlags, pBgm);
    }
}

AudSingleBgm* AudBgmKeeper::getValidSingleBgm() {
    return ::takeFree(mSingleBgm, mSingleBgmActiveFlags);
}

AudMultiBgm* AudBgmKeeper::getValidMultiBgm() {
    return ::takeFree(mMultiBgm, mMultiBgmActiveFlags);
}
