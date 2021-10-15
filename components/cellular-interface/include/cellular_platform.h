#ifndef CELLULAR_PLATFORM_H
#define CELLULAR_PLATFORM_H

#include <stdbool.h>

// The "platform" abstraction seems to require the FreeRTOS queue functions exactly as they already
// exist, so just include the appropriate header.
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
// The PlatformEventGroup_* functions required by the platform abstraction seem to be a rename of
// the FreeRTOS EventGroup functions
#include "freertos/event_groups.h"
#include "freertos/task.h"

/*
 * It's unclear what needs to be declared in this file, so things have just been added incrementally
 * to satisfy compilation errors.
 */

// Initialization
void Platform_Init(void);

// Threads

// This priority was chosen arbitrarily
#define PLATFORM_THREAD_DEFAULT_PRIORITY (tskIDLE_PRIORITY + 2)
// This value is passed straight through to xTaskCreate, so it's in not in bytes, but rather in
// StackType_t which I believe is the word size of the machine (so 4 bytes for a 32-bit arch).
#define PLATFORM_THREAD_DEFAULT_STACK_SIZE (8192U / sizeof(StackType_t))

bool Platform_CreateDetachedThread(
    void (*thread_fn)(void *argument),
    void *argument,
    int32_t priority,
    size_t stack_size);

// Mutexes
typedef struct PlatformMutex* PlatformMutex_t;
bool PlatformMutex_Create(PlatformMutex_t *mutex, bool recursive);
void PlatformMutex_Destroy(PlatformMutex_t *mutex);
void PlatformMutex_Lock(PlatformMutex_t *mutex);
void PlatformMutex_Unlock(PlatformMutex_t *mutex);

// Event Groups
typedef EventGroupHandle_t PlatformEventGroupHandle_t;
typedef EventBits_t PlatformEventGroup_EventBits;
#define PlatformEventGroup_Create xEventGroupCreate
#define PlatformEventGroup_Delete vEventGroupDelete
#define PlatformEventGroup_SetBitsFromISR xEventGroupSetBitsFromISR
#define PlatformEventGroup_SetBits xEventGroupSetBits
#define PlatformEventGroup_ClearBits xEventGroupClearBits
#define PlatformEventGroup_GetBits xEventGroupGetBits
#define PlatformEventGroup_WaitBits xEventGroupWaitBits

// Heap
void *Platform_Malloc(size_t requested_bytes);
#define Platform_Free free

// Misc.
void Platform_Delay(uint32_t delay_ms);
#define PlatformTickType TickType_t

void Platform_EnterCritical(void);
void Platform_ExitCritical(void);

#endif // CELLULAR_PLATFORM_H
