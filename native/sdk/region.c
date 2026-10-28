/* The release the disc is: North America (RMGE01), Europe (RMGP01) or Japan (RMGJ01), by its game id from the host
   (which plays no other). Their executables differ in a few places only (Petari's VERSION switches; the native game
   follows the disc's through NebulaRegion.hpp), keep their data tables at the same addresses (dol_data.c), and save in
   their own titles' folders. */
#include <revolution/types.h>

#include <nebula_game_api.h>

#include "nebula_sdk.h"

char nebula_disc_region(void) {
    const char* id = nebula_host()->disc_id(nebula_host()->user);
    return id[3];
}

const char* nebula_home_dir(void) {
    switch (nebula_disc_region()) {
    case 'P':
        return "/title/00010000/524d4750/data"; /* RMGP */
    case 'J':
        return "/title/00010000/524d474a/data"; /* RMGJ */
    default:
        return "/title/00010000/524d4745/data"; /* RMGE */
    }
}
