#pragma once

#include <nebula_types.h>

// One of a user's save files: Mario's game, Luigi's game or the config, of user mUser (1 to 6). Nebula.sav and the old
// saves name it ("mario1", "config3"); the game keys it by these two.
struct SaveFileId {
    enum Kind : u8 {
        Kind_Mario,
        Kind_Luigi,
        Kind_Config,
        Kind_Num,
    };

    Kind mKind;
    s32 mUser;
};
