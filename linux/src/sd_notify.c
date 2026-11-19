#define _DEFAULT_SOURCE

#include "sd_notify.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

void sd_notify_send(const char *state)
{
    const char *path = getenv("NOTIFY_SOCKET");
    if (!path || !*path) {
        return;
    }
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    size_t len = strlen(path);
    if (len >= sizeof(addr.sun_path) || (path[0] != '/' && path[0] != '@')) {
        return;
    }
    memcpy(addr.sun_path, path, len);
    if (path[0] == '@') {
        addr.sun_path[0] = '\0'; /* abstract namespace */
    }

    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return;
    }
    socklen_t addr_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + len);
    (void)sendto(fd, state, strlen(state), MSG_NOSIGNAL,
                 (const struct sockaddr *)&addr, addr_len);
    close(fd);
}
