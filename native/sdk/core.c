/* The native SDK layer's core: what a missing piece does. */
#include <stdio.h>
#include <stdlib.h>

/* An SDK function the native layer does not implement yet (sdk/stubs). The game reaching one is a gap to fill,
   never something to step over. */
void nebula_sdk_missing(const char* name) {
    fprintf(stderr, "Nebula: the game called %s, which the native SDK layer does not implement yet.\n", name);
    fflush(stderr);
    abort();
}
