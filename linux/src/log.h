/* stderr logging with sd-daemon priority prefixes, so journald files each
 * line at the right level (systemd's SyslogLevelPrefix= default). */
#pragma once

#include <stdio.h>

#define LOGE(tag, fmt, ...) fprintf(stderr, "<3>%s: " fmt "\n", tag, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) fprintf(stderr, "<4>%s: " fmt "\n", tag, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) fprintf(stderr, "<6>%s: " fmt "\n", tag, ##__VA_ARGS__)
