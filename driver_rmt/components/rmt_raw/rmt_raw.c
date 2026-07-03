#include "rmt_raw.h"
#include "soc/rmt_reg.h"
#include "soc/gpio_sig_map.h"
#include "hal/gpio_hal.h" // Только для маршрутизации пинов (матрица)
#include "esp_rom_sys.h"  // Для задержек, если понадобятся
#include "esp_rom_gpio.h" // Для esp_rom_gpio_connect_out_signal и pad_select
#include "driver/gpio.h"  // Для gpio_set_direction
#include "esp_log.h"

static const char *TAG = "RMT_RAW";

// Сохраняем разрешение, установленное в init(), чтобы пересчитывать µs → тики
static uint32_t s_resolution_hz = 1000000;

// ========================================================
// О макросах REG_WRITE и REG_READ (Memory-Mapped I/O)
// ========================================================
// В ESP-IDF эти макросы уже встроены в <soc/soc.h> (он подключается через rmt_reg.h).
// Под капотом они выглядят именно так — прямое обращение к адресам памяти через volatile указатель:
// #define REG_WRITE(addr, val) (*(volatile uint32_t *)(addr) = (val))
// #define REG_READ(addr)       (*(volatile uint32_t *)(addr))

#include "soc/system_reg.h"

void rmt_raw_init(int gpio, uint32_t resolution_hz) {
    // ========================================================
    // ШАГ 0: Включение тактирования периферии на уровне системы
    // ========================================================
    // Без этого все записи в регистры RMT будут проигнорированы!
    ESP_LOGI(TAG, "ШАГ 0: Включение тактирования RMT...");
    REG_WRITE(SYSTEM_PERIP_CLK_EN0_REG, REG_READ(SYSTEM_PERIP_CLK_EN0_REG) | SYSTEM_RMT_CLK_EN);
    REG_WRITE(SYSTEM_PERIP_RST_EN0_REG, REG_READ(SYSTEM_PERIP_RST_EN0_REG) & ~SYSTEM_RMT_RST);
    ESP_LOGI(TAG, "  Тактирование включено, сброс снят");

    // ========================================================
    // ШАГ 1: Маршрутизация (Подключить выход RMT к ножке МК)
    // ========================================================
    ESP_LOGI(TAG, "ШАГ 1: GPIO routing (pin %d → RMT CH0 TX)...", gpio);
    esp_rom_gpio_pad_select_gpio(gpio);
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    esp_rom_gpio_connect_out_signal(gpio, RMT_SIG_OUT0_IDX, false, false);
    ESP_LOGI(TAG, "  GPIO %d подключен", gpio);

    // ========================================================
    // ШАГ 2: Включить тактирование и настроить SYS_CONF_REG
    // ========================================================
    // Настройка глобальной конфигурации модуля RMT (RMT_SYS_CONF_REG).
    // Производится включение тактирования регистров, выбор источника частоты (APB) 
    // и настройка режима работы с памятью (FIFO-режим).
    ESP_LOGI(TAG, "ШАГ 2: Настройка RMT_SYS_CONF_REG...");
    uint32_t sys_conf = REG_READ(RMT_SYS_CONF_REG);
    ESP_LOGI(TAG, "SYS_CONF до: 0x%08lX", (unsigned long)sys_conf);

    // Включаем тактирование (RMT_CLK_EN, бит 31)
    // Аналог: sys_conf |= RMT_SCLK_EN_M;
    sys_conf |= (1 << 31);

    // Очищаем поле выбора частоты (RMT_SCLK_SEL, биты 25:24)
    // 3 в бинарном виде это 11. Сдвигаем на 24 и инвертируем.
    // Аналог: sys_conf &= ~RMT_SCLK_SEL_M;
    sys_conf &= ~(3 << 24);

   
    // Выбираем источник APB (значение 0x1, пишем в 24-й бит)
    // Аналог: sys_conf |= (1 << RMT_SCLK_SEL_S);
    sys_conf |= (1 << 24);
   
    // ⚠️ APB_FIFO_MASK (бит 0) — ОСТАВЛЯЕМ В 0!
    // Значение 0 = режим FIFO: запись в RMT_CH0DATA_REG идёт через FIFO 
    // с автоинкрементом указателя записи.
    // Значение 1 = прямой доступ: FIFO обходится, нужно писать в RAM-адреса
    // напрямую (RMTMEM.chan[ch].data32[i]), а НЕ через CH0DATA_REG.
    // Наш send_items() пишет в CH0DATA_REG → нужен FIFO (бит 0 = 0).
    sys_conf &= ~(1 << 0);

    // Применяем настройки
    REG_WRITE(RMT_SYS_CONF_REG, sys_conf);
    ESP_LOGI(TAG, "  SYS_CONF после: 0x%08lX", (unsigned long)sys_conf);


    // ========================================================
    // ШАГ 3: Настройка канала 0 (RMT_CH0CONF0_REG)
    // ========================================================
    // Настройка параметров передачи нулевого канала (RMT_CH0CONF0_REG).
    // Устанавливается предделитель частоты (DIV_CNT) для достижения заданного resolution_hz, 
    // выделяется необходимый объем памяти (MEM_SIZE) и выполняется сброс указателей FIFO.

    ESP_LOGI(TAG, "ШАГ 3: Настройка CH0CONF0 (делитель, память)...");
    uint32_t ch0conf0 = REG_READ(RMT_CH0CONF0_REG);
    ESP_LOGI(TAG, "  CH0CONF0 до: 0x%08lX", (unsigned long)ch0conf0);

    uint32_t div = 80000000 / resolution_hz;
    ESP_LOGI(TAG, "  Делитель: %lu (80MHz / %lu Hz)", (unsigned long)div, (unsigned long)resolution_hz);
    
    // Очищаем поле делителя (RMT_DIV_CNT_CH0, биты 15:8)
    // Аналог: ch0conf0 &= ~RMT_DIV_CNT_CH0_M;
    ch0conf0 &= ~(0xFF << 8); // Сбрасываем биты 8-15 (маска 0xFF - это 8 единиц)
    
    // Записываем наш делитель
    // Аналог: ch0conf0 |= (div << RMT_DIV_CNT_CH0_S);
    ch0conf0 |= (div << 8);   
    
    // Очищаем поле размера памяти (RMT_MEM_SIZE_CH0, биты 19:16)
    // Аналог: ch0conf0 &= ~RMT_MEM_SIZE_CH0_M;
    ch0conf0 &= ~(0xF << 16); // Сбрасываем биты 16-19 (маска 0xF - это 4 единицы)
    
    // Записываем размер памяти = 1 (один блок)
    // Аналог: ch0conf0 |= (1 << RMT_MEM_SIZE_CH0_S);
    ch0conf0 |= (1 << 16);    

    // Включаем уровень покоя (RMT_IDLE_OUT_EN_CH0, бит 6)
    // Аналог: ch0conf0 |= (1 << RMT_IDLE_OUT_EN_CH0_S);
    ch0conf0 |= (1 << 6);  
    
    // Устанавливаем уровень покоя в 0 (RMT_IDLE_OUT_LV_CH0, бит 5)
    // Аналог: ch0conf0 &= ~(1 << RMT_IDLE_OUT_LV_CH0_S);
    ch0conf0 &= ~(1 << 5); // Ставим 0 в IDLE_OUT_LV. Очистка И-НЕ ставит ноль.

    // Выключаем непрерывный режим (RMT_TX_CONTI_MODE_CH0, бит 3)
    // Аналог: ch0conf0 &= ~(1 << RMT_TX_CONTI_MODE_CH0_S);
    ch0conf0 &= ~(1 << 3); 
    
    // Выключаем режим Wrap (RMT_MEM_TX_WRAP_EN_CH0, бит 4)
    // Аналог: ch0conf0 &= ~(1 << RMT_MEM_TX_WRAP_EN_CH0_S);
    ch0conf0 &= ~(1 << 4); 

    // ⚠️ Выключаем несущую (carrier modulation)!
    // В дефолте CH0CONF0 = 0x00710200, биты 20-22 = 1 (carrier включена!).
    // Это нужно для ИК-пультов (модуляция 38 кГц), но для обычных импульсов — мешает.
    // Если забыть — вместо чистых 10/20/50 µs импульсов увидишь ~1.6 µs несущую.
    // Аналог: ch0conf0 &= ~(RMT_CARRIER_EN_CH0_M | RMT_CARRIER_EFF_EN_CH0_M | RMT_CARRIER_OUT_LV_CH0_M);
    ch0conf0 &= ~(1 << 21);  // RMT_CARRIER_EN_CH0 — главный выключатель несущей
    ch0conf0 &= ~(1 << 20);  // RMT_CARRIER_EFF_EN_CH0 — несущая только при передаче данных
    ch0conf0 &= ~(1 << 22);  // RMT_CARRIER_OUT_LV_CH0 — полярность несущей


    // Сброс памяти канала (RMT_APB_MEM_RST_CH0, бит 2)
    // Сначала временно ставим 1 и пишем в регистр
    // Аналог: REG_WRITE(RMT_CH0CONF0_REG, ch0conf0 | (1 << RMT_APB_MEM_RST_CH0_S));
    REG_WRITE(RMT_CH0CONF0_REG, ch0conf0 | (1 << 2)); 
    
    // Затем снимаем 1 (сбрасываем триггер)
    // Аналог: ch0conf0 &= ~(1 << RMT_APB_MEM_RST_CH0_S); REG_WRITE(RMT_CH0CONF0_REG, ch0conf0);
    REG_WRITE(RMT_CH0CONF0_REG, ch0conf0 &= ~(1 << 2));


    // ========================================================
    // ШАГ 4: САМОЕ ГЛАВНОЕ - Защелкнуть настройки!
    // ========================================================
    // Установить бит RMT_CONF_UPDATE в RMT_CH0CONF0_REG.

    // Устанавливаем бит защелки (RMT_CONF_UPDATE_CH0, бит 24)
    // Аналог: ch0conf0 |= (1 << RMT_CONF_UPDATE_CH0_S);
    ch0conf0 |= (1 << 24); 
    
    // Финальная запись конфигурации в железо
    REG_WRITE(RMT_CH0CONF0_REG, ch0conf0); 

    ESP_LOGI(TAG, "  CH0CONF0 после: 0x%08lX", (unsigned long)ch0conf0);
    ESP_LOGI(TAG, "ШАГ 4: Защёлка записана. Инициализация завершена!");

    // Запоминаем разрешение для вспомогательных функций (µs → тики)
    s_resolution_hz = resolution_hz;
}

void rmt_raw_send_items(const rmt_item_t *items, size_t count) {
    ESP_LOGD(TAG, "send_items: count=%d", (int)count);

    uint32_t ch0conf0 = REG_READ(RMT_CH0CONF0_REG);

   // ========================================================
    // ШАГ 1: Сброс указателя записи (чтобы писать с нулевого адреса)
    // ========================================================
    
    // Включаем сброс памяти (RMT_APB_MEM_RST_CH0, бит 2) 
    // + обязательно ставим бит защелки (RMT_CONF_UPDATE_CH0, бит 24)
    // Аналог: REG_WRITE(RMT_CH0CONF0_REG, ch0conf0 | (1 << RMT_APB_MEM_RST_CH0_S) | (1 << RMT_CONF_UPDATE_CH0_S));
    REG_WRITE(RMT_CH0CONF0_REG, ch0conf0 | (1 << 2) | (1 << 24));
    
    // Выключаем сброс памяти (снимаем бит 2), чтобы разрешить запись
    // + снова ставим защелку (RMT_CONF_UPDATE_CH0, бит 24)
    // Аналог: REG_WRITE(RMT_CH0CONF0_REG, (ch0conf0 & ~(1 << RMT_APB_MEM_RST_CH0_S)) | (1 << RMT_CONF_UPDATE_CH0_S));
    REG_WRITE(RMT_CH0CONF0_REG, (ch0conf0 & ~(1 << 2)) | (1 << 24));


    // ========================================================
    // ШАГ 2: Упаковка и запись данных в FIFO (память RMT)
    // ========================================================
    for (size_t i = 0; i < count; i++) {
        // Упаковываем структуру в 32-битное слово согласно TRM
        uint32_t word = (items[i].dur0 & 0x7FFF) | (items[i].lvl0 << 15) | 
                        ((items[i].dur1 & 0x7FFF) << 16) | (items[i].lvl1 << 31);
        
        // Пишем слово в регистр данных (RMT_CH0DATA_REG). Железо само сдвинет Write Pointer на +1
        REG_WRITE(RMT_CH0DATA_REG, word);
    }


    // ========================================================
    // ШАГ 3: Обязательный нуль-маркер
    // ========================================================
    // Если duration0 = 0, RMT понимает, что это конец передачи
    // Записываем нуль в регистр данных (RMT_CH0DATA_REG)
    REG_WRITE(RMT_CH0DATA_REG, 0);

    // ========================================================
    // ШАГ 4: Сброс указателя чтения + СТАРТ передачи
    // ========================================================
    
    // Перечитываем регистр конфигурации на всякий случай
    ch0conf0 = REG_READ(RMT_CH0CONF0_REG);

    // Включаем сброс указателя чтения (RMT_MEM_RD_RST_CH0, бит 1) 
    // + защелка (RMT_CONF_UPDATE_CH0, бит 24)
    // Аналог: REG_WRITE(RMT_CH0CONF0_REG, ch0conf0 | (1 << RMT_MEM_RD_RST_CH0_S) | (1 << RMT_CONF_UPDATE_CH0_S));
    REG_WRITE(RMT_CH0CONF0_REG, ch0conf0 | (1 << 1) | (1 << 24));
    
    // Снимаем сброс указателя чтения (сбрасываем бит 1)
    // Аналог: ch0conf0 &= ~(1 << RMT_MEM_RD_RST_CH0_S);
    ch0conf0 &= ~(1 << 1);

    // Сбрасываем флаг старта, если он остался от предыдущей передачи
    ch0conf0 &= ~(1 << 0);
    REG_WRITE(RMT_CH0CONF0_REG, ch0conf0 | (1 << 24));
    
    // Устанавливаем бит старта передачи (RMT_TX_START_CH0, бит 0)
    // Аналог: ch0conf0 |= (1 << RMT_TX_START_CH0_S);
    ch0conf0 |= (1 << 0);
    
    // Записываем финальную конфигурацию со снятым сбросом, флагом старта и ЗАЩЕЛКОЙ (RMT_CONF_UPDATE_CH0, бит 24)
    // Аналог: REG_WRITE(RMT_CH0CONF0_REG, ch0conf0 | (1 << RMT_CONF_UPDATE_CH0_S));
    REG_WRITE(RMT_CH0CONF0_REG, ch0conf0 | (1 << 24));
}

void rmt_raw_wait(void) {
    // ========================================================
    // ШАГ 1: Опрос (Polling) регистра сырых прерываний
    // ========================================================
    // Читаем RMT_INT_RAW_REG, накладываем маску на нулевой бит (CH0_TX_END_INT_RAW).
    // Пока результат равен 0 — передача еще идет, крутимся в пустом цикле.
    while ((REG_READ(RMT_INT_RAW_REG) & (1 << 0)) == 0) {
        // Блокирующее ожидание. 
        // Примечание: В production-коде здесь используется FreeRTOS прерывание, 
        // чтобы отдать процессорное время другим задачам, пока RMT работает.
        // Но для понимания работы с железом поллинг — идеальный старт.
    }
    ESP_LOGD(TAG, "TX_END получен, очищаем флаг");

    // ========================================================
    // ШАГ 2: Очистка флага (Acknowledge)
    // ========================================================
    // Как только цикл завершился, передача окончена.
    // ОБЯЗАТЕЛЬНО нужно сбросить флаг, записав 1 в нулевой бит регистра RMT_INT_CLR_REG.
    // Если использовать REG_WRITE(RMT_INT_RAW_REG, 0), это не сработает — флаги 
    // аппаратуры очищаются именно через запись в выделенный CLR-регистр.
    REG_WRITE(RMT_INT_CLR_REG, (1 << 0));
}

// ========================================================
// rmt_raw_send_pulses — удобная обёртка
// ========================================================
// Принимает массив длительностей в МИКРОСЕКУНДАХ и стартовый уровень.
// Уровень чередуется автоматически: start_level → !start_level → start_level → ...
//
// Пример: durations_us = {100, 200, 300}, start_level = 1
//   → 100µs HIGH, 200µs LOW, 300µs HIGH
//
// Внутри пакует пары длительностей в rmt_item_t и вызывает send_items.
// Если count нечётный — последний символ имеет dur1=0 (нуль-маркер встроен).
void rmt_raw_send_pulses(const uint32_t *durations_us, size_t count, int start_level) {
    if (count == 0) return;

    // Коэффициент пересчёта: µs → тики
    // При resolution = 1 MHz → 1 тик = 1 µs → коэффициент = 1
    // При resolution = 2 MHz → 1 тик = 0.5 µs → коэффициент = 2
    uint32_t ticks_per_us = s_resolution_hz / 1000000;
    if (ticks_per_us == 0) ticks_per_us = 1;

    // Каждый rmt_item_t хранит ДВА полуцикла, поэтому кол-во символов = (count+1)/2
    size_t item_count = (count + 1) / 2;
    rmt_item_t items[item_count];

    int level = start_level ? 1 : 0;

    for (size_t i = 0; i < item_count; i++) {
        size_t idx = i * 2;

        // Первый полуцикл (dur0, lvl0)
        items[i].dur0 = (uint16_t)(durations_us[idx] * ticks_per_us);
        items[i].lvl0 = level;
        level = !level;

        // Второй полуцикл (dur1, lvl1) — или 0, если count нечётный и это последний
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
// rmt_raw_square — генерация меандра
// ========================================================
// freq_hz  — частота меандра в Гц
// duty_pct — скважность в % (0..100): доля HIGH-уровня в одном периоде
// periods  — количество полных периодов
//
// Пример: freq_hz=1000, duty_pct=50, periods=10
//   → 10 периодов по 1 мс (500µs HIGH + 500µs LOW)
//
// Формулы:
//   period_ticks = resolution_hz / freq_hz
//   high_ticks   = period_ticks * duty_pct / 100
//   low_ticks    = period_ticks - high_ticks
void rmt_raw_square(uint32_t freq_hz, uint8_t duty_pct, uint32_t periods) {
    if (freq_hz == 0 || periods == 0) return;
    if (duty_pct > 100) duty_pct = 100;

    // Один период меандра в тиках RMT
    uint32_t period_ticks = s_resolution_hz / freq_hz;

    // Разделяем период на HIGH и LOW по скважности
    uint32_t high_ticks = period_ticks * duty_pct / 100;
    uint32_t low_ticks  = period_ticks - high_ticks;

    // Каждый rmt_item_t = один полный период меандра (HIGH + LOW)
    // Ограничиваем по размеру RAM блока RMT (48 символов для CH0)
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
