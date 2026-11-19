#define _DEFAULT_SOURCE

#include "bridge.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "log.h"

#define BRIDGE_ACK_TIMEOUT_MS 20

static const char *TAG = "bridge";
static int s_fd = -1;

int bridge_open(const char *tty_path)
{
    s_fd = open(tty_path, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (s_fd < 0) {
        LOGE(TAG, "open %s: %s", tty_path, strerror(errno));
        return -1;
    }

    struct termios t;
    if (tcgetattr(s_fd, &t) < 0) {
        LOGE(TAG, "tcgetattr %s: %s", tty_path, strerror(errno));
        bridge_close();
        return -1;
    }
    cfmakeraw(&t); /* 8N1, no echo, no line discipline */
    t.c_cflag |= CLOCAL | CREAD;
    t.c_cflag &= ~CRTSCTS;
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    cfsetispeed(&t, B115200);
    cfsetospeed(&t, B115200);
    if (tcsetattr(s_fd, TCSANOW, &t) < 0) {
        LOGE(TAG, "tcsetattr %s: %s", tty_path, strerror(errno));
        bridge_close();
        return -1;
    }
    tcflush(s_fd, TCIOFLUSH);
    LOGI(TAG, "bridge UART up on %s", tty_path);
    return 0;
}

int bridge_set(bridge_channel_t ch, uint8_t power_percent)
{
    if (power_percent > 100) {
        power_percent = 100;
    }
    char cmd[24];
    int n = snprintf(cmd, sizeof(cmd), "SET %s %u\n",
                     ch == BRIDGE_CH_FAN ? "FAN" : "LIGHT", power_percent);
    if (write(s_fd, cmd, (size_t)n) != n) {
        LOGW(TAG, "short write to bridge: %s", strerror(errno));
        return -1;
    }

    /* Reference bridge replies "OK\n" per command. A missing ACK is logged
     * but non-fatal: the wall switch must keep working even with a one-way
     * bridge. TODO(bring-up): tighten to the real bridge's protocol. */
    char resp[8] = { 0 };
    struct pollfd pfd = { .fd = s_fd, .events = POLLIN };
    ssize_t r = 0;
    if (poll(&pfd, 1, BRIDGE_ACK_TIMEOUT_MS) > 0) {
        r = read(s_fd, resp, sizeof(resp) - 1);
    }
    if (r < 2 || strncmp(resp, "OK", 2) != 0) {
        LOGW(TAG, "no ACK for '%.*s' (got %zd bytes)", n - 1, cmd, r);
        tcflush(s_fd, TCIFLUSH);
    }
    return 0;
}

void bridge_close(void)
{
    if (s_fd >= 0) {
        close(s_fd);
        s_fd = -1;
    }
}
