#include "cellular_platform.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static portMUX_TYPE cellular_socket_context_mutex;


struct PlatformMutex
{
    StaticSemaphore_t freertos_mutex_buffer;
    SemaphoreHandle_t freertos_mutex_handle;
    bool recursive;
};

struct ThreadRequest
{
    void (*fn)(void *argument);
    void *argument;
};

void Platform_Init(void)
{
    vPortCPUInitializeMutex(&cellular_socket_context_mutex);
}

void Platform_EnterCritical(void)
{
    taskENTER_CRITICAL(&cellular_socket_context_mutex);
}

void Platform_ExitCritical(void)
{
    taskENTER_CRITICAL(&cellular_socket_context_mutex);
}

static void thread_runner(void *thread_request)
{
    struct ThreadRequest *tr = thread_request;

    tr->fn(tr->argument);
    free(tr);

    // Delete our own thread
    vTaskDelete(NULL);
}

bool Platform_CreateDetachedThread(
    void (*thread_fn)(void *argument),
    void *argument,
    int32_t priority,
    size_t stack_size)
{
    struct ThreadRequest *tr = calloc(1, sizeof(*tr));
    if (tr == NULL) {
        return false;
    }
    tr->fn = thread_fn;
    tr->argument = argument;

    BaseType_t res = xTaskCreate(
        thread_runner,
        "cellular",
        (configSTACK_DEPTH_TYPE)stack_size,
        tr,
        (UBaseType_t)priority,
        NULL);

    return res == pdPASS;
}

bool PlatformMutex_Create(PlatformMutex_t *mutex, bool recursive)
{
    struct PlatformMutex *m = calloc(1, sizeof(*m));
    if (!m) {
        return false;
    }

    if (!recursive) {
        m->freertos_mutex_handle = xSemaphoreCreateMutexStatic(&m->freertos_mutex_buffer);
    } else {
        m->freertos_mutex_handle = xSemaphoreCreateRecursiveMutexStatic(&m->freertos_mutex_buffer);
    }
    *mutex = m;
    return true;
}

void PlatformMutex_Destroy(PlatformMutex_t *mutex)
{
    vSemaphoreDelete((*mutex)->freertos_mutex_handle);
    free(*mutex);
}

void PlatformMutex_Lock(PlatformMutex_t *mutex)
{
    BaseType_t res;
    if (!(*mutex)->recursive) {
        res = xSemaphoreTake((*mutex)->freertos_mutex_handle, portMAX_DELAY);
    } else {
        res = xSemaphoreTakeRecursive((*mutex)->freertos_mutex_handle, portMAX_DELAY);
    }

    configASSERT(res == pdTRUE);
}

void PlatformMutex_Unlock(PlatformMutex_t *mutex)
{
    BaseType_t res;
    if (!(*mutex)->recursive) {
        res = xSemaphoreGive((*mutex)->freertos_mutex_handle);
    } else {
        res = xSemaphoreGiveRecursive((*mutex)->freertos_mutex_handle);
    }

    configASSERT(res == pdTRUE);
}

void *Platform_Malloc(size_t requested_bytes)
{
    void *p = malloc(requested_bytes);
    configASSERT(p != NULL);
    return p;
}

void Platform_Delay(uint32_t delay_ms)
{
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}
