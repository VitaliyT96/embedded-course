#include <stdio.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

// Hardware Configuration
#define MOTOR_PWM_GPIO      4
#define BUTTON_GPIO         5
#define POT_ADC_UNIT        ADC_UNIT_2
#define POT_ADC_CHANNEL     ADC_CHANNEL_2 // GPIO 13 on ESP32-S3 is ADC2 Channel 2

#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL        LEDC_CHANNEL_0
#define LEDC_DUTY_RES       LEDC_TIMER_10_BIT // 10-bit resolution (0-1023)
#define LEDC_FREQUENCY      20000 // 20 kHz
#define MIN_PWM_TO_START    400   // Minimum PWM (0-1023) at which motor starts spinning

static const char *TAG = "MOTOR_APP";

/*
 * Atomics ensure thread-safe operations (no race conditions) on multi-core ESP32.
 */
atomic_bool is_motor_running = ATOMIC_VAR_INIT(false);
atomic_int target_speed = ATOMIC_VAR_INIT(0);

// Global handle for the ADC
adc_oneshot_unit_handle_t adc2_handle;

/* 
 * Task 1: Button Task
 * Debounces button and toggles motor state
 */
void button_task(void *pvParameters) {
    int last_state = 0;
    int stable_state = 0;
    int debounce_counter = 0;
    const int DEBOUNCE_THRESHOLD = 3; 

    while (1) {
        int current_reading = gpio_get_level(BUTTON_GPIO);

        if (current_reading != stable_state) {
            debounce_counter++;
            if (debounce_counter >= DEBOUNCE_THRESHOLD) {
                stable_state = current_reading;
                debounce_counter = 0;

                // LOW to HIGH transition (Активний HIGH)
                if (stable_state == 1 && last_state == 0) {
                    bool current_flag = atomic_load(&is_motor_running);
                    atomic_store(&is_motor_running, !current_flag);
                    
                    ESP_LOGI(TAG, "Motor: %s", !current_flag ? "RUNNING" : "STOPPED");
                }
                last_state = stable_state;
            }
        } else {
            debounce_counter = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* 
 * Task 2: ADC Task
 * Reads ADC, applies EMA filter, maps to PWM duty cycle
 */
void adc_task(void *pvParameters) {
    int raw_val = 0;
    float filtered_val = 0.0f;
    const float ALPHA = 0.3f; // EMA filter alpha
    bool first_reading = true;

    while (1) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc2_handle, POT_ADC_CHANNEL, &raw_val));

        if (first_reading) {
            filtered_val = (float)raw_val;
            first_reading = false;
        } else {
            // EMA Filter formula
            filtered_val = (ALPHA * raw_val) + ((1.0f - ALPHA) * filtered_val);
        }

        // Map 12-bit ADC (0-4095) to 10-bit PWM (0-1023)
        int mapped_speed = 0;
        if (filtered_val > 50.0f) { 
            mapped_speed = MIN_PWM_TO_START + (int)((filtered_val / 4095.0f) * (1023 - MIN_PWM_TO_START));
        }
        
        if (mapped_speed < 0) mapped_speed = 0;
        if (mapped_speed > 1023) mapped_speed = 1023;

        atomic_store(&target_speed, mapped_speed);

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* 
 * Task 3: Motor Task
 * Controls PWM based on shared state (Non-blocking).
 */
void motor_task(void *pvParameters) {
    while (1) {
        bool running = atomic_load(&is_motor_running);
        int speed = atomic_load(&target_speed);

        if (running) {
            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, speed);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
        } else {
            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* 
 * Main Application
 */
void app_main(void) {
    ESP_LOGI(TAG, "Initializing Hardware...");

    // 1. Setup Button GPIO
    gpio_config_t btn_config = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_config);

    // 2. Setup LEDC (PWM) Timer and Channel
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = MOTOR_PWM_GPIO,
        .duty           = 0,
        .hpoint         = 0
    };
    ledc_channel_config(&ledc_channel);

    // 3. Setup ADC
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = POT_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc2_handle));


    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc2_handle, POT_ADC_CHANNEL, &config));

    ESP_LOGI(TAG, "Spawning Tasks...");

    // 4. Spawn the FreeRTOS Tasks
    xTaskCreate(button_task, "Button Task", 2048, NULL, 5, NULL);
    xTaskCreate(adc_task,    "ADC Task",    2048, NULL, 5, NULL);
    xTaskCreate(motor_task,  "Motor Task",  2048, NULL, 5, NULL);
}