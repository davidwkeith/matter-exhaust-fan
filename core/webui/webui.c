#include "webui.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

#include "settings.h"
#include "wifi_maintenance.h"

static const char *TAG = "webui";

static webui_config_t s_cfg;

/* --- auth (§7.1) -------------------------------------------------------
 * Password is optional; storage format is "<salt-hex>$<sha256-hex>" in the
 * existing pass_hash NVS string. Session state is a single in-memory token
 * (one admin at a time) handed out as an HttpOnly cookie on login — no
 * persistent Basic Auth prompt, lost on reboot or password change. */

#define PASS_SALT_LEN 16
#define PASS_HASH_LEN 32
#define PASS_HASH_STR_LEN (PASS_SALT_LEN * 2 + 1 + PASS_HASH_LEN * 2)
#define SESSION_TOKEN_LEN 16
#define PASS_MAX_LEN 64

static bool s_session_active;
static char s_session_token[SESSION_TOKEN_LEN * 2 + 1];

static void hex_encode(const uint8_t *in, size_t len, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = digits[in[i] >> 4];
        out[i * 2 + 1] = digits[in[i] & 0xf];
    }
    out[len * 2] = '\0';
}

static bool hex_decode(const char *in, uint8_t *out, size_t out_len)
{
    for (size_t i = 0; i < out_len; i++) {
        char hi = in[i * 2], lo = in[i * 2 + 1];
        if (!isxdigit((unsigned char)hi) || !isxdigit((unsigned char)lo)) {
            return false;
        }
        int hv = (hi <= '9') ? hi - '0' : (tolower((unsigned char)hi) - 'a' + 10);
        int lv = (lo <= '9') ? lo - '0' : (tolower((unsigned char)lo) - 'a' + 10);
        out[i] = (uint8_t)((hv << 4) | lv);
    }
    return true;
}

static bool webui_pass_is_set(void)
{
    char buf[PASS_HASH_STR_LEN + 1];
    return settings_get_webui_pass_hash(buf, sizeof(buf)) == ESP_OK;
}

static esp_err_t store_password(const char *password)
{
    uint8_t salt[PASS_SALT_LEN];
    esp_fill_random(salt, sizeof(salt));

    uint8_t combined[PASS_SALT_LEN + PASS_MAX_LEN];
    size_t plen = strlen(password);
    memcpy(combined, salt, PASS_SALT_LEN);
    memcpy(combined + PASS_SALT_LEN, password, plen);

    uint8_t hash[PASS_HASH_LEN];
    mbedtls_sha256(combined, PASS_SALT_LEN + plen, hash, 0);

    char salt_hex[PASS_SALT_LEN * 2 + 1], hash_hex[PASS_HASH_LEN * 2 + 1];
    hex_encode(salt, PASS_SALT_LEN, salt_hex);
    hex_encode(hash, PASS_HASH_LEN, hash_hex);

    char stored[PASS_HASH_STR_LEN + 1];
    snprintf(stored, sizeof(stored), "%s$%s", salt_hex, hash_hex);
    return settings_set_webui_pass_hash(stored);
}

/* Constant-time-ish compare of the derived hash — avoids leaking a match
 * position via early exit, which is what actually matters against timing
 * attacks on a LAN password check. */
static bool verify_password(const char *password)
{
    char stored[PASS_HASH_STR_LEN + 1];
    if (settings_get_webui_pass_hash(stored, sizeof(stored)) != ESP_OK) {
        return false;
    }
    char *sep = strchr(stored, '$');
    if (!sep || (size_t)(sep - stored) != PASS_SALT_LEN * 2 ||
        strlen(sep + 1) != PASS_HASH_LEN * 2) {
        return false;
    }
    *sep = '\0';

    uint8_t salt[PASS_SALT_LEN], expect_hash[PASS_HASH_LEN];
    if (!hex_decode(stored, salt, PASS_SALT_LEN) ||
        !hex_decode(sep + 1, expect_hash, PASS_HASH_LEN)) {
        return false;
    }

    uint8_t combined[PASS_SALT_LEN + PASS_MAX_LEN];
    size_t plen = strlen(password);
    if (plen > PASS_MAX_LEN) {
        plen = PASS_MAX_LEN;
    }
    memcpy(combined, salt, PASS_SALT_LEN);
    memcpy(combined + PASS_SALT_LEN, password, plen);

    uint8_t actual_hash[PASS_HASH_LEN];
    mbedtls_sha256(combined, PASS_SALT_LEN + plen, actual_hash, 0);

    uint8_t diff = 0;
    for (size_t i = 0; i < PASS_HASH_LEN; i++) {
        diff |= actual_hash[i] ^ expect_hash[i];
    }
    return diff == 0;
}

/* Fills cookie_hdr with a fresh Set-Cookie value and activates the session;
 * caller keeps the buffer alive until httpd_resp_send (esp_http_server
 * doesn't copy header values). */
static void issue_session(char *cookie_hdr, size_t len)
{
    uint8_t tok[SESSION_TOKEN_LEN];
    esp_fill_random(tok, sizeof(tok));
    hex_encode(tok, SESSION_TOKEN_LEN, s_session_token);
    s_session_active = true;
    snprintf(cookie_hdr, len, "session=%s; Path=/; HttpOnly; SameSite=Strict",
             s_session_token);
}

static void clear_session(void)
{
    s_session_active = false;
    memset(s_session_token, 0, sizeof(s_session_token));
}

static bool cookie_get(const char *hdr, const char *key, char *out, size_t len)
{
    size_t klen = strlen(key);
    const char *p = hdr;
    while (p && *p) {
        while (*p == ' ') {
            p++;
        }
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *v = p + klen + 1;
            const char *end = strchr(v, ';');
            size_t vlen = end ? (size_t)(end - v) : strlen(v);
            if (vlen >= len) {
                vlen = len - 1;
            }
            memcpy(out, v, vlen);
            out[vlen] = '\0';
            return true;
        }
        p = strchr(p, ';');
        if (p) {
            p++;
        }
    }
    return false;
}

static bool session_valid(httpd_req_t *req)
{
    if (!s_session_active) {
        return false;
    }
    char hdr[160];
    if (httpd_req_get_hdr_value_str(req, "Cookie", hdr, sizeof(hdr)) != ESP_OK) {
        return false;
    }
    char token[SESSION_TOKEN_LEN * 2 + 1];
    if (!cookie_get(hdr, "session", token, sizeof(token))) {
        return false;
    }
    return strcmp(token, s_session_token) == 0;
}

/* Gate for POST handlers and /update: open access when no password is set,
 * otherwise require a valid session cookie. Sends the redirect itself on
 * failure — caller just returns when this returns false. */
static bool require_auth(httpd_req_t *req)
{
    if (!webui_pass_is_set() || session_valid(req)) {
        return true;
    }
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/login");
    httpd_resp_send(req, NULL, 0);
    return false;
}

/* --- style-guide chrome (docs/style-guide.md): system fonts, one accent
 * color, minimal markup, ~40rem column ------------------------------- */

static const char PAGE_HEAD[] =
    "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>Exhaust Fan Controller</title><style>"
    ":root{--accent:#0a7ea4}"
    "body{font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",Roboto,"
    "Helvetica,Arial,sans-serif;max-width:40rem;margin:2rem auto;"
    "padding:0 1rem;color:#1a1a1a;background:#fff;line-height:1.5}"
    "h1{font-size:1.4rem}a{color:var(--accent)}"
    "nav ul{list-style:none;padding:0}nav li{margin:.5rem 0}"
    "label{display:block;margin:.75rem 0 .25rem}"
    "input{font:inherit;padding:.3rem;width:12rem}"
    "button{font:inherit;margin-top:1rem;padding:.4rem 1.2rem;"
    "background:var(--accent);color:#fff;border:0;border-radius:.25rem}"
    ".note{color:#555;font-size:.9rem}"
    ".ok{color:var(--accent)}"
    "</style></head><body>";

static const char PAGE_FOOT[] = "<p><a href=\"/\">Home</a></p></body></html>";

/* --- tiny form helpers ------------------------------------------------ */

/* Percent-decode + '+'→space, in place. */
static void url_decode(char *s)
{
    char *out = s;
    while (*s) {
        if (*s == '+') {
            *out++ = ' ';
            s++;
        } else if (*s == '%' && s[1] && s[2]) {
            char hex[3] = { s[1], s[2], 0 };
            *out++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else {
            *out++ = *s++;
        }
    }
    *out = '\0';
}

/* Extract a field from an application/x-www-form-urlencoded body. */
static bool form_get(const char *body, const char *key, char *val, size_t len)
{
    size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *v = p + klen + 1;
            const char *end = strchr(v, '&');
            size_t vlen = end ? (size_t)(end - v) : strlen(v);
            if (vlen >= len) {
                vlen = len - 1;
            }
            memcpy(val, v, vlen);
            val[vlen] = '\0';
            url_decode(val);
            return true;
        }
        p = strchr(p, '&');
        if (p) {
            p++;
        }
    }
    return false;
}

static bool form_get_u8(const char *body, const char *key, uint8_t min,
                        uint8_t max, uint8_t *out)
{
    char buf[8];
    if (!form_get(body, key, buf, sizeof(buf))) {
        return false;
    }
    char *end;
    long v = strtol(buf, &end, 10);
    if (end == buf || *end != '\0' || v < min || v > max) {
        return false;
    }
    *out = (uint8_t)v;
    return true;
}

static esp_err_t read_body(httpd_req_t *req, char *buf, size_t len)
{
    if (req->content_len >= len) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t off = 0;
    while (off < req->content_len) {
        int r = httpd_req_recv(req, buf + off, req->content_len - off);
        if (r <= 0) {
            return ESP_FAIL;
        }
        off += r;
    }
    buf[off] = '\0';
    return ESP_OK;
}

static esp_err_t send_page(httpd_req_t *req, const char *body_html)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, PAGE_HEAD, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, body_html, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, PAGE_FOOT, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* --- pages ------------------------------------------------------------ */

static esp_err_t index_get(httpd_req_t *req)
{
    bool has_pass = webui_pass_is_set();
    bool logged_in = has_pass && session_valid(req);

    char body[1280];
    snprintf(body, sizeof(body),
        "<h1>Exhaust Fan Controller</h1>"
        "%s"
        "<p>Local maintenance interface. Normal operation never requires "
        "this page.</p>"
        "<nav><ul>"
        "<li><a href=\"/calibration\">Calibration</a> — makeup-air "
        "threshold, minimum fan speed</li>"
        "<li><a href=\"/hostname\">Device name</a> — cosmetic label only</li>"
        "<li><a href=\"/wifi\">Wi-Fi</a> — maintenance network</li>"
        "<li><a href=\"/update\">Firmware update</a></li>"
        "<li><a href=\"/password\">Web UI password</a> — %s</li>"
        "</ul></nav>"
        "%s",
        wifi_maintenance_in_setup_mode()
            ? "<p class=\"note\"><strong>Setup mode:</strong> this open "
              "access point exists only to configure Wi-Fi. Please finish "
              "<a href=\"/wifi\">Wi-Fi setup</a> promptly.</p>"
            : "",
        has_pass ? "set" : "not set",
        !has_pass
            ? ""
            : (logged_in
                ? "<form method=\"post\" action=\"/logout\"><button>Log "
                  "out</button></form>"
                : "<p class=\"note\">Changing settings or the firmware "
                  "requires <a href=\"/login\">logging in</a>.</p>"));
    return send_page(req, body);
}

static esp_err_t login_get(httpd_req_t *req)
{
    if (session_valid(req)) {
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/");
        return httpd_resp_send(req, NULL, 0);
    }
    return send_page(req,
        "<h1>Log in</h1>"
        "<form method=\"post\" action=\"/login\">"
        "<label for=\"p\">Password</label>"
        "<input id=\"p\" name=\"pass\" type=\"password\" required autofocus>"
        "<button>Log in</button></form>");
}

static esp_err_t login_post(httpd_req_t *req)
{
    char body[256];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    }
    char pass[PASS_MAX_LEN + 1];
    if (!form_get(body, "pass", pass, sizeof(pass))) {
        pass[0] = '\0';
    }
    if (!webui_pass_is_set() || !verify_password(pass)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        return send_page(req,
            "<h1>Log in</h1><p class=\"note\">Incorrect password.</p>"
            "<form method=\"post\" action=\"/login\">"
            "<label for=\"p\">Password</label>"
            "<input id=\"p\" name=\"pass\" type=\"password\" required "
            "autofocus>"
            "<button>Log in</button></form>");
    }
    char cookie_hdr[80];
    issue_session(cookie_hdr, sizeof(cookie_hdr));
    httpd_resp_set_hdr(req, "Set-Cookie", cookie_hdr);
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t logout_post(httpd_req_t *req)
{
    clear_session();
    httpd_resp_set_hdr(req, "Set-Cookie",
        "session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static const char PASSWORD_NOTE[] =
    "<p class=\"note\">Protects settings changes and firmware updates. "
    "Served over plain HTTP — this stops casual access on a shared "
    "network, not an attacker already on the path.</p>";

static esp_err_t password_get(httpd_req_t *req)
{
    bool has_pass = webui_pass_is_set();
    if (has_pass && !require_auth(req)) {
        return ESP_OK;
    }

    char body[1024];
    if (has_pass) {
        snprintf(body, sizeof(body),
            "<h1>Web UI password</h1>%s"
            "<form method=\"post\" action=\"/password\">"
            "<label for=\"c\">Current password</label>"
            "<input id=\"c\" name=\"current\" type=\"password\" required>"
            "<label for=\"n\">New password</label>"
            "<input id=\"n\" name=\"new1\" type=\"password\" minlength=\"8\" "
            "required>"
            "<label for=\"n2\">Confirm new password</label>"
            "<input id=\"n2\" name=\"new2\" type=\"password\" minlength=\"8\" "
            "required>"
            "<button>Change password</button></form>"
            "<form method=\"post\" action=\"/password\">"
            "<input type=\"hidden\" name=\"action\" value=\"remove\">"
            "<label for=\"c2\">Current password</label>"
            "<input id=\"c2\" name=\"current\" type=\"password\" required>"
            "<button>Remove password (open access)</button></form>",
            PASSWORD_NOTE);
    } else {
        snprintf(body, sizeof(body),
            "<h1>Web UI password</h1>"
            "<p class=\"note\">Optional — the device stays open on the LAN "
            "until you set one.</p>%s"
            "<form method=\"post\" action=\"/password\">"
            "<label for=\"n\">New password</label>"
            "<input id=\"n\" name=\"new1\" type=\"password\" minlength=\"8\" "
            "required>"
            "<label for=\"n2\">Confirm new password</label>"
            "<input id=\"n2\" name=\"new2\" type=\"password\" minlength=\"8\" "
            "required>"
            "<button>Set password</button></form>",
            PASSWORD_NOTE);
    }
    return send_page(req, body);
}

static esp_err_t password_post(httpd_req_t *req)
{
    bool has_pass = webui_pass_is_set();
    if (has_pass && !require_auth(req)) {
        return ESP_OK;
    }

    char body[640];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    }

    char current[PASS_MAX_LEN + 1] = "";
    form_get(body, "current", current, sizeof(current));
    if (has_pass && !verify_password(current)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        return send_page(req,
            "<h1>Web UI password</h1><p class=\"note\">Current password is "
            "incorrect. Nothing changed.</p>"
            "<p><a href=\"/password\">Back</a></p>");
    }

    char action[16] = "set";
    form_get(body, "action", action, sizeof(action));
    if (strcmp(action, "remove") == 0) {
        settings_clear_webui_pass_hash();
        clear_session();
        ESP_LOGW(TAG, "web UI password removed");
        httpd_resp_set_hdr(req, "Set-Cookie",
            "session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
        return send_page(req,
            "<h1>Web UI password</h1><p class=\"ok\">Removed — the device "
            "is open on the LAN again.</p>");
    }

    char new1[PASS_MAX_LEN + 1], new2[PASS_MAX_LEN + 1];
    if (!form_get(body, "new1", new1, sizeof(new1)) || strlen(new1) < 8) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "password must be at least 8 characters");
    }
    if (!form_get(body, "new2", new2, sizeof(new2)) ||
        strcmp(new1, new2) != 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "passwords don't match");
    }
    if (store_password(new1) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "could not save password");
    }
    ESP_LOGI(TAG, "web UI password %s", has_pass ? "changed" : "set");

    char cookie_hdr[80];
    issue_session(cookie_hdr, sizeof(cookie_hdr));
    httpd_resp_set_hdr(req, "Set-Cookie", cookie_hdr);
    return send_page(req,
        "<h1>Web UI password</h1><p class=\"ok\">Saved.</p>"
        "<p><a href=\"/\">Home</a></p>");
}

static esp_err_t calibration_get(httpd_req_t *req)
{
    uint8_t thr = settings_get_mua_threshold(s_cfg.mua_threshold_default);
    uint8_t hyst = settings_get_mua_hysteresis(s_cfg.mua_hysteresis_default);
    uint8_t floor = settings_get_fan_min_floor(s_cfg.fan_floor_default);

    char body[1024];
    snprintf(body, sizeof(body),
        "<h1>Calibration</h1>"
        "%s"
        "<p class=\"note\">These values describe the installed fan and "
        "survive factory reset. See docs/calibration.md for the measurement "
        "procedure (CA Mechanical Code 505.2 — 400 CFM).</p>"
        "<form method=\"post\">"
        "<label for=\"t\">Makeup-air threshold (%% fan speed)</label>"
        "<input id=\"t\" name=\"mua_threshold\" type=\"number\" min=\"1\" "
        "max=\"100\" value=\"%u\" required>"
        "<label for=\"h\">Hysteresis band (%%)</label>"
        "<input id=\"h\" name=\"mua_hysteresis\" type=\"number\" min=\"0\" "
        "max=\"20\" value=\"%u\" required>"
        "<label for=\"f\">Minimum fan speed floor (%%)</label>"
        "<input id=\"f\" name=\"fan_floor\" type=\"number\" min=\"0\" "
        "max=\"60\" value=\"%u\" required>"
        "<button>Save</button></form>",
        settings_mua_threshold_is_set()
            ? ""
            : "<p class=\"note\"><strong>Not yet calibrated</strong> — the "
              "values below are board defaults, not measurements.</p>",
        thr, hyst, floor);
    return send_page(req, body);
}

static esp_err_t calibration_post(httpd_req_t *req)
{
    if (!require_auth(req)) {
        return ESP_OK;
    }
    char body[256];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    }

    uint8_t thr, hyst, floor;
    if (!form_get_u8(body, "mua_threshold", 1, 100, &thr) ||
        !form_get_u8(body, "mua_hysteresis", 0, 20, &hyst) ||
        !form_get_u8(body, "fan_floor", 0, 60, &floor)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "values out of range");
    }

    settings_set_mua_threshold(thr);
    settings_set_mua_hysteresis(hyst);
    settings_set_fan_min_floor(floor);
    ESP_LOGI(TAG, "calibration saved: thr=%u hyst=%u floor=%u", thr, hyst,
             floor);
    if (s_cfg.on_calibration_changed) {
        s_cfg.on_calibration_changed();
    }
    return send_page(req,
        "<h1>Calibration</h1><p class=\"ok\">Saved.</p>"
        "<p><a href=\"/calibration\">Back to calibration</a></p>");
}

static esp_err_t hostname_get(httpd_req_t *req)
{
    char label[64] = "";
    settings_get_hostname_label(label, sizeof(label));

    char body[640];
    snprintf(body, sizeof(body),
        "<h1>Device name</h1>"
        "<p class=\"note\">Cosmetic label shown in this UI only. The "
        "network hostname is fixed and derived from the hardware address "
        "— it never changes and is what the NFC tag points to.</p>"
        "<form method=\"post\">"
        "<label for=\"l\">Label</label>"
        "<input id=\"l\" name=\"label\" maxlength=\"48\" value=\"%s\">"
        "<button>Save</button></form>",
        label);
    return send_page(req, body);
}

static esp_err_t hostname_post(httpd_req_t *req)
{
    if (!require_auth(req)) {
        return ESP_OK;
    }
    char body[160];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    }
    char label[64];
    if (!form_get(body, "label", label, sizeof(label))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing label");
    }
    settings_set_hostname_label(label);
    return send_page(req,
        "<h1>Device name</h1><p class=\"ok\">Saved.</p>");
}

static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1500)); /* let the HTTP response flush */
    esp_restart();
}

static esp_err_t wifi_get(httpd_req_t *req)
{
    char ssid[33] = "";
    char pass[65];
    settings_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass));

    char body[640];
    snprintf(body, sizeof(body),
        "<h1>Wi-Fi</h1>"
        "<p class=\"note\">Used only for this maintenance interface. "
        "Matter control runs over Thread and does not depend on it.</p>"
        "<form method=\"post\">"
        "<label for=\"s\">Network name (SSID)</label>"
        "<input id=\"s\" name=\"ssid\" maxlength=\"32\" value=\"%s\" required>"
        "<label for=\"p\">Password</label>"
        "<input id=\"p\" name=\"pass\" type=\"password\" maxlength=\"64\">"
        "<button>Save and reboot</button></form>",
        ssid);
    return send_page(req, body);
}

static esp_err_t wifi_post(httpd_req_t *req)
{
    if (!require_auth(req)) {
        return ESP_OK;
    }
    char body[256];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    }
    char ssid[33], pass[65];
    if (!form_get(body, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid");
    }
    if (!form_get(body, "pass", pass, sizeof(pass))) {
        pass[0] = '\0';
    }
    settings_set_wifi(ssid, pass);
    ESP_LOGI(TAG, "Wi-Fi credentials saved (ssid=\"%s\"), rebooting", ssid);
    esp_err_t err = send_page(req,
        "<h1>Wi-Fi</h1><p class=\"ok\">Saved — rebooting to join the "
        "network. Reconnect via <code>http://&lt;hostname&gt;.local/</code> "
        "(see the NFC tag).</p>");
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 1, NULL);
    return err;
}

/* Raw-binary upload (no multipart parsing needed): the page's inline script
 * POSTs the .bin file body directly and reports progress via XHR upload
 * events. Written to the inactive ota_0/ota_1 slot with esp_ota_*; the
 * running slot is left untouched until esp_ota_set_boot_partition succeeds,
 * so a bad upload never disturbs the working firmware. */

static const char UPDATE_SCRIPT[] =
    "<script>"
    "document.getElementById('f').addEventListener('submit',function(ev){"
    "ev.preventDefault();"
    "var file=document.getElementById('file').files[0];"
    "var status=document.getElementById('status');"
    "if(!file)return;"
    "var xhr=new XMLHttpRequest();"
    "xhr.open('POST','/update');"
    "xhr.setRequestHeader('Content-Type','application/octet-stream');"
    "xhr.upload.onprogress=function(e){"
    "if(e.lengthComputable){"
    "status.textContent='Uploading: '+Math.round(100*e.loaded/e.total)+'%';"
    "}};"
    "xhr.onload=function(){document.open();document.write(xhr.responseText);"
    "document.close();};"
    "xhr.onerror=function(){status.textContent='Upload failed — connection "
    "lost.';};"
    "status.textContent='Uploading…';"
    "xhr.send(file);"
    "});"
    "</script>";

static esp_err_t update_get(httpd_req_t *req)
{
    if (!require_auth(req)) {
        return ESP_OK;
    }
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);

    char body[1536];
    snprintf(body, sizeof(body),
        "<h1>Firmware update</h1>"
        "<p class=\"note\">Upload a firmware <code>.bin</code> built for "
        "this board (up to %lu KB). The current firmware stays in the "
        "other OTA slot — if the new image fails to boot, the device "
        "automatically rolls back to it.</p>"
        "<form id=\"f\">"
        "<label for=\"file\">Firmware image</label>"
        "<input id=\"file\" name=\"file\" type=\"file\" accept=\".bin\" "
        "required>"
        "<button type=\"submit\">Upload and reboot</button>"
        "</form>"
        "<p id=\"status\" class=\"note\" role=\"status\"></p>"
        "%s",
        target ? (unsigned long)(target->size / 1024) : 0UL, UPDATE_SCRIPT);
    return send_page(req, body);
}

#define OTA_CHUNK_SIZE 4096

static esp_err_t update_post(httpd_req_t *req)
{
    if (!require_auth(req)) {
        return ESP_OK;
    }
    if (req->content_len == 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
    }

    const esp_partition_t *update_partition =
        esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "no OTA partition available");
    }
    if (req->content_len > update_partition->size) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "image too large for the OTA partition");
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err =
        esp_ota_begin(update_partition, req->content_len, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "could not start OTA write");
    }

    static uint8_t buf[OTA_CHUNK_SIZE]; /* too large for the httpd stack */
    size_t received = 0;
    while (received < req->content_len) {
        size_t want = req->content_len - received;
        if (want > OTA_CHUNK_SIZE) {
            want = OTA_CHUNK_SIZE;
        }
        int r = httpd_req_recv(req, (char *)buf, want);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            esp_ota_abort(ota_handle);
            ESP_LOGE(TAG, "OTA upload interrupted after %u/%u bytes",
                     (unsigned)received, (unsigned)req->content_len);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                       "upload interrupted");
        }
        err = esp_ota_write(ota_handle, buf, (size_t)r);
        if (err != ESP_OK) {
            esp_ota_abort(ota_handle);
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            return send_page(req,
                "<h1>Firmware update</h1><p class=\"note\">Upload "
                "rejected — not a valid firmware image for this board. "
                "Nothing was flashed.</p>"
                "<p><a href=\"/update\">Try again</a></p>");
        }
        received += (size_t)r;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        return send_page(req,
            "<h1>Firmware update</h1><p class=\"note\">Image failed "
            "validation after upload — nothing was flashed. Re-download "
            "it and try again.</p>"
            "<p><a href=\"/update\">Try again</a></p>");
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s",
                 esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "could not select the new firmware");
    }

    ESP_LOGW(TAG, "OTA update written to \"%s\", rebooting",
             update_partition->label);
    esp_err_t resp_err = send_page(req,
        "<h1>Firmware update</h1><p class=\"ok\">Upload complete — "
        "rebooting into the new firmware.</p>"
        "<p class=\"note\">If it fails to come back up, the device "
        "automatically rolls back to the version you're on now.</p>");
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 1, NULL);
    return resp_err;
}

/* --- server ----------------------------------------------------------- */

esp_err_t webui_start(const webui_config_t *config)
{
    s_cfg = *config;

    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.max_uri_handlers = 16;
    http_cfg.stack_size = 8192; /* OTA write handler needs headroom */
    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &http_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %d", err);
        return err;
    }

    const httpd_uri_t routes[] = {
        { .uri = "/",            .method = HTTP_GET,  .handler = index_get },
        { .uri = "/calibration", .method = HTTP_GET,  .handler = calibration_get },
        { .uri = "/calibration", .method = HTTP_POST, .handler = calibration_post },
        { .uri = "/hostname",    .method = HTTP_GET,  .handler = hostname_get },
        { .uri = "/hostname",    .method = HTTP_POST, .handler = hostname_post },
        { .uri = "/wifi",        .method = HTTP_GET,  .handler = wifi_get },
        { .uri = "/wifi",        .method = HTTP_POST, .handler = wifi_post },
        { .uri = "/update",      .method = HTTP_GET,  .handler = update_get },
        { .uri = "/update",      .method = HTTP_POST, .handler = update_post },
        { .uri = "/login",       .method = HTTP_GET,  .handler = login_get },
        { .uri = "/login",       .method = HTTP_POST, .handler = login_post },
        { .uri = "/logout",      .method = HTTP_POST, .handler = logout_post },
        { .uri = "/password",    .method = HTTP_GET,  .handler = password_get },
        { .uri = "/password",    .method = HTTP_POST, .handler = password_post },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    ESP_LOGI(TAG, "web UI up on :80");
    return ESP_OK;
}
