#pragma once

#include <nebula_types.h>

// The one live instance of each sound service, set by the one that asks.

template < typename T >
class SoundGlobal {
public:
    static T* sInstance;

    SoundGlobal(bool setInstance) {
        if (setInstance) {
            sInstance = (T*)this;
        }
    }

    ~SoundGlobal() {
        if (sInstance == (T*)this) {
            sInstance = nullptr;
        }
    }

    static T* getInstance() {
        return sInstance;
    }
};

template < typename T >
T* SoundGlobal< T >::sInstance;
