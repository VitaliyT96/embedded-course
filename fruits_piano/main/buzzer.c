#include "buzzer.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Store the channel number globally for this file
static const ledc_channel_t BUZZER_CHANNEL = LEDC_CHANNEL_1;
static const ledc_timer_t BUZZER_TIMER = LEDC_TIMER_1;
static const ledc_mode_t BUZZER_MODE = LEDC_LOW_SPEED_MODE;

void buzzer_init(gpio_num_t pin) {
  // 1. Configure the timer to generate frequency
  ledc_timer_config_t timer_conf = {
      .speed_mode = BUZZER_MODE,
      .timer_num = BUZZER_TIMER,
      .duty_resolution = LEDC_TIMER_10_BIT, // 10 bits (values from 0 to 1023)
      .freq_hz = 2000,                      // Default frequency at startup
      .clk_cfg = LEDC_AUTO_CLK};
  ledc_timer_config(&timer_conf);

  // 2. Configure the channel that links the timer and physical pin
  ledc_channel_config_t channel_conf = {
      .gpio_num = pin,
      .speed_mode = BUZZER_MODE,
      .channel = BUZZER_CHANNEL,
      .timer_sel = BUZZER_TIMER,
      .duty = 0, // Start with silence (duty cycle 0)
      .hpoint = 0};
  ledc_channel_config(&channel_conf);
}

void buzzer_play_tone(uint32_t freq_hz, uint32_t duration_ms) {
  if (freq_hz > 0) {
    // Change the frequency
    ledc_set_freq(BUZZER_MODE, BUZZER_TIMER, freq_hz);
    // Set volume to ~50% (512 out of 1023) - ideal
    // square wave for a buzzer
    ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 512);
    ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);

    // Wait for the required time while the hardware plays the sound
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
  }

  // Turn off the sound (duty cycle 0)
  ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 0);
  ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
}

void buzzer_start_tone(uint32_t freq_hz) {
  if (freq_hz > 0) {
    ledc_set_freq(BUZZER_MODE, BUZZER_TIMER, freq_hz);
    ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 512);
    ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
  }
}

void buzzer_stop_tone(void) {
  ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 0);
  ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
}