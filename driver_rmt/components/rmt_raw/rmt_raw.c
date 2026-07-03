#include "rmt_raw.h"
#include "soc/rmt_reg.h"
#include "soc/gpio_sig_map.h"
#include "hal/gpio_hal.h" // Тільки для маршрутизації пінів (матриця)
#include "esp_rom_sys.h"  // Для затримок, якщо знадобляться
#include "esp_rom_gpio.h" // Для esp_rom_gpio_connect_out_signal та pad_select
#include "driver/gpio.h"  // Для gpio_set_direction
#include "esp_log.h"

static const char *TAG = "RMT_RAW";

// Зберігаємо роздільну здатність, встановлену в init(), щоб перераховувати µs → тіки
static uint32_t s_resolution_hz = 1000000;

// ========================================================
// Про макроси REG_WRITE та REG_READ (Memory-Mapped I/O)
// ========================================================
// В ESP-IDF ці макроси вже вбудовані у <soc/soc.h> (він підключається через rmt_reg.h).
// Під капотом вони виглядають саме так — пряме звернення до адрес пам'яті через volatile вказівник:
// #define REG_WRITE(addr, val) (*(volatile uint32_t *)(addr) = (val))
// #define REG_READ(addr)       (*(volatile uint32_t *)(addr))

#include "soc/system_reg.h"

void rmt_raw_init(int gpio, uint32_t resolution_hz) {
    // ========================================================
    // КРОК 0: Увімкнення тактування периферії на рівні системи
    // ========================================================
    // Без цього всі записи в регістри RMT будуть проігноровані!
    ESP_LOGI(TAG, "КРОК 0: Увімкнення тактування RMT...");
    REG_WRITE(SYSTEM_PERIP_CLK_EN0_REG, REG_READ(SYSTEM_PERIP_CLK_EN0_REG) | SYSTEM_RMT_CLK_EN);
    REG_WRITE(SYSTEM_PERIP_RST_EN0_REG, REG_READ(SYSTEM_PERIP_RST_EN0_REG) & ~SYSTEM_RMT_RST);
    ESP_LOGI(TAG, "  Тактування увімкнено, скидання знято");

    // ========================================================
    // КРОК 1: Маршрутизація (Підключити вихід RMT до ніжки МК)
    // ========================================================
    ESP_LOGI(TAG, "КРОК 1: GPIO routing (pin %d → RMT CH0 TX)...", gpio);
    esp_rom_gpio_pad_select_gpio(gpio);
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    esp_rom_gpio_connect_out_signal(gpio, RMT_SIG_OUT0_IDX, false, false);
    ESP_LOGI(TAG, "  GPIO %d підключено", gpio);

    // ========================================================
    // КРОК 2: Увімкнути тактування та налаштувати SYS_CONF_REG
    // ========================================================
    // Налаштування глобальної конфігурації модуля RMT (RMT_SYS_CONF_REG).
    // Виконується увімкнення тактування регістрів, вибір джерела частоти (APB) 
    // та налаштування режиму роботи з пам'яттю (FIFO-режим).
    ESP_LOGI(TAG, "КРОК 2: Налаштування RMT_SYS_CONF_REG...");
    uint32_t sys_conf = REG_READ(RMT_SYS_CONF_REG);
    ESP_LOGI(TAG, "SYS_CONF до: 0x%08lX", (unsigned long)sys_conf);

    // Вмикаємо тактування (RMT_CLK_EN, біт 31)
    // Аналог: sys_conf |= RMT_SCLK_EN_M;
    sys_conf |= (1 << 31);

    // Очищаємо поле вибору частоти (RMT_SCLK_SEL, біти 25:24)
    // 3 у бінарному вигляді це 11. Зсуваємо на 24 та інвертуємо.
    // Аналог: sys_conf &= ~RMT_SCLK_SEL_M;
    sys_conf &= ~(3 << 24);

   
    // Вибираємо джерело APB (значення 0x1, пишемо у 24-й біт)
    // Аналог: sys_conf |= (1 << RMT_SCLK_SEL_S);
    sys_conf |= (1 << 24);
   
    // ⚠️ APB_FIFO_MASK (біт 0) — ЗАЛИШАЄМО В 0!
    // Значення 0 = режим FIFO: запис у RMT_CH0DATA_REG йде через FIFO 
    // з автоінкрементом вказівника запису.
    // Значення 1 = прямий доступ: FIFO обходиться, потрібно писати в RAM-адреси
    // безпосередньо (RMTMEM.chan[ch].data32[i]), а НЕ через CH0DATA_REG.
    // Наш send_items() пише у CH0DATA_REG → потрібен FIFO (біт 0 = 0).
    sys_conf &= ~(1 << 0);

    // Застосовуємо налаштування
    REG_WRITE(RMT_SYS_CONF_REG, sys_conf);
    ESP_LOGI(TAG, "  SYS_CONF після: 0x%08lX", (unsigned long)sys_conf);


    // ========================================================
    // КРОК 3: Налаштування каналу 0 (RMT_CH0CONF0_REG)
    // ========================================================
    // Налаштування параметрів передачі нульового каналу (RMT_CH0CONF0_REG).
    // Встановлюється переддільник частоти (DIV_CNT) для досягнення заданого resolution_hz, 
    // виділяється необхідний об'єм пам'яті (MEM_SIZE) та виконується скидання вказівників FIFO.

    ESP_LOGI(TAG, "КРОК 3: Налаштування CH0CONF0 (дільник, пам'ять)...");
    uint32_t ch0conf0 = REG_READ(RMT_CH0CONF0_REG);
    ESP_LOGI(TAG, "  CH0CONF0 до: 0x%08lX", (unsigned long)ch0conf0);

    uint32_t div = 80000000 / resolution_hz;
    ESP_LOGI(TAG, "  Дільник: %lu (80MHz / %lu Hz)", (unsigned long)div, (unsigned long)resolution_hz);
    
    // Очищаємо поле дільника (RMT_DIV_CNT_CH0, біти 15:8)
    // Аналог: ch0conf0 &= ~RMT_DIV_CNT_CH0_M;
    ch0conf0 &= ~(0xFF << 8); // Скидаємо біти 8-15 (маска 0xFF - це 8 одиниць)
    
    // Записуємо наш дільник
    // Аналог: ch0conf0 |= (div << RMT_DIV_CNT_CH0_S);
    ch0conf0 |= (div << 8);   
    
    // Очищаємо поле розміру пам'яті (RMT_MEM_SIZE_CH0, біти 19:16)
    // Аналог: ch0conf0 &= ~RMT_MEM_SIZE_CH0_M;
    ch0conf0 &= ~(0xF << 16); // Скидаємо біти 16-19 (маска 0xF - це 4 одиниці)
    
    // Записуємо розмір пам'яті = 1 (один блок)
    // Аналог: ch0conf0 |= (1 << RMT_MEM_SIZE_CH0_S);
    ch0conf0 |= (1 << 16);    

    // Вмикаємо рівень спокою (RMT_IDLE_OUT_EN_CH0, біт 6)
    // Аналог: ch0conf0 |= (1 << RMT_IDLE_OUT_EN_CH0_S);
    ch0conf0 |= (1 << 6);  
    
    // Встановлюємо рівень спокою в 0 (RMT_IDLE_OUT_LV_CH0, біт 5)
    // Аналог: ch0conf0 &= ~(1 << RMT_IDLE_OUT_LV_CH0_S);
    ch0conf0 &= ~(1 << 5); // Ставимо 0 в IDLE_OUT_LV. Очищення І-НЕ ставить нуль.

    // Вимикаємо безперервний режим (RMT_TX_CONTI_MODE_CH0, біт 3)
    // Аналог: ch0conf0 &= ~(1 << RMT_TX_CONTI_MODE_CH0_S);
    ch0conf0 &= ~(1 << 3); 
    
    // Вимикаємо режим Wrap (RMT_MEM_TX_WRAP_EN_CH0, біт 4)
    // Аналог: ch0conf0 &= ~(1 << RMT_MEM_TX_WRAP_EN_CH0_S);
    ch0conf0 &= ~(1 << 4); 

    // ⚠️ Вимикаємо несучу (carrier modulation)!
    // За замовчуванням CH0CONF0 = 0x00710200, біти 20-22 = 1 (carrier увімкнена!).
    // Це потрібно для ІЧ-пультів (модуляція 38 кГц), але для звичайних імпульсів — заважає.
    // Якщо забути — замість чистих 10/20/50 µs імпульсів побачиш ~1.6 µs несучу.
    // Аналог: ch0conf0 &= ~(RMT_CARRIER_EN_CH0_M | RMT_CARRIER_EFF_EN_CH0_M | RMT_CARRIER_OUT_LV_CH0_M);
    ch0conf0 &= ~(1 << 21);  // RMT_CARRIER_EN_CH0 — головний вимикач несучої
    ch0conf0 &= ~(1 << 20);  // RMT_CARRIER_EFF_EN_CH0 — несуча тільки під час передачі даних
    ch0conf0 &= ~(1 << 22);  // RMT_CARRIER_OUT_LV_CH0 — полярність несучої


    // Скидання пам'яті каналу (RMT_APB_MEM_RST_CH0, біт 2)
    // Спочатку тимчасово ставимо 1 і пишемо в регістр
    // Аналог: REG_WRITE(RMT_CH0CONF0_REG, ch0conf0 | (1 << RMT_APB_MEM_RST_CH0_S));
    REG_WRITE(RMT_CH0CONF0_REG, ch0conf0 | (1 << 2)); 
    
    // Потім знімаємо 1 (скидаємо тригер)
    // Аналог: ch0conf0 &= ~(1 << RMT_APB_MEM_RST_CH0_S); REG_WRITE(RMT_CH0CONF0_REG, ch0conf0);
    REG_WRITE(RMT_CH0CONF0_REG, ch0conf0 &= ~(1 << 2));


    // ========================================================
    // КРОК 4: НАЙГОЛОВНІШЕ - Зафіксувати налаштування!
    // ========================================================
    // Встановити біт RMT_CONF_UPDATE у RMT_CH0CONF0_REG.

    // Встановлюємо біт фіксації (RMT_CONF_UPDATE_CH0, біт 24)
    // Аналог: ch0conf0 |= (1 << RMT_CONF_UPDATE_CH0_S);
    ch0conf0 |= (1 << 24); 
    
    // Фінальний запис конфігурації у залізо
    REG_WRITE(RMT_CH0CONF0_REG, ch0conf0); 

    ESP_LOGI(TAG, "  CH0CONF0 після: 0x%08lX", (unsigned long)ch0conf0);
    ESP_LOGI(TAG, "КРОК 4: Защіпка записана. Ініціалізація завершена!");

    // Запам'ятовуємо роздільну здатність для допоміжних функцій (µs → тіки)
    s_resolution_hz = resolution_hz;
}

void rmt_raw_send_items(const rmt_item_t *items, size_t count) {
    ESP_LOGD(TAG, "send_items: count=%d", (int)count);

    uint32_t ch0conf0 = REG_READ(RMT_CH0CONF0_REG);

   // ========================================================
    // КРОК 1: Скидання вказівника запису (щоб писати з нульової адреси)
    // ========================================================
    
    // Вмикаємо скидання пам'яті (RMT_APB_MEM_RST_CH0, біт 2) 
    // + обов'язково ставимо біт фіксації (RMT_CONF_UPDATE_CH0, біт 24)
    // Аналог: REG_WRITE(RMT_CH0CONF0_REG, ch0conf0 | (1 << RMT_APB_MEM_RST_CH0_S) | (1 << RMT_CONF_UPDATE_CH0_S));
    REG_WRITE(RMT_CH0CONF0_REG, ch0conf0 | (1 << 2) | (1 << 24));
    
    // Вимикаємо скидання пам'яті (знімаємо біт 2), щоб дозволити запис
    // + знову ставимо фіксацію (RMT_CONF_UPDATE_CH0, біт 24)
    // Аналог: REG_WRITE(RMT_CH0CONF0_REG, (ch0conf0 & ~(1 << RMT_APB_MEM_RST_CH0_S)) | (1 << RMT_CONF_UPDATE_CH0_S));
    REG_WRITE(RMT_CH0CONF0_REG, (ch0conf0 & ~(1 << 2)) | (1 << 24));


    // ========================================================
    // КРОК 2: Пакування та запис даних у FIFO (пам'ять RMT)
    // ========================================================
    for (size_t i = 0; i < count; i++) {
        // Пакуємо структуру у 32-бітне слово згідно з TRM
        uint32_t word = (items[i].dur0 & 0x7FFF) | (items[i].lvl0 << 15) | 
                        ((items[i].dur1 & 0x7FFF) << 16) | (items[i].lvl1 << 31);
        
        // Пишемо слово в регістр даних (RMT_CH0DATA_REG). Залізо самостійно зсуне Write Pointer на +1
        REG_WRITE(RMT_CH0DATA_REG, word);
    }


    // ========================================================
    // КРОК 3: Обов'язковий нуль-маркер
    // ========================================================
    // Якщо duration0 = 0, RMT розуміє, що це кінець передачі
    // Записуємо нуль у регістр даних (RMT_CH0DATA_REG)
    REG_WRITE(RMT_CH0DATA_REG, 0);

    // ========================================================
    // КРОК 4: Скидання вказівника читання + СТАРТ передачі
    // ========================================================
    
    // Перечитуємо регістр конфігурації про всяк випадок
    ch0conf0 = REG_READ(RMT_CH0CONF0_REG);

    // Вмикаємо скидання вказівника читання (RMT_MEM_RD_RST_CH0, біт 1) 
    // + фіксація (RMT_CONF_UPDATE_CH0, біт 24)
    // Аналог: REG_WRITE(RMT_CH0CONF0_REG, ch0conf0 | (1 << RMT_MEM_RD_RST_CH0_S) | (1 << RMT_CONF_UPDATE_CH0_S));
    REG_WRITE(RMT_CH0CONF0_REG, ch0conf0 | (1 << 1) | (1 << 24));
    
    // Знімаємо скидання вказівника читання (скидаємо біт 1)
    // Аналог: ch0conf0 &= ~(1 << RMT_MEM_RD_RST_CH0_S);
    ch0conf0 &= ~(1 << 1);

    // Скидаємо прапорець старту, якщо він залишився від попередньої передачі
    ch0conf0 &= ~(1 << 0);
    REG_WRITE(RMT_CH0CONF0_REG, ch0conf0 | (1 << 24));
    
    // Встановлюємо біт старту передачі (RMT_TX_START_CH0, біт 0)
    // Аналог: ch0conf0 |= (1 << RMT_TX_START_CH0_S);
    ch0conf0 |= (1 << 0);
    
    // Записуємо фінальну конфігурацію зі знятим скиданням, прапорцем старту та ФІКСАЦІЄЮ (RMT_CONF_UPDATE_CH0, біт 24)
    // Аналог: REG_WRITE(RMT_CH0CONF0_REG, ch0conf0 | (1 << RMT_CONF_UPDATE_CH0_S));
    REG_WRITE(RMT_CH0CONF0_REG, ch0conf0 | (1 << 24));
}

void rmt_raw_wait(void) {
    // ========================================================
    // КРОК 1: Опитування (Polling) регістра сирих переривань
    // ========================================================
    // Читаємо RMT_INT_RAW_REG, накладаємо маску на нульовий біт (CH0_TX_END_INT_RAW).
    // Поки результат дорівнює 0 — передача ще триває, крутимось у порожньому циклі.
    while ((REG_READ(RMT_INT_RAW_REG) & (1 << 0)) == 0) {
        // Блокуюче очікування. 
        // Примітка: У production-коді тут використовується FreeRTOS переривання, 
        // щоб віддати процесорний час іншим задачам, поки RMT працює.
        // Але для розуміння роботи з залізом полінг — ідеальний старт.
    }
    ESP_LOGD(TAG, "TX_END отримано, очищаємо прапорець");

    // ========================================================
    // КРОК 2: Очищення прапорця (Acknowledge)
    // ========================================================
    // В RMT_INT_CLR_REG запис 1 у нульовий біт очищає CH0_TX_END_INT_RAW.
    // Запис 0 ігнорується. 
    // Аналог: REG_WRITE(RMT_INT_CLR_REG, (1 << RMT_CH0_TX_END_INT_CLR_S));
    REG_WRITE(RMT_INT_CLR_REG, (1 << 0));
}

// ========================================================
// rmt_raw_send_pulses — зручна обгортка
// ========================================================
// Приймає масив тривалостей у МІКРОСЕКУНДАХ та стартовий рівень.
// Рівень чергується автоматично: start_level → !start_level → start_level → ...
//
// Приклад: durations_us = {100, 200, 300}, start_level = 1
//   → 100µs HIGH, 200µs LOW, 300µs HIGH
//
// Всередині пакує пари тривалостей у rmt_item_t та викликає send_items.
// Якщо count непарний — останній символ має dur1=0 (нуль-маркер вбудований).
void rmt_raw_send_pulses(const uint32_t *durations_us, size_t count, int start_level) {
    if (count == 0) return;

    // Коефіцієнт перерахунку: µs → тіки
    // При resolution = 1 MHz → 1 тік = 1 µs → коефіцієнт = 1
    // При resolution = 2 MHz → 1 тік = 0.5 µs → коефіцієнт = 2
    uint32_t ticks_per_us = s_resolution_hz / 1000000;
    if (ticks_per_us == 0) ticks_per_us = 1;

    // Кожен rmt_item_t зберігає ДВА напівцикли, тому кількість символів = (count+1)/2
    size_t item_count = (count + 1) / 2;
    rmt_item_t items[item_count];

    int level = start_level ? 1 : 0;

    for (size_t i = 0; i < item_count; i++) {
        size_t idx = i * 2;

        // Перший напівцикл (dur0, lvl0)
        items[i].dur0 = (uint16_t)(durations_us[idx] * ticks_per_us);
        items[i].lvl0 = level;
        level = !level;

        // Другий напівцикл (dur1, lvl1) — або 0, якщо count непарний і це останній
        if (idx + 1 < count) {
            items[i].dur1 = (uint16_t)(durations_us[idx + 1] * ticks_per_us);
            items[i].lvl1 = level;
            level = !level;
        } else {
            items[i].dur1 = 0;
            items[i].lvl1 = 0;
        }
    }

    rmt_raw_send_items(items, item_count);
}

// ========================================================
// rmt_raw_square — генерація меандру
// ========================================================
// freq_hz  — частота меандру в Гц
// duty_pct — шпаруватість у % (0..100): частка HIGH-рівня в одному періоді
// periods  — кількість повних періодів
//
// Приклад: freq_hz=1000, duty_pct=50, periods=10
//   → 10 періодів по 1 мс (500µs HIGH + 500µs LOW)
//
// Формули:
//   period_ticks = resolution_hz / freq_hz
//   high_ticks   = period_ticks * duty_pct / 100
//   low_ticks    = period_ticks - high_ticks
void rmt_raw_square(uint32_t freq_hz, uint8_t duty_pct, uint32_t periods) {
    if (freq_hz == 0 || periods == 0) return;
    if (duty_pct > 100) duty_pct = 100;

    // Один період меандру в тіках RMT
    uint32_t period_ticks = s_resolution_hz / freq_hz;

    // Розділяємо період на HIGH та LOW за шпаруватістю
    uint32_t high_ticks = period_ticks * duty_pct / 100;
    uint32_t low_ticks  = period_ticks - high_ticks;

    // Кожен rmt_item_t = один повний період меандру (HIGH + LOW)
    // Обмежуємо за розміром RAM блоку RMT (48 символів для CH0)
    size_t batch_size = 48 - 1; // -1 для нуль-маркера
    rmt_item_t items[batch_size];

    uint32_t sent = 0;
    while (sent < periods) {
        size_t to_send = periods - sent;
        if (to_send > batch_size) to_send = batch_size;

        for (size_t i = 0; i < to_send; i++) {
            items[i].dur0 = (uint16_t)high_ticks;
            items[i].lvl0 = 1;
            items[i].dur1 = (uint16_t)low_ticks;
            items[i].lvl1 = 0;
        }

        rmt_raw_send_items(items, to_send);
        rmt_raw_wait();
        sent += to_send;
    }
}
