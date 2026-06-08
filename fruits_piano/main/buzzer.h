#pragma once

#include "driver/gpio.h"
#include <stdint.h>

// Initialize buzzer pin
void buzzer_init(gpio_num_t pin);

// Play a sound
// freq_hz - frequency in Hertz (e.g., 1000)
// duration_ms - duration in milliseconds
void buzzer_play_tone(uint32_t freq_hz, uint32_t duration_ms);

void buzzer_start_tone(uint32_t freq_hz);
void buzzer_stop_tone(void);