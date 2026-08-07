#include "dimmer_bridge.h"

#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BRIDGE_UART_NUM       UART_NUM_1
#define BRIDGE_BAUD           115200
#define BRIDGE_BUF_SIZE       256
#define BRIDGE_ACK_TIMEOUT_MS 20

static const char *TAG = "dimmer_bridge";

esp_err_t dimmer_bridge_init(int tx_gpio, int rx_gpio)
{
    const uart_config_t cfg = {
        .baud_rate = BRIDGE_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(BRIDGE_UART_NUM, BRIDGE_BUF_SIZE, 0, 0,
                                        NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(BRIDGE_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(BRIDGE_UART_NUM, tx_gpio, rx_gpio,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "bridge UART up (tx=%d rx=%d)", tx_gpio, rx_gpio);
    return ESP_OK;
}

esp_err_t dimmer_bridge_set(dimmer_channel_t ch, uint8_t power_percent)
{
    if (power_percent > 100) {
        power_percent = 100;
    }
    char cmd[24];
    int n = snprintf(cmd, sizeof(cmd), "SET %s %u\n",
                     ch == DIMMER_CH_FAN ? "FAN" : "LIGHT", power_percent);
    int written = uart_write_bytes(BRIDGE_UART_NUM, cmd, n);
    if (written != n) {
        ESP_LOGW(TAG, "short write to bridge (%d/%d)", written, n);
        return ESP_FAIL;
    }

    /* Reference bridge replies "OK\n" per command. A missing/garbled ACK is
     * logged but non-fatal: the wall switch must keep working even if the
     * bridge firmware doesn't ACK (e.g. a fork using a one-way protocol).
     * TODO(bring-up): verify against the concrete bridge (DimmerLink or
     * equivalent) and tighten to its real protocol. */
    uint8_t resp[8] = { 0 };
    int r = uart_read_bytes(BRIDGE_UART_NUM, resp, sizeof(resp) - 1,
                            pdMS_TO_TICKS(BRIDGE_ACK_TIMEOUT_MS));
    if (r < 2 || strncmp((const char *)resp, "OK", 2) != 0) {
        ESP_LOGW(TAG, "no ACK for '%.*s' (got %d bytes)", n - 1, cmd, r);
    }
    return ESP_OK;
}
