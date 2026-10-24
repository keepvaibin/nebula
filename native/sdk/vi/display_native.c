/* The picture's shape the game draws for: the host latches the output's shape at each retrace and the game's screen
   width, camera aspect and layout canvas follow it. Retraces and the game run on the simulation thread, so a frame
   never sees half a change. */
#include <math.h>

#include <nebula_game_api.h>

#include "nebula_sdk.h"

#define WII_ASPECT (16.0 / 9.0)

static NebulaDisplay sDisplay = {0, 16.0f / 9.0f, 832, 608.0f, 1.0f};
static uint32_t sExtent;

void nebula_display_refresh(void) {
    const NebulaHostServices* host = nebula_host();
    const uint32_t extent = host->display_extent != NULL ? host->display_extent(host->user) : 0;
    if (extent == sExtent) {
        return;
    }
    sExtent = extent;
    const NebulaDisplay wii = {0, 16.0f / 9.0f, 832, 608.0f, 1.0f};
    sDisplay = wii;
    const uint32_t w = extent >> 16, h = extent & 0xFFFF;
    if (w <= 1 || h <= 1) {
        return;
    }
    /* Every shape, held to 9:21 .. 32:9 (the presentation fits the rest). The screen width in even units from the
       game's 832 at 16:9 (608 is the game's own 4:3 mode); below 4:3 it stays 4:3's 624 and the picture grows taller
       (Vert+: CameraContext::getRenderFovy, the layouts' vertical_fit). */
    const double aspect = fmin(fmax((double)w / h, 9.0 / 21.0), 32.0 / 9.0);
    const long width = lround(832.0 * (fmax(aspect, 4.0 / 3.0) / WII_ASPECT) * 0.5) * 2;
    if (width <= 608 || width > 1664) {
        return;
    }
    sDisplay.wide = 1;
    sDisplay.content_aspect = (float)aspect;
    sDisplay.screen_width = (int32_t)width;
    sDisplay.canvas_width = (float)(608.0 * fmax(1.0, aspect / WII_ASPECT));
    sDisplay.vertical_fit = (float)fmax(1.0, WII_ASPECT / aspect);
}

const NebulaDisplay* __NebulaDisplay(void) {
    return &sDisplay;
}
