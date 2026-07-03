#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "rmt_raw.h"

static const char *TAG = "RMT_DEMO";

#define RMT_GPIO_PIN  4

void app_main(void) {
    ESP_LOGI(TAG, "=== Инициализация bare-metal RMT драйвера ===");
    rmt_raw_init(RMT_GPIO_PIN, 1000000);  // 1 MHz → 1 тик = 1 µs

    while (1) {
        // ─── Демо 1: send_items (ручная упаковка символов) ───
        // Каждый символ = два полуцикла (dur0 + dur1)
        // На анализаторе: 20µs, 40µs, 100µs полные периоды
        ESP_LOGI(TAG, "--- Демо 1: send_items (10+10, 20+20, 50+50 µs) ---");
        rmt_item_t test_items[] = {
            {10, 1, 10, 0},
            {20, 1, 20, 0},
            {50, 1, 50, 0},
        };
        rmt_raw_send_items(test_items, 3);
        rmt_raw_wait();

        vTaskDelay(pdMS_TO_TICKS(50));

        // ─── Демо 2: send_pulses (длительности в µs) ───
        // 5 импульсов с чередующимся уровнем, начиная с HIGH
        // На анализаторе: 100µs↑ 200µs↓ 300µs↑ 150µs↓ 250µs↑
        ESP_LOGI(TAG, "--- Демо 2: send_pulses (100,200,300,150,250 µs) ---");
        uint32_t pulses[] = {100, 200, 300, 150, 250};
        rmt_raw_send_pulses(pulses, 5, 1);
        rmt_raw_wait();

        vTaskDelay(pdMS_TO_TICKS(50));

        // ─── Демо 3: square (меандр 10 кГц, 50%, 20 периодов) ───
        // На анализаторе: 20 периодов по 100µs (50µs HIGH + 50µs LOW)
        ESP_LOGI(TAG, "--- Демо 3: square (10kHz, 50%%, 20 периодов) ---");
        rmt_raw_square(10000, 50, 20);

        vTaskDelay(pdMS_TO_TICKS(50));

        // ─── Демо 4: square (1 кГц, 75%, 5 периодов) ───
        // На анализаторе: 5 периодов по 1ms (750µs HIGH + 250µs LOW)
        ESP_LOGI(TAG, "--- Демо 4: square (1kHz, 75%%, 5 периодов) ---");
        rmt_raw_square(1000, 75, 5);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}