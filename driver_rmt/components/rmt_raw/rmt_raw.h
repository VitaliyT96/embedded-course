#pragma once
#include <stdint.h>
#include <stddef.h>

// Той самий 32-бітний символ RMT, розділений на 4 частини
typedef struct {
    uint16_t dur0; uint8_t lvl0;
    uint16_t dur1; uint8_t lvl1;
} rmt_item_t;

void rmt_raw_init(int gpio, uint32_t resolution_hz);
void rmt_raw_send_items(const rmt_item_t *items, size_t count);
void rmt_raw_wait(void);

// Допоміжні функції — обгортки поверх send_items (без регістрів, чиста логіка)
// durations_us: масив тривалостей у мкс, рівень чергується від start_level
void rmt_raw_send_pulses(const uint32_t *durations_us, size_t count, int start_level);
// freq_hz: частота меандру, duty_pct: шпаруватість (%), periods: кількість періодів
void rmt_raw_square(uint32_t freq_hz, uint8_t duty_pct, uint32_t periods);
