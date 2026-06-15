#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "HW_PART_B";

// Структура для задачи Б9
struct point {
    int x;
    int y;
};

// Прототипы функций для Б7 и Б8 (чтобы компилятор не ругался)
void fill(int *buf, int n, int value);
void get_min_max(const int *arr, int n, int *min, int *max);


/* ==========================================================
 * РЕАЛИЗАЦИЯ ЗАДАЧ
 * ========================================================== */

void task_b1_pointers(void) {
    ESP_LOGI(TAG, "--- Б1: Разминка ---");
    int x = 42;
    int *p = &x;

    ESP_LOGI(TAG, "x        = %d", x);
    ESP_LOGI(TAG, "&x       = %p", (void *)&x);    
    ESP_LOGI(TAG, "p        = %p", (void *)p);     
    ESP_LOGI(TAG, "*p       = %d", *p);  
    
    // Оскільки вказівник p зберігає фізичну адресу x, 
    // зміна значення за розіменованим вказівником напряму змінює x.
    *p=100;
    ESP_LOGI(TAG, "p x      = %d", x);  
}

void task_b2_swap(void) {
    ESP_LOGI(TAG, "--- Б2: Обмен значений ---");
   int a=1;
   int b=2;

   int *p_a=&a;
   int *p_b=&b;

   // Обмін відбувається виключно через адреси змінних,
   // без прямого звертання до a чи b.
   int temp;
   temp=*p_a;
   *p_a=*p_b;
   *p_b=temp;
   ESP_LOGI(TAG, "a=%d,b=%d",a,b); 
}

void task_b3_b4_b5_arithmetic(void) {
    ESP_LOGI(TAG, "--- Б3, Б4, Б5: Арифметика указателей ---");
    int arr[5] = {10, 20, 30, 40, 50};
    // Кожен елемент типу int займає 4 байти, тому адреси відрізнятимуться на 4.
    for (int i=0; i < 5;i++) {
        ESP_LOGI(TAG, "%d %p ", arr[i], (void *)&arr[i]);
    }
    
    ESP_LOGI(TAG, "--- arr та &arr[0]---");
    // Ім'я масиву неявно перетворюється на вказівник на його нульовий елемент.
    ESP_LOGI(TAG, "%p", (void *)arr);
    ESP_LOGI(TAG, "%p", (void *)&arr[0]);
    
    ESP_LOGI(TAG, "--- arr[2] та *(arr+2)---");
    ESP_LOGI(TAG, "%d", arr[2]);
    ESP_LOGI(TAG, "%d", *(arr+2)); // Исправлено на %d, так как это значение
    
    // Віднімання вказівників повертає кількість елементів, а не байтів (результат 3).
    ESP_LOGI(TAG, "%d", (arr + 4) - (arr + 1));

    //-----------//
    int el_count = 0;
    ESP_LOGI(TAG, "--- el_count_val classic for arr[i]---");
    for (int i = 0; i < 5; i++){
        el_count += arr[i];
    }
    ESP_LOGI(TAG, "%d", el_count);

    el_count=0;
    ESP_LOGI(TAG, "--- el_count_val pointer q = arr---");
    for (int *q = arr; q < arr + 5; q++)
        el_count += *q;
    ESP_LOGI(TAG, "%d", el_count);
}

void task_b6_strings(void) {
    ESP_LOGI(TAG, "--- Б6: Строки и указатели ---");
    char s[] = "ESP32-S3";
    int s_lenght = 0;

    // Вказівник іде по пам'яті доки не натрапить на прихований нуль-термінатор '\0'.
    for (char *q = s; *q != '\0'; q++) s_lenght++; 
    ESP_LOGI(TAG, "--- strlen value");
    ESP_LOGI(TAG, "%d", s_lenght);
  
}

void task_b7_fill(void) {
    ESP_LOGI(TAG, "--- Б7: Заполнение массива ---");
    int test_arr[5] = {0};
 ESP_LOGI(TAG, "%p", (void *)test_arr);

    // Масив передається як вказівник, тому функція має прямий доступ до 
    // оригінальної пам'яті масиву і змінює його вміст.
    fill(&test_arr[0], 5, 42);

    for (int *p=test_arr; p < test_arr + 5; p++) {
     ESP_LOGI(TAG, "%d", *p);
    }
}

void task_b8_minmax(void) {
    ESP_LOGI(TAG, "--- Б8: min и max через указатели ---");
    int arr[] = {15, 3, 42, 7, -1};
    int min, max;
    // Використовуємо вказівники як вихідні параметри, щоб функція могла 
    // повернути одразу два значення (min та max).
    get_min_max(&arr[0], 5, &min, &max);
    ESP_LOGI(TAG, "min=%d", min);
    ESP_LOGI(TAG, "max=%d", max);
}

void task_b9_structs(void) {
    ESP_LOGI(TAG, "--- Б9: Указатели на структуры ---");
    struct point pts[3];
    for (struct point *p = pts; p < pts+3; p++) {
        p->x = 1;
        p->y = 2;
    }
    for (struct point *p = pts; p < pts+3; p++) {
        ESP_LOGI(TAG, "x=%d,y=%d", p->x, p->y);
    }
    
    
    // Структура point містить два int по 4 байти, тому її загальний розмір 8 байт.
    // Адреси сусідніх елементів відрізнятимуться на 8 байтів.
    ESP_LOGI(TAG, "Адрес pts[0]: %p", (void *)&pts[0]);
    ESP_LOGI(TAG, "Адрес pts[1]: %p", (void *)&pts[1]);
}


/* ==========================================================
 * ТОЧКА ВХОДА
 * ========================================================== */
void app_main(void) {
    // Даем время на подключение монитора порта (2 секунды)
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "=== СТАРТ ДОМАШНЕГО ЗАДАНИЯ (ЧАСТЬ Б) ===");

    task_b1_pointers();
    task_b2_swap();
    task_b3_b4_b5_arithmetic();
    task_b6_strings();
    task_b7_fill();
    task_b8_minmax();
    task_b9_structs();

    ESP_LOGI(TAG, "=== ВСЕ ЗАДАЧИ ВЫПОЛНЕНЫ ===");
}

/* ==========================================================
 * ФУНКЦИИ-ПОМОЩНИКИ (для Б7 и Б8)
 * ========================================================== */
void fill(int *buf, int n, int value) {
    for (int * p=buf; p < buf+n; p++) {
    *p=value; 
    }
}

// Модифікатор const гарантує, що функція лише читатиме масив arr 
// і випадково не змінить його дані.
void get_min_max(const int *arr, int n, int *min, int *max) {
    *min = arr[0];
    *max = arr[0];
    for (const int* p = arr; p < arr + n; p++) {
        if (*p > *max) {
            *max = *p;
        }
        if (*p < *min) {
            *min = *p;
        }
    }
}