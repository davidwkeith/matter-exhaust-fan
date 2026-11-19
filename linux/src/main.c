/* exhaustd without Matter: argument parsing and signals around the shared
 * controller loop (controller.c). */

#define _DEFAULT_SOURCE

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "controller.h"

static void on_signal(int sig)
{
    (void)sig;
    controller_request_stop();
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [--gpiochip PATH] [--uart PATH] [--state-dir DIR] "
            "[--iface IF]\n"
            "  defaults: /dev/gpiochip0 /dev/serial0 /var/lib/exhaustd wlan0\n",
            argv0);
}

int main(int argc, char **argv)
{
    controller_opts_t opts = CONTROLLER_OPTS_DEFAULT;

    for (int i = 1; i < argc; i++) {
        const char **opt = NULL;
        if (strcmp(argv[i], "--gpiochip") == 0) opt = &opts.gpiochip;
        else if (strcmp(argv[i], "--uart") == 0) opt = &opts.uart;
        else if (strcmp(argv[i], "--state-dir") == 0) opt = &opts.state_dir;
        else if (strcmp(argv[i], "--iface") == 0) opt = &opts.iface;
        if (!opt || i + 1 >= argc) {
            usage(argv[0]);
            return strcmp(argv[i], "--help") == 0 ? EXIT_SUCCESS : 2;
        }
        *opt = argv[++i];
    }
    setvbuf(stderr, NULL, _IOLBF, 0);

    struct sigaction sa = { .sa_handler = on_signal };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    return controller_run(&opts);
}
