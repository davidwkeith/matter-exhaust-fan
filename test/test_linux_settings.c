/* Host tests for the Raspberry Pi controller's file-backed settings
 * (linux/src/settings.c): persistence across reloads, and factory reset
 * keeping hardware calibration (§5.5). */

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "settings.h"

static int failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                    #cond);                                             \
            failures++;                                                 \
        }                                                               \
    } while (0)

int main(void)
{
    char dir[] = "/tmp/exhaustd-test-XXXXXX";
    if (!mkdtemp(dir)) {
        perror("mkdtemp");
        return 1;
    }

    CHECK(settings_init(dir) == 0);
    CHECK(!settings_mua_threshold_is_set());
    CHECK(settings_get_mua_threshold(60) == 60);
    CHECK(settings_get_last_used("fan", 100) == 100);

    CHECK(settings_set_mua_threshold(72) == 0);
    CHECK(settings_set_fan_min_floor(28) == 0);
    CHECK(settings_set_last_used("fan", 40) == 0);
    CHECK(settings_set_last_used("light", 80) == 0);

    /* Reload from disk. */
    CHECK(settings_init(dir) == 0);
    CHECK(settings_mua_threshold_is_set());
    CHECK(settings_get_mua_threshold(60) == 72);
    CHECK(settings_get_fan_min_floor(30) == 28);
    CHECK(settings_get_last_used("fan", 100) == 40);
    CHECK(settings_get_last_used("light", 100) == 80);

    /* Factory reset: user state gone, calibration kept — in memory and on
     * disk. */
    CHECK(settings_factory_reset() == 0);
    CHECK(settings_get_last_used("fan", 100) == 100);
    CHECK(settings_get_mua_threshold(60) == 72);
    CHECK(settings_init(dir) == 0);
    CHECK(settings_get_last_used("light", 100) == 100);
    CHECK(settings_get_fan_min_floor(30) == 28);

    char path[64];
    snprintf(path, sizeof(path), "%s/hwcal.conf", dir);
    unlink(path);
    rmdir(dir);

    if (failures) {
        fprintf(stderr, "%d linux settings check(s) failed\n", failures);
        return 1;
    }
    printf("linux settings tests passed\n");
    return 0;
}
