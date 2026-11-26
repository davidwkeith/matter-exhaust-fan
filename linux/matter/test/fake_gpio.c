/* Test double for ../../src/gpio.c (same gpio.h interface), used by the
 * off-target end-to-end test (e2e.py): rocker levels are read from the file
 * "inputs" in the working directory (four '0'/'1' raw levels: fan up, fan
 * down, light up, light down; '0' = pressed), and the relay state is
 * written to "relay". Select it with
 *   gn gen out/sim --args='exhaust_gpio_source="//test/fake_gpio.c"'
 */

#include "gpio.h"

#include <stdio.h>

int gpio_open(gpio_lines_t *g, const char *chip_path, const unsigned *inputs,
              size_t num_inputs, int relay_line)
{
    (void)chip_path;
    (void)inputs;
    g->inputs_fd = 0;
    g->num_inputs = num_inputs;
    g->relay_fd = relay_line;
    return 0;
}

int gpio_read_inputs(const gpio_lines_t *g, unsigned *bits)
{
    char levels[8] = { 0 };
    FILE *f = fopen("inputs", "r");
    *bits = (1u << g->num_inputs) - 1; /* nothing pressed */
    if (!f) {
        return 0;
    }
    size_t n = fread(levels, 1, g->num_inputs, f);
    fclose(f);
    for (size_t i = 0; i < n; i++) {
        if (levels[i] == '0') {
            *bits &= ~(1u << i);
        }
    }
    return 0;
}

int gpio_set_relay(const gpio_lines_t *g, bool energized)
{
    (void)g;
    FILE *f = fopen("relay", "w");
    if (!f) {
        return -1;
    }
    fprintf(f, "%d\n", energized ? 1 : 0);
    fclose(f);
    return 0;
}

void gpio_close(gpio_lines_t *g)
{
    (void)g;
}
