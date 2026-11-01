#define _DEFAULT_SOURCE

#include "gpio.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "log.h"

static const char *TAG = "gpio";

static int request_lines(int chip_fd, const unsigned *lines, size_t n,
                         unsigned long long flags, int output_value)
{
    struct gpio_v2_line_request req;
    memset(&req, 0, sizeof(req));
    for (size_t i = 0; i < n; i++) {
        req.offsets[i] = lines[i];
    }
    req.num_lines = n;
    strncpy(req.consumer, "exhaustd", sizeof(req.consumer) - 1);
    req.config.flags = flags;
    if (output_value >= 0) {
        /* Set the level atomically with the direction change, so the relay
         * never glitches closed between request and first write. */
        req.config.num_attrs = 1;
        req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
        req.config.attrs[0].attr.values = output_value ? 1 : 0;
        req.config.attrs[0].mask = 1;
    }
    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
        return -1;
    }
    return req.fd;
}

int gpio_open(gpio_lines_t *g, const char *chip_path,
              const unsigned *inputs, size_t num_inputs, int relay_line)
{
    g->inputs_fd = -1;
    g->relay_fd = -1;
    g->num_inputs = num_inputs;
    if (num_inputs == 0 || num_inputs > GPIO_MAX_INPUTS) {
        return -1;
    }

    int chip_fd = open(chip_path, O_RDWR | O_CLOEXEC);
    if (chip_fd < 0) {
        LOGE(TAG, "open %s: %s", chip_path, strerror(errno));
        return -1;
    }

    /* Rockers close to GND; pull-ups make them active-low (docs/wiring.md
     * recommends external pull-ups too on a long run). */
    g->inputs_fd = request_lines(chip_fd, inputs, num_inputs,
                                 GPIO_V2_LINE_FLAG_INPUT |
                                     GPIO_V2_LINE_FLAG_BIAS_PULL_UP,
                                 -1);
    if (g->inputs_fd < 0) {
        LOGE(TAG, "request rocker inputs: %s", strerror(errno));
        close(chip_fd);
        return -1;
    }

    if (relay_line >= 0) {
        unsigned line = (unsigned)relay_line;
        g->relay_fd = request_lines(chip_fd, &line, 1,
                                    GPIO_V2_LINE_FLAG_OUTPUT, 0);
        if (g->relay_fd < 0) {
            LOGE(TAG, "request relay line %u: %s", line, strerror(errno));
            close(g->inputs_fd);
            close(chip_fd);
            return -1;
        }
    }

    close(chip_fd); /* line fds stay valid without the chip fd */
    return 0;
}

int gpio_read_inputs(const gpio_lines_t *g, unsigned *bits)
{
    struct gpio_v2_line_values v = {
        .mask = (1ULL << g->num_inputs) - 1,
    };
    if (ioctl(g->inputs_fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &v) < 0) {
        return -1;
    }
    *bits = (unsigned)v.bits;
    return 0;
}

int gpio_set_relay(const gpio_lines_t *g, bool energized)
{
    if (g->relay_fd < 0) {
        return 0;
    }
    struct gpio_v2_line_values v = {
        .bits = energized ? 1 : 0,
        .mask = 1,
    };
    return ioctl(g->relay_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &v) < 0 ? -1 : 0;
}

void gpio_close(gpio_lines_t *g)
{
    if (g->inputs_fd >= 0) {
        close(g->inputs_fd);
        g->inputs_fd = -1;
    }
    if (g->relay_fd >= 0) {
        close(g->relay_fd);
        g->relay_fd = -1;
    }
}
