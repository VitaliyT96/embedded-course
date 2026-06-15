#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "HW_PART_V";

/* ===== В1: Кільцевий буфер ===== */
#define RB_SIZE 4
static uint8_t rb_buf[RB_SIZE];
static uint16_t rb_head = 0, rb_tail = 0;

static bool rb_push(uint8_t v) {
    uint16_t next = (rb_head + 1u) % RB_SIZE;
    if (next == rb_tail) return false;
    rb_buf[rb_head] = v;
    rb_head = next;
    return true;
}

static bool rb_pop(uint8_t *v) {
    if (rb_tail == rb_head) return false;
    *v = rb_buf[rb_tail];
    rb_tail = (rb_tail + 1u) % RB_SIZE;
    return true;
}

void demo_v1_ringbuf(void) {
    ESP_LOGI(TAG, "== В1: Кільцевий буфер ==");
    rb_head = rb_tail = 0;
    
    // Намагаємося додати 4 елементи. Через особливості кільцевого буфера
    // 1 слот завжди порожній (щоб відрізнити повний від порожнього), тому 4-й елемент дасть FULL.
    for (uint8_t v = 1; v <= 4; v++) {
        ESP_LOGI(TAG, "push %d -> %s", v * 10, rb_push(v * 10) ? "ok" : "FULL");
    }
    
    // Після вилучення індекси обгортаються по колу завдяки % RB_SIZE
    // Делаем два pop и еще два push
    uint8_t out;
    if (rb_pop(&out)) ESP_LOGI(TAG, "pop -> %d", out);
    if (rb_pop(&out)) ESP_LOGI(TAG, "pop -> %d", out);
    
    ESP_LOGI(TAG, "push 50 -> %s", rb_push(50) ? "ok" : "FULL");
    ESP_LOGI(TAG, "push 60 -> %s", rb_push(60) ? "ok" : "FULL");
    
    ESP_LOGI(TAG, "head=%d, tail=%d", rb_head, rb_tail);
}

/* ===== В2: State machine ===== */
// Добавлен ST_PAUSE
typedef enum { ST_WAIT, ST_LED_ON, ST_PAUSE, ST_LED_OFF, ST_DONE } st_t;

void demo_v2_fsm(void) {
    ESP_LOGI(TAG, "== В2: State machine ==");
    st_t st = ST_WAIT;
    int blinks = 0;
    
    // Кожен прохід циклу виконує лише один крок автомата (один case),
    // не блокуючи систему затримками на кшталт delay().
    for (int tick = 0; tick < 12; tick++) {
        switch (st) {
            case ST_WAIT:
                ESP_LOGI(TAG, "tick %d: WAIT", tick);
                st = ST_LED_ON;
                break;
            case ST_LED_ON:
                ESP_LOGI(TAG, "tick %d: LED ON", tick);
                st = ST_PAUSE;
                break;
            case ST_PAUSE:
                ESP_LOGI(TAG, "tick %d: PAUSE", tick);
                st = ST_LED_OFF;
                break;
            case ST_LED_OFF:
                ESP_LOGI(TAG, "tick %d: LED OFF", tick);
                st = (++blinks >= 2) ? ST_DONE : ST_WAIT;
                break;
            case ST_DONE:
                ESP_LOGI(TAG, "tick %d: DONE", tick);
                break;
        }
    }
}

/* ===== В3: Volatile ===== */
// volatile забороняє компілятору кешувати значення в регістрі процесора,
// змушуючи кожного разу читати його з оперативної пам'яті. Без volatile
// оптимізатор міг би перетворити цикл while на нескінченний.
static volatile sig_atomic_t tick_flag = 0;

static void on_timer(void *arg) {
    tick_flag = 1; 
}

void demo_v3_volatile(void) {
    ESP_LOGI(TAG, "== В3: Volatile ==");
    const esp_timer_create_args_t args = { .callback = on_timer, .name = "tick" };
    esp_timer_handle_t timer;
    esp_timer_create(&args, &timer);
    esp_timer_start_periodic(timer, 100000); // 100 мс

    int ticks = 0;
    while (ticks < 3) {
        if (tick_flag) {
            tick_flag = 0;
            ESP_LOGI(TAG, "tick %d (прапорець помічено)", ++ticks);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    esp_timer_stop(timer);
    esp_timer_delete(timer);
}

/* ===== В4: Константи ===== */
#define LUT_SIZE 4
static const uint8_t gamma_lut_const[LUT_SIZE] = { 0, 3, 9, 27 }; 
static uint8_t gamma_lut_var[LUT_SIZE] = { 0, 3, 9, 27 }; // Той самий масив, але БЕЗ const

void demo_v4_constants(void) {
    ESP_LOGI(TAG, "== В4: Константи ==");
    // gamma_lut_const лежить у Flash-пам'яті (DROM, адреси ~0x3c.../0x3f4...)
    // gamma_lut_var лежить в оперативній пам'яті (DRAM, адреси ~0x3fc...)
    ESP_LOGI(TAG, "&gamma_lut_const (const): %p", (void *)gamma_lut_const);
    ESP_LOGI(TAG, "&gamma_lut_var (no const): %p", (void *)gamma_lut_var);
    
    // Використання змінних (навіть const) для розміру масиву (VLA)
    // в embedded-розробці є небезпечним через ризик переповнення стека.
    const int N = 4;
    uint8_t test_arr[N];
}

/* ===== В5: Ініціалізація ===== */
struct gpio_config {
    uint32_t dir;
    uint32_t out;
    uint8_t pin;
    uint32_t speed; // Додано нове поле
};

// Задаємо speed
static const struct gpio_config led_cfg_full = { .dir = 1u << 5, .out = 0, .pin = 5, .speed = 100 };
// НЕ задаємо speed (designated init)
static const struct gpio_config led_cfg_partial = { .dir = 1u << 5, .out = 0, .pin = 6 }; 

static void apply_config(const struct gpio_config *c, const char* name) {
    ESP_LOGI(TAG, "apply %s: pin=%u speed=%u", name, c->pin, (unsigned)c->speed);
}

static void lazy_feature(void) {
    static bool inited = false;
    if (!inited) {
        ESP_LOGI(TAG, "lazy: ініціалізація (один раз)");
        inited = true;
    }
    ESP_LOGI(TAG, "lazy: використання");
}

void demo_v5_init(void) {
    ESP_LOGI(TAG, "== В5: Ініціалізація ==");
    apply_config(&led_cfg_full, "full");
    // При частковій ініціалізації невказане поле (speed) автоматично заповнюється нулем
    apply_config(&led_cfg_partial, "partial");
    
    // Локальна static змінна зберігає свій стан, тому ініціалізація відбудеться лише раз
    lazy_feature();
    lazy_feature();
    lazy_feature();
}

void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "=== СТАРТ ЧАСТИНИ В ===");
    
    demo_v1_ringbuf();
    demo_v2_fsm();
    demo_v3_volatile();
    demo_v4_constants();
    demo_v5_init();
    
    ESP_LOGI(TAG, "=== ГОТОВО ===");
}