#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_console.h"

static const char *TAG = "UART_ANALYZER";

#define LAB_UART    UART_NUM_1
#define LAB_TX_PIN  17
#define LAB_RX_PIN  18
#define BUF_SIZE    512

static int s_baud = 115200;

static void uart_reconfig(int baud)
{
    const uart_config_t cfg = {
        .baud_rate  = baud,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(LAB_UART, &cfg);
    s_baud = baud;
}

static int cmd_send(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: send <text>\n");
        return 1;
    }
    int total = 0;
    for (int i = 1; i < argc; i++) {
        total += uart_write_bytes(LAB_UART, argv[i], strlen(argv[i]));
        if (i < argc - 1) {
            uart_write_bytes(LAB_UART, " ", 1);
            total += 1;
        }
    }
    printf("TX %d bytes @ %d baud -> watch GPIO%d\n", total, s_baud, LAB_TX_PIN);
    return 0;
}

static int cmd_byte(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: byte <0xNN>\n");
        return 1;
    }
    uint8_t b = (uint8_t)strtol(argv[1], NULL, 0);
    uart_write_bytes(LAB_UART, &b, 1);
    printf("TX 0x%02X (0b", b);
    for (int i = 7; i >= 0; i--) printf("%d", (b >> i) & 1);
    printf(") -> one frame on GPIO%d\n", LAB_TX_PIN);
    return 0;
}

static int cmd_setbaud(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: setbaud <rate>\n");
        return 1;
    }
    int baud = atoi(argv[1]);
    uart_reconfig(baud);
    printf("UART1 now @ %d baud (analyzer still set to old rate?)\n", baud);
    return 0;
}

static void register_cmds(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "send",    .help = "send text over UART1",     .func = cmd_send },
        { .command = "byte",    .help = "send one byte, e.g. byte 0x55", .func = cmd_byte },
        { .command = "setbaud", .help = "change UART1 baud rate",    .func = cmd_setbaud },
    };
    for (int i = 0; i < 3; i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
}

void app_main(void)
{
    uart_reconfig(115200);
    ESP_ERROR_CHECK(uart_set_pin(LAB_UART, LAB_TX_PIN, LAB_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(LAB_UART, BUF_SIZE, 0, 0, NULL, 0));

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "cmd>";

    esp_console_dev_usb_serial_jtag_config_t dev_cfg =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&dev_cfg, &repl_cfg, &repl));
    esp_console_register_help_command();
    register_cmds();

    ESP_LOGI(TAG, "type a command; each one becomes frames on GPIO%d", LAB_TX_PIN);
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}