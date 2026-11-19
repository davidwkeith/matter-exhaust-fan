/* The controller loop shared by both exhaustd builds: the standalone one
 * (main.c, wall switches only) and the Matter one (linux/matter/), which
 * runs it on its own thread next to the CHIP event loop. */
#pragma once

#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *gpiochip;  /* /dev/gpiochip0 */
    const char *uart;      /* /dev/serial0 */
    const char *state_dir; /* /var/lib/exhaustd */
    const char *iface;     /* wlan0, for the MAC-derived hostname */
} controller_opts_t;

#define CONTROLLER_OPTS_DEFAULT                                     \
    { "/dev/gpiochip0", "/dev/serial0", "/var/lib/exhaustd", "wlan0" }

/* Run until controller_request_stop() or a factory reset; returns the
 * process exit status. Calls matter_start() and matter_poll(). */
int controller_run(const controller_opts_t *opts);

/* Async-signal-safe. */
void controller_request_stop(void);

#ifdef __cplusplus
}
#endif
