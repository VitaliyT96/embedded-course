#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "rmt_raw.h"

static const char *TAG = "SW_UART_RMT";

// Налаштування для Software UART
#define SW_UART_TX_PIN   17      // Пін для передачі даних
#define SW_UART_BAUD     9600    // Швидкість передачі (бод)
#define RMT_RESOLUTION   1000000 // Роздільна здатність RMT (1 МГц = 1 тік/мкс)

// Функція для передачі одного байта через RMT (Software UART TX)
static void sw_uart_tx_byte(uint8_t data) {
    /*
     * Пакує 1 байт (1 Start bit + 8 Data bits + 1 Stop bit) 
     * у масив структур rmt_item_t та відправляє його через RMT.
     */
    // RMT кадр складається з 5 елементів (оскільки кожен елемент зберігає по 2 біти, 5 * 2 = 10 біт).
    rmt_item_t frame[5];
    
    // Розрахунок тривалості одного біта (в мікросекундах).
    // RMT_RESOLUTION = 1 МГц (1 тік = 1 мкс). Для 9600 бод тривалість = 1000000 / 9600 = 104 мкс.
    uint16_t bit_dur = RMT_RESOLUTION / SW_UART_BAUD; 

    // Цикл пакування 10 біт (1 Start, 8 Data, 1 Stop) у 5 елементів rmt_item_t
    for (uint8_t i = 0; i < 5; i++) {
        // Кожен rmt_item_t має два слоти для станів (lvl0 та lvl1).
        // Розраховуємо глобальний індекс біта у UART фреймі (від 0 до 9)
        uint8_t idx0 = i * 2;       // Індекс для першого слота (lvl0)
        uint8_t idx1 = i * 2 + 1;   // Індекс для другого слота (lvl1)

        // Сетимо однакову тривалість (104 мкс) для обох слотів
        frame[i].dur0 = bit_dur;
        frame[i].dur1 = bit_dur;

        /*
         * 1. Логіка для парних бітів (lvl0), що залежить тільки від idx0
         * - Якщо idx0 == 0: це завжди Start bit (логічний 0).
         * - Якщо 1 <= idx0 <= 8: це біти даних. Ми дістаємо потрібний біт 
         *   за допомогою побітового зсуву: (data >> (idx0 - 1)) & 1.
         */
        if (idx0 == 0) {
            frame[i].lvl0 = 0; // Start bit
        } else if (idx0 <= 8) {
            frame[i].lvl0 = (data >> (idx0 - 1)) & 1; // Data bits
        }

        /*
         * 2. Логіка для непарних бітів (lvl1), що залежить тільки від idx1
         * - Якщо 1 <= idx1 <= 8: це біти даних (аналогічно як вище).
         * - Якщо idx1 == 9: це завжди Stop bit (логічна 1).
         *
         * Чому два окремих блоки if?
         * Це запобігає конфліктам при призначенні lvl0 і lvl1 в межах однієї ітерації. 
         * Раніше спроба об'єднати їх в один if-else ланцюг призводила до того, 
         * що lvl1 міг не призначатися, якщо спрацьовувала умова для lvl0.
         */
        if (idx1 <= 8) {
            frame[i].lvl1 = (data >> (idx1 - 1)) & 1; // Data bits
        } else if (idx1 == 9) {
            frame[i].lvl1 = 1; // Stop bit
        }
    }

    rmt_raw_send_items(frame, 5);
    rmt_raw_wait();
    ESP_LOGI(TAG, "Відправка байта: 0x%02X", data);
}

// Функція для передачі рядка
static void sw_uart_tx_string(const char *str) {
    while (*str) {
        sw_uart_tx_byte((uint8_t)*str);
        str++;
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "=== Software UART over RMT ===");

    // 1. Ініціалізуємо RMT. idle рівень для UART має бути HIGH.
    // У драйвері rmt_raw рівень за замовчуванням змінено на 1 (HIGH) спеціально для UART.
    rmt_raw_init(SW_UART_TX_PIN, RMT_RESOLUTION);

    while (1) {
        ESP_LOGI(TAG, "-----------------------------------");
        
        // Тест: 0x55 (в двійковому 01010101). 
        ESP_LOGI(TAG, "Тест: 0x55 (чергування бітів)");
        sw_uart_tx_byte(0x55);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
