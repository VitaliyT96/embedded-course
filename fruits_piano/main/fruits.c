#include "fruits.h"
#include "buzzer.h"
#include "driver/touch_pad.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "FRUITS";

#define ACTIVE_DELTA_THRESHOLD 50000
#define DELTA_FULL_SCALE 300000

#include "driver/gpio.h"
#define BOOT_BUTTON_PIN GPIO_NUM_0

// atomic is used for thread safety between freertos tasks (reader/writer)
atomic_int fruit_pot_value = 0;      // touch intensity (0 - 4095)
atomic_bool fruit_is_active = false; // flag if any fruit is touched

// struct for each fruit sensor
typedef struct {
    touch_pad_t id;       // hardware pin id (e.g. TOUCH_PAD_NUM5)
    const char* name;     // name for logs
    uint32_t baseline;    // empty capacitance value. uint32_t because s3 values are huge
    float ema;            // smoothed value to remove noise. float is needed for math precision
    uint32_t freq_hz;     // buzzer note frequency
} fruit_t;

// array of our fruit keys
static fruit_t fruits[] = {
    { TOUCH_PAD_NUM5, "Orange",     0, 0.0f, 261 }, // note c4 - orange
    { TOUCH_PAD_NUM6, "Strawberry", 0, 0.0f, 329 }, // note e4 - strawberry
    { TOUCH_PAD_NUM7, "Cucumber",   0, 0.0f, 392 }, // note g4 - cucumber
    { TOUCH_PAD_NUM8, "Tomato",     0, 0.0f, 523 }  // note c5 - tomato (pin 8)
};

#define NUM_FRUITS (sizeof(fruits) / sizeof(fruits[0]))

static bool is_muted = false;
static bool last_btn_state = true;

static void fruits_task(void* arg) {
    // configure boot button
    gpio_set_direction(BOOT_BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BOOT_BUTTON_PIN, GPIO_PULLUP_ONLY);

    // if we don't wait, baseline will be wrong
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // do 100 dummy reads to charge up the internal rc circuit
    for (int j = 0; j < 100; j++) {
        touch_pad_sw_start();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "Setting final baselines...");
    
    // save the baseline values for untouched state
    for (int i = 0; i < NUM_FRUITS; i++) {
        uint32_t raw_val;
        touch_pad_sw_start();
        vTaskDelay(pdMS_TO_TICKS(10));
        if (touch_pad_read_raw_data(fruits[i].id, &raw_val) == ESP_OK) {
            fruits[i].baseline = raw_val;
            fruits[i].ema = (float)raw_val; // init ema filter
            ESP_LOGI(TAG, "%s Baseline: %lu", fruits[i].name, fruits[i].baseline);
        }
    } ESP_LOGI(TAG, "Fruits Task ready!");

    // main loop
    while (1) {
        // start hardware scan
        touch_pad_sw_start();
        
        // 20ms delay gives ~50hz refresh rate. fast enough for fingers, leaves cpu for wifi
        vTaskDelay(pdMS_TO_TICKS(20));

        // handle mute button
        bool btn_state = gpio_get_level(BOOT_BUTTON_PIN);
        if (!btn_state && last_btn_state) {
            is_muted = !is_muted;
            ESP_LOGI(TAG, "Mute toggled: %s", is_muted ? "MUTED" : "UNMUTED");
        }
        last_btn_state = btn_state;

        // print debug logs every 50 loops (about 1 sec)
        static int tick = 0;
        bool print_debug = (tick++ % 50 == 0);
        
        uint32_t max_delta = 0;
        int active_idx = -1; // currently touched fruit

        for (int i = 0; i < NUM_FRUITS; i++) {
            uint32_t raw_val;
            // read raw sensor data
            esp_err_t ret = touch_pad_read_raw_data(fruits[i].id, &raw_val);
            if (ret == ESP_OK) {
                // fast ema filter to reduce 50hz noise
                // 0.6 for new data makes response fast without lag
                fruits[i].ema = (fruits[i].ema * 0.4f) + ((float)raw_val * 0.6f);
                
                // calculate absolute delta
                // on s3 touch increases by ~130k units regardless of baseline
                float diff = fruits[i].ema - (float)fruits[i].baseline;
                uint32_t delta = (uint32_t)(diff > 0 ? diff : -diff);
                
                if (print_debug) {
                    ESP_LOGI(TAG, "%s: raw=%lu, base=%lu, delta=%lu", 
                             fruits[i].name, raw_val, fruits[i].baseline, delta);
                }
                
                // trigger threshold is 50k
                if (delta > 50000 && delta > max_delta) {
                    max_delta = delta;
                    active_idx = i; // remember which fruit was pressed hardest
                } else if (delta < 20000) {
                    // adaptive baseline: if not touched, slowly track current value
                    // fixes random beeps from temperature drift
                    fruits[i].baseline = (uint32_t)(((float)fruits[i].baseline * 0.99f) + ((float)raw_val * 0.01f));
                }
            }
        }

        // if any fruit is touched
        if (active_idx >= 0) {
            atomic_store(&fruit_is_active, true);
            
            // map intensity (50k-150k) to graph height (0-4095)
            uint32_t active_range = max_delta > 50000 ? max_delta - 50000 : 0;
            uint32_t mapped_val = (active_range * 4095) / 100000;
            if (mapped_val > 4095) mapped_val = 4095; // ceiling limit
            
            // send to web scope
            atomic_store(&fruit_pot_value, (int)mapped_val);
            
            // play sound
            if (!is_muted) {
                buzzer_start_tone(fruits[active_idx].freq_hz);
            } else {
                buzzer_stop_tone();
            }
        } else {
            // no touch - stop sound and reset graph
            atomic_store(&fruit_is_active, false);
            atomic_store(&fruit_pot_value, 0);
            buzzer_stop_tone();
        }
    }
}

void fruits_init(void) {
    ESP_LOGI(TAG, "Initializing Touch Pads...");
    ESP_ERROR_CHECK(touch_pad_init());
    
    for (int i = 0; i < NUM_FRUITS; i++) {
        ESP_ERROR_CHECK(touch_pad_config(fruits[i].id));
    }
    
    ESP_ERROR_CHECK(touch_pad_set_fsm_mode(TOUCH_FSM_MODE_SW));

    xTaskCreate(fruits_task, "fruits_task", 4096, NULL, 5, NULL);
}
