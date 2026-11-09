/* What the module's C layer gives the rest of the module. */
#ifndef NEBULA_MODULE_H
#define NEBULA_MODULE_H

#include <stdarg.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

/* The host's services; NULL before nebula_game_attach. */
const struct NebulaHostServices* nebula_host(void);

/* Log lines and fatal errors, through the host's log and fatal services (stderr before the host attaches). */
void nebula_log(const char* format, ...) __attribute__((format(printf, 1, 2)));
void nebula_vlog(const char* format, va_list args) __attribute__((format(printf, 1, 0)));
void nebula_fatal(const char* format, ...) __attribute__((noreturn, format(printf, 1, 2)));


/* The game's release: 'E' North America, 'P' Europe, 'J' Japan. */
char nebula_game_region(void);

#ifdef __cplusplus
}
#endif

#endif
