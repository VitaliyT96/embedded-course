#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "mem";

/* глобальні дані в різних сегментах */
int        g_data = 42;     /* .data   -> DRAM (+копія у Flash) */
int        g_bss;           /* .bss    -> DRAM                  */
const int  g_ro   = 7;      /* .rodata -> Flash (DROM)          */


/* ===== ДЕМО 1: робота з вказівниками ===== */
void demo_pointers(void)
{
    int   x = 25;
    int  *p = &x;          /* p зберігає адресу x */

    ESP_LOGI(TAG, "== Демо 1: вказівники ==");
    ESP_LOGI(TAG, "x        = %d", x);
    ESP_LOGI(TAG, "&x       = %p", (void *)&x);    /* адреса x */
    ESP_LOGI(TAG, "p        = %p", (void *)p);     /* те саме */
    ESP_LOGI(TAG, "*p       = %d", *p);            /* значення за адресою */

    *p = 99;                                       /* запис через вказівник */
    ESP_LOGI(TAG, "після *p = 99  ->  x = %d", x); /* x теж змінилася */

    ESP_LOGI(TAG, "sizeof(x) = %u, sizeof(p) = %u",
             (unsigned)sizeof(x), (unsigned)sizeof(p));

    int arr[3] = {10, 20, 30};
    ESP_LOGI(TAG, "&arr[0]  = %p", (void *)&arr[0]);
    ESP_LOGI(TAG, "&arr[1]  = %p", (void *)&arr[1]); /* +4 байти */
    ESP_LOGI(TAG, "*(arr+2) = %d", *(arr + 2));      /* = arr[2] = 30 */

    int *nul = NULL;                               /* нікуди не вказує */
    ESP_LOGI(TAG, "nul      = %p", (void *)nul);
}


/* ===== ДЕМО 2: де живе кожна змінна ===== */
void demo_addresses(void)
{
    int   local = 1;                     /* стек */
    int  *heap  = malloc(sizeof *heap);  /* купа */

    ESP_LOGI(TAG, "== Демо 2: адреси сегментів ==");
    // Сегменти .text (код) та .rodata (константи) фізично лежать у Flash-пам'яті
    ESP_LOGI(TAG, ".text    func  : %p", (void *)&demo_addresses); /* IROM  ~0x42.. */
    ESP_LOGI(TAG, ".rodata  g_ro  : %p", (void *)&g_ro);           /* DROM  ~0x3C.. */
    
    // Сегменти .data, .bss, heap та stack розміщуються у внутрішній RAM (DRAM)
    ESP_LOGI(TAG, ".data    g_data: %p", (void *)&g_data);         /* DRAM  ~0x3FC. */
    ESP_LOGI(TAG, ".bss     g_bss : %p", (void *)&g_bss);          /* DRAM         */
    ESP_LOGI(TAG, "heap     *heap : %p", (void *)heap);            /* DRAM (купа)  */
    ESP_LOGI(TAG, "stack    local : %p", (void *)&local);          /* DRAM (стек задачі) */

    free(heap);
}


/* ===== ДЕМО 3: стек росте вниз ===== */
void recurse(int depth)
{
    int marker;

    // Додавання локального масиву (128 байтів) збільшує розмір стекового кадру.
    // Стек росте вниз (адреси зменшуються). Різниця між &marker сусідніх викликів
    // наочно покаже розмір цього кадру (~128 байтів різниці).
    // added  local array to increase stack size
    int big[32]; 
    big[0] = depth;

    ESP_LOGI(TAG, "depth=%d  &marker=%p", depth, (void *)&marker);
    if (depth < 8)
        recurse(depth + 1);
}

void demo_stack(void)
{
    ESP_LOGI(TAG, "== Демо 3: стек росте вниз ==");
    recurse(0);
}


/* ===== ДЕМО 4: фрагментація на «арені» 16 байт ===== */
#define ARENA 16
static char arena[ARENA];

void show_arena(void)
{
    char buf[ARENA + 1];
    for (int i = 0; i < ARENA; i++)
        buf[i] = arena[i] ? arena[i] : '.';
    buf[ARENA] = '\0';
    ESP_LOGI(TAG, "%s", buf);
}

int my_alloc(char tag, int size)
{
    for (int i = 0; i + size <= ARENA; i++) {
        int free_here = 1;
        for (int j = 0; j < size; j++)
            if (arena[i + j]) { free_here = 0; break; }
        if (free_here) {
            for (int j = 0; j < size; j++) arena[i + j] = tag;
            return i;
        }
    }
    return -1;                 /* немає size байтів ПІДРЯД */
}

void my_free(char tag)
{
    for (int i = 0; i < ARENA; i++)
        if (arena[i] == tag) arena[i] = 0;
}

void demo_fragmentation(void)
{
    ESP_LOGI(TAG, "== Демо 4: фрагментація (модель) ==");
    memset(arena, 0, ARENA);

    my_alloc('A', 4);
    my_alloc('B', 4);
    my_alloc('C', 4);
    show_arena();                       /* AAAABBBBCCCC.... */

    // Звільняємо блок у кінці. Оскільки з'являється достатньо суцільного 
    // (безперервного) простору, наступна алокація на 6 байтів пройде успішно.
    my_free('C');
    show_arena();                       /* AAAA....CCCC.... */

    int r = my_alloc('D', 6);
    show_arena();
    ESP_LOGI(TAG, "my_alloc(6) -> %s", r < 0 ? "FAIL" : "ok");
}


/* ===== ДЕМО 5: реальна купа ESP32-S3 ===== */
void demo_heap_real(void)
{
    ESP_LOGI(TAG, "== Демо 5: реальна купа ESP32 ==");
    ESP_LOGI(TAG, "вільно (сума)   : %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "найбільший блок : %u", (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    void *p[8];
    for (int i = 0; i < 8; i++) p[i] = malloc(4096);
    
    // Тут звільняються усі блоки підряд. Механізм злиття (coalescing)
    // об'єднує суміжні вільні блоки в один великий суцільний простір.
    for (int i = 0; i < 8; i++) free(p[i]);     /* звільняємо через один */

    ESP_LOGI(TAG, "після шахматних free:");
    ESP_LOGI(TAG, "вільно (сума)   : %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "найбільший блок : %u", (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    for (int i = 1; i < 8; i += 2) free(p[i]);     /* прибираємо за собою */
}


/* ----------------------------------------------------------
 * ПСЕВДОКОД: шлях до app_main() на ESP32-S3
 *   ROM-завантажувач -> 2-й завантажувач (bootloader)
 *   -> startup: copy .data, zero .bss, кеш Flash
 *   -> старт FreeRTOS -> задача main -> app_main();
 * ---------------------------------------------------------- */

void app_main(void)
{
    demo_pointers();
    demo_addresses();
    demo_stack();
    demo_fragmentation();
    demo_heap_real();
}