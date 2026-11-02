#pragma once

#include "JSystem/JAudio2/JAISoundParams.hpp"
#include "JSystem/JAudio2/JASHeapCtrl.hpp"

struct JASTrack;

struct JAISoundChild : JASPoolAllocObject< JAISoundChild > {
    void init();
    void mixOut(JASTrack*);
    void calc();

    JAISoundChild() {
        init();
    }

    JAISoundParamsMove* getMoveParams() {
        return &mMove;
    }

    JAISoundParamsMove mMove;
    JASSoundParams mParams;
};
