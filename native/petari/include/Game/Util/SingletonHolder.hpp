#pragma once

#include "Game/System/NebulaArena.hpp"

// One object for the whole run: made in the root arena (with whatever it makes as it is built), so a first init during
// a scene does not leave it in the scene's arena.
template < typename T >
class SingletonHolder {
public:
    static void init() {
        if (sInstance == nullptr) {
            NebulaArena::CurrentScope scope(NebulaArena::root());
            sInstance = new T();
        }
    }

    static T* get() {
        return sInstance;
    }

private:
    static T* sInstance;
};

template < typename T >
T* SingletonHolder< T >::sInstance;

template < typename T >
class AudSingletonHolder {
public:
    static void init() {
        if (sInstance == nullptr) {
            NebulaArena::CurrentScope scope(NebulaArena::root());
            sInstance = new T();
        }
    }

    static T* get() {
        return sInstance;
    }

private:
    static T* sInstance;
};

template < typename T >
T* AudSingletonHolder< T >::sInstance;
