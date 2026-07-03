#pragma once
#include <stdint.h>
#include <stddef.h>

// Тот самый 32-битный символ RMT, разделенный на 4 части
typedef struct {
    uint16_t dur0; uint8_t lvl0;
    uint16_t dur1; uint8_t lvl1;
} rmt_item_t;

void rmt_raw_init(int gpio, uint32_t resolution_hz);
void rmt_raw_send_items(const rmt_item_t *items, size_t count);
void rmt_raw_wait(void);

// Вспомогательные функции — обёртки поверх send_items (без регистров, чистая логика)
// durations_us: массив длительностей в мкс, уровень чередуется от start_level
void rmt_raw_send_pulses(const uint32_t *durations_us, size_t count, int start_level);
// freq_hz: частота меандра, duty_pct: скважность (%), periods: кол-во периодов
void rmt_raw_square(uint32_t freq_hz, uint8_t duty_pct, uint32_t periods);
