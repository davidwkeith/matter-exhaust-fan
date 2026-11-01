#define _DEFAULT_SOURCE

#include "settings.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"

#define MAX_ENTRIES 16
#define KEY_LEN     24

typedef struct {
    char key[KEY_LEN];
    int value;
} entry_t;

/* One in-memory copy per file; files are tiny, so every write rewrites
 * the whole file. */
typedef struct {
    const char *name;
    entry_t entries[MAX_ENTRIES];
    size_t count;
} store_t;

static const char *TAG = "settings";
static char s_dir[PATH_MAX];
static store_t s_cal = { .name = "hwcal.conf" };
static store_t s_user = { .name = "user.conf" };

static void store_path(const store_t *s, char *buf, size_t len, bool tmp)
{
    snprintf(buf, len, "%s/%s%s", s_dir, s->name, tmp ? ".tmp" : "");
}

static entry_t *find(store_t *s, const char *key)
{
    for (size_t i = 0; i < s->count; i++) {
        if (strcmp(s->entries[i].key, key) == 0) {
            return &s->entries[i];
        }
    }
    return NULL;
}

static void load(store_t *s)
{
    s->count = 0;
    char path[PATH_MAX + 16];
    store_path(s, path, sizeof(path), false);
    FILE *f = fopen(path, "r");
    if (!f) {
        return; /* missing file == all defaults */
    }
    char line[64];
    while (fgets(line, sizeof(line), f) && s->count < MAX_ENTRIES) {
        char *eq = strchr(line, '=');
        if (!eq || eq == line || (size_t)(eq - line) >= KEY_LEN) {
            continue;
        }
        char *end;
        long v = strtol(eq + 1, &end, 10);
        if (end == eq + 1 || v < 0 || v > 255) {
            continue;
        }
        entry_t *e = &s->entries[s->count++];
        memcpy(e->key, line, (size_t)(eq - line));
        e->key[eq - line] = '\0';
        e->value = (int)v;
    }
    fclose(f);
}

static int fsync_dir(void)
{
    int fd = open(s_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    int r = fsync(fd);
    close(fd);
    return r;
}

static int save(const store_t *s)
{
    char tmp[PATH_MAX + 16], path[PATH_MAX + 16];
    store_path(s, tmp, sizeof(tmp), true);
    store_path(s, path, sizeof(path), false);

    FILE *f = fopen(tmp, "w");
    if (!f) {
        LOGE(TAG, "write %s: %s", tmp, strerror(errno));
        return -1;
    }
    for (size_t i = 0; i < s->count; i++) {
        fprintf(f, "%s=%d\n", s->entries[i].key, s->entries[i].value);
    }
    if (fflush(f) != 0 || fsync(fileno(f)) != 0) {
        LOGE(TAG, "flush %s: %s", tmp, strerror(errno));
        fclose(f);
        unlink(tmp);
        return -1;
    }
    fclose(f);
    if (rename(tmp, path) != 0) {
        LOGE(TAG, "rename %s: %s", path, strerror(errno));
        unlink(tmp);
        return -1;
    }
    return fsync_dir();
}

static uint8_t get_u8(store_t *s, const char *key, uint8_t def)
{
    const entry_t *e = find(s, key);
    return e ? (uint8_t)e->value : def;
}

static int set_u8(store_t *s, const char *key, uint8_t v)
{
    entry_t *e = find(s, key);
    if (e && e->value == v) {
        return 0; /* unchanged: spare the SD card a write */
    }
    if (!e) {
        if (s->count >= MAX_ENTRIES || strlen(key) >= KEY_LEN) {
            return -1;
        }
        e = &s->entries[s->count++];
        strcpy(e->key, key);
    }
    e->value = v;
    return save(s);
}

int settings_init(const char *state_dir)
{
    if (strlen(state_dir) >= sizeof(s_dir)) {
        return -1;
    }
    strcpy(s_dir, state_dir);
    if (access(s_dir, W_OK) != 0) {
        LOGE(TAG, "state dir %s not writable: %s", s_dir, strerror(errno));
        return -1;
    }
    load(&s_cal);
    load(&s_user);
    return 0;
}

uint8_t settings_get_mua_threshold(uint8_t def) { return get_u8(&s_cal, "mua_thr", def); }
int settings_set_mua_threshold(uint8_t v) { return set_u8(&s_cal, "mua_thr", v); }
uint8_t settings_get_mua_hysteresis(uint8_t def) { return get_u8(&s_cal, "mua_hyst", def); }
int settings_set_mua_hysteresis(uint8_t v) { return set_u8(&s_cal, "mua_hyst", v); }
uint8_t settings_get_fan_min_floor(uint8_t def) { return get_u8(&s_cal, "fan_floor", def); }
int settings_set_fan_min_floor(uint8_t v) { return set_u8(&s_cal, "fan_floor", v); }

bool settings_mua_threshold_is_set(void)
{
    return find(&s_cal, "mua_thr") != NULL;
}

uint8_t settings_get_last_used(const char *channel, uint8_t def)
{
    char key[KEY_LEN];
    snprintf(key, sizeof(key), "last_%s", channel);
    return get_u8(&s_user, key, def);
}

int settings_set_last_used(const char *channel, uint8_t percent)
{
    char key[KEY_LEN];
    snprintf(key, sizeof(key), "last_%s", channel);
    return set_u8(&s_user, key, percent);
}

int settings_factory_reset(void)
{
    char path[PATH_MAX + 16];
    store_path(&s_user, path, sizeof(path), false);
    if (unlink(path) != 0 && errno != ENOENT) {
        LOGE(TAG, "factory reset: unlink %s: %s", path, strerror(errno));
        return -1;
    }
    s_user.count = 0;
    LOGW(TAG, "factory reset: user settings erased, calibration preserved");
    return fsync_dir();
}
