#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "irq";

#define BUTTON_GPIO   GPIO_NUM_4        /* безпечний пін на ESP32-S3 */
#define DEBOUNCE_US   200000           /* вікно дебаунсу: 200 мс */
#define WINDOW_S      8                /* скільки секунд триває кожне демо з кнопкою */

static QueueHandle_t q;                /* черга подій з ISR -> superloop (int64_t) */
static volatile int64_t        last_us = 0;
static volatile sig_atomic_t   flag    = 0;
static volatile uint32_t       raw_count, clean_count;


/* ---- спільний дебаунс (теж IRAM_ATTR, бо викликається з ISR) ---- */
static bool IRAM_ATTR debounce_ok(void)
{
    int64_t now = esp_timer_get_time();
    if (now - last_us < DEBOUNCE_US) return false;
    last_us = now;
    return true;
}

/* ---- варіанти ISR під різні демо ---- */
static void IRAM_ATTR isr_flag(void *arg)        /* демо 1: ставить прапорець */
{
    if (debounce_ok()) flag = 1;
}
static void IRAM_ATTR isr_debounce(void *arg)    /* демо 2: рахує сирі й чисті */
{
    raw_count++;                                  /* кожен фронт, навіть брязкіт */
    if (debounce_ok()) clean_count++;             /* лише реальні натискання */
}
static void IRAM_ATTR isr_edge(void *arg)        /* демо 3: кладе рівень у чергу */
{
    int64_t level = gpio_get_level(BUTTON_GPIO);
    xQueueSendFromISR(q, &level, NULL);
}
static void IRAM_ATTR isr_queue(void *arg)       /* демо 4: кладе час у чергу */
{
    if (debounce_ok()) {
        int64_t now = esp_timer_get_time();
        xQueueSendFromISR(q, &now, NULL);
    }
}
static void on_timer(void *arg)                  /* демо 5: heartbeat */
{
    flag = 1;
}


/* ---- хелпери підключення/відключення переривання кнопки ---- */
static void button_attach(gpio_int_type_t type, gpio_isr_t handler)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = 1,
        .intr_type    = type,
    };
    gpio_config(&io);
    gpio_isr_handler_add(BUTTON_GPIO, handler, NULL);
}
static void button_detach(void)
{
    gpio_isr_handler_remove(BUTTON_GPIO);
}


/* ===== ДЕМО 1: базове переривання (ISR ставить прапорець) ===== */
void demo1_flag(void)
{
    ESP_LOGI(TAG, "== Демо 1: базове переривання (флаг) ==");
    ESP_LOGI(TAG, "натискайте кнопку ~%d с...", WINDOW_S);
    flag = 0; last_us = 0;
    button_attach(GPIO_INTR_NEGEDGE, isr_flag);

    int presses = 0;
    int64_t end = esp_timer_get_time() + (int64_t)WINDOW_S * 1000000;
    while (esp_timer_get_time() < end) {
        if (flag) { flag = 0; ESP_LOGI(TAG, "  натиснуто #%d", ++presses); }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    button_detach();
    ESP_LOGI(TAG, "разом натискань: %d\n", presses);
}


/* ===== ДЕМО 2: брязкіт контактів і дебаунс ===== */
void demo2_debounce(void)
{
    ESP_LOGI(TAG, "== Демо 2: брязкіт і дебаунс ==");
    ESP_LOGI(TAG, "натисніть кнопку 3 рази, не поспішаючи...");
    raw_count = 0; clean_count = 0; last_us = 0;
    button_attach(GPIO_INTR_NEGEDGE, isr_debounce);

    int64_t end = esp_timer_get_time() + (int64_t)WINDOW_S * 1000000;
    while (esp_timer_get_time() < end) vTaskDelay(pdMS_TO_TICKS(50));
    button_detach();

    ESP_LOGI(TAG, "фронтів усього (raw):        %u", (unsigned)raw_count);
    ESP_LOGI(TAG, "реальних натискань (clean):  %u", (unsigned)clean_count);
    ESP_LOGI(TAG, "різниця — це і є брязкіт контактів\n");
}


/* ===== ДЕМО 3: розрізнення фронтів (PRESS / RELEASE) ===== */
void demo3_edges(void)
{
    ESP_LOGI(TAG, "== Демо 3: розрізнення фронтів ==");
    ESP_LOGI(TAG, "натискайте Й ВІДПУСКАЙТЕ кнопку ~%d с...", WINDOW_S);
    last_us = 0; xQueueReset(q);
    button_attach(GPIO_INTR_ANYEDGE, isr_edge);

    int64_t end = esp_timer_get_time() + (int64_t)WINDOW_S * 1000000;
    while (esp_timer_get_time() < end) {
        int64_t level;
        if (xQueueReceive(q, &level, 0) == pdTRUE)
            ESP_LOGI(TAG, "  %s", level == 0 ? "PRESS  (фронт у 0)" : "RELEASE (фронт у 1)");
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    button_detach();
    ESP_LOGI(TAG, "ANYEDGE ловить обидва фронти; NEGEDGE — лише PRESS\n");
}


/* ===== ДЕМО 4: черга з ISR — передаємо ДАНІ, а не лише факт ===== */
void demo4_queue(void)
{
    ESP_LOGI(TAG, "== Демо 4: черга з ISR (дані події) ==");
    ESP_LOGI(TAG, "натискайте кнопку ~%d с — побачите час кожного натискання...", WINDOW_S);
    last_us = 0; xQueueReset(q);
    button_attach(GPIO_INTR_NEGEDGE, isr_queue);

    int n = 0;
    int64_t end = esp_timer_get_time() + (int64_t)WINDOW_S * 1000000;
    while (esp_timer_get_time() < end) {
        int64_t t;
        if (xQueueReceive(q, &t, 0) == pdTRUE)
            ESP_LOGI(TAG, "  натискання #%d у t=%lld мс", ++n, (long long)(t / 1000));
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    button_detach();
    ESP_LOGI(TAG, "через чергу ISR віддає main не лише факт, а й дані\n");
}


/* ===== ДЕМО 5: таймерне переривання (без кнопки) ===== */
void demo5_timer(void)
{
    ESP_LOGI(TAG, "== Демо 5: таймерне переривання ==");
    flag = 0;
    const esp_timer_create_args_t args = { .callback = on_timer, .name = "hb" };
    esp_timer_handle_t tm;
    esp_timer_create(&args, &tm);
    esp_timer_start_periodic(tm, 500000);          /* 500 мс */

    int beats = 0;
    while (beats < 6) {
        if (flag) { flag = 0; ESP_LOGI(TAG, "  heartbeat #%d (кожні 500 мс)", ++beats); }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    esp_timer_stop(tm);
    esp_timer_delete(tm);
    ESP_LOGI(TAG, "таймер сам генерує події — періодика без блокувань\n");
}


void app_main(void)
{
    q = xQueueCreate(16, sizeof(int64_t));
    gpio_install_isr_service(0);
    ESP_LOGI(TAG, "кнопка на GPIO%d (між піном і GND)\n", BUTTON_GPIO);

    demo1_flag();
    demo2_debounce();
    demo3_edges();
    demo4_queue();
    demo5_timer();

    ESP_LOGI(TAG, "усі демо завершено.");
}