/* Rocker inputs and MUA relay output over the kernel GPIO character device
 * (uAPI v2, /dev/gpiochipN). No libgpiod: Debian/Ubuntu still ship the
 * incompatible 1.x API while Raspberry Pi OS Trixie ships 2.x, and the raw
 * ioctls are stable across both.
 *
 * Line numbers are the chip's offsets — BCM numbers on the Pi's gpiochip0.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define GPIO_MAX_INPUTS 8

typedef struct {
    int inputs_fd;
    size_t num_inputs;
    int relay_fd; /* -1 when the board has no MUA relay */
} gpio_lines_t;

/* Request `inputs` as pulled-up inputs and, when relay_line >= 0, the relay
 * line as an output driven low (relay open). Returns 0 on success. */
int gpio_open(gpio_lines_t *g, const char *chip_path,
              const unsigned *inputs, size_t num_inputs, int relay_line);

/* Read all inputs; bit i of *bits is input i's raw level. */
int gpio_read_inputs(const gpio_lines_t *g, unsigned *bits);

int gpio_set_relay(const gpio_lines_t *g, bool energized);

void gpio_close(gpio_lines_t *g);
