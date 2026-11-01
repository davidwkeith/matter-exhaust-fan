#define _DEFAULT_SOURCE

#include "hostname.h"

#include <stdio.h>

int hostname_get(const char *prefix, const char *iface, char *buf,
                 size_t buf_len)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/net/%s/address", iface);
    FILE *f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    unsigned mac[6];
    int got = fscanf(f, "%x:%x:%x:%x:%x:%x", &mac[0], &mac[1], &mac[2],
                     &mac[3], &mac[4], &mac[5]);
    fclose(f);
    if (got != 6) {
        return -1;
    }
    int n = snprintf(buf, buf_len, "%s-%02x%02x%02x", prefix,
                     mac[3] & 0xff, mac[4] & 0xff, mac[5] & 0xff);
    return (n > 0 && (size_t)n < buf_len) ? 0 : -1;
}
