#include "ecore/app.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static SemaphoreHandle_t mutex;
static SemaphoreHandle_t completion;
static SemaphoreHandle_t condition;

static volatile int counter;
static volatile int ready;

static void hello_task(void *arg) {
  printf("hello task: arg=%d\n", (int)(intptr_t)arg);

  /*
   * FreeRTOS tasks don't return a value.  Signal completion instead.
   */
  xSemaphoreGive(completion);

  vTaskDelete(NULL);
}

static void counter_task(void *arg) {
  int loops = (int)(intptr_t)arg;

  for (int i = 0; i < loops; i++) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    counter++;
    xSemaphoreGive(mutex);
  }

  xSemaphoreGive(completion);
  vTaskDelete(NULL);
}

static void condition_waiter_task(void *arg) {
  (void)arg;

  /*
   * A binary semaphore is the closest simple FreeRTOS equivalent
   * to the condition-variable usage in test_pthread.
   */
  xSemaphoreTake(condition, portMAX_DELAY);

  printf("condition signaled\n");

  xSemaphoreGive(completion);
  vTaskDelete(NULL);
}

static void sleep_task(void *arg) {
  (void)arg;

  for (int i = 0; i < 5; i++) {
    printf("delay %d\n", i);
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  xSemaphoreGive(completion);
  vTaskDelete(NULL);
}

static void detached_task(void *arg) {
  (void)arg;

  printf("detached task started\n");

  vTaskDelay(pdMS_TO_TICKS(1000));

  printf("detached task finished\n");

  vTaskDelete(NULL);
}

static void priority_task(void *arg) {
  const char *name = (const char *)arg;

  printf("priority task '%s': priority=%lu\n", name,
         (unsigned long)uxTaskPriorityGet(NULL));

  xSemaphoreGive(completion);
  vTaskDelete(NULL);
}

static void notification_task(void *arg) {
  (void)arg;

  uint32_t value = 0;

  printf("notification task waiting\n");

  printf("Testing lost dirs/fds\n");

  opendir("/");
  fopen("/bin/test_rtos_tasks", "r");

  if (xTaskNotifyWait(0, UINT32_MAX, &value, pdMS_TO_TICKS(2000)) == pdTRUE) {

    printf("notification value=%lu\n", (unsigned long)value);
  } else {
    printf("notification timeout\n");
  }

  xSemaphoreGive(completion);
  vTaskDelete(NULL);
}

static int wait_for_task(void) {
  return xSemaphoreTake(completion, pdMS_TO_TICKS(5000)) == pdTRUE;
}

static int test_create(void) {
  TaskHandle_t task = NULL;

  puts("== create/wait ==");

  if (xTaskCreate(hello_task, "rtos_hello", 2048, (void *)42,
                  tskIDLE_PRIORITY + 1, &task) != pdPASS) {

    puts("xTaskCreate failed");
    return -1;
  }

  if (!wait_for_task()) {
    puts("task completion timeout");
    return -1;
  }

  printf("task=%p completed\n", (void *)task);

  return 0;
}

static int test_mutex(void) {
  TaskHandle_t a;
  TaskHandle_t b;

  puts("");
  puts("== mutex ==");

  counter = 0;

  if (xTaskCreate(counter_task, "counter_a", 2048, (void *)100000,
                  tskIDLE_PRIORITY + 1, &a) != pdPASS) {

    puts("counter task A creation failed");
    return -1;
  }

  if (xTaskCreate(counter_task, "counter_b", 2048, (void *)100000,
                  tskIDLE_PRIORITY + 1, &b) != pdPASS) {

    puts("counter task B creation failed");
    return -1;
  }

  if (!wait_for_task() || !wait_for_task()) {
    puts("counter task timeout");
    return -1;
  }

  printf("counter=%d (expect 200000)\n", counter);

  return counter == 200000 ? 0 : -1;
}

static int test_condition(void) {
  TaskHandle_t task;

  puts("");
  puts("== condition ==");

  ready = 0;

  if (xTaskCreate(condition_waiter_task, "condition", 2048, NULL,
                  tskIDLE_PRIORITY + 1, &task) != pdPASS) {

    puts("condition task creation failed");
    return -1;
  }

  vTaskDelay(pdMS_TO_TICKS(100));

  ready = 1;
  xSemaphoreGive(condition);

  if (!wait_for_task()) {
    puts("condition task timeout");
    return -1;
  }

  return 0;
}

static int test_detached(void) {
  puts("");
  puts("== detached ==");

  /*
   * Unlike pthreads, FreeRTOS doesn't have a detached/joinable
   * distinction. A task that deletes itself is effectively
   * independent of its creator.
   */
  if (xTaskCreate(detached_task, "detached", 2048, NULL, tskIDLE_PRIORITY + 1,
                  NULL) != pdPASS) {

    puts("detached task creation failed");
    return -1;
  }

  vTaskDelay(pdMS_TO_TICKS(1500));

  return 0;
}

static int test_delay(void) {
  TaskHandle_t task;

  puts("");
  puts("== vTaskDelay ==");

  if (xTaskCreate(sleep_task, "sleep", 2048, NULL, tskIDLE_PRIORITY + 1,
                  &task) != pdPASS) {

    puts("sleep task creation failed");
    return -1;
  }

  if (!wait_for_task()) {
    puts("sleep task timeout");
    return -1;
  }

  return 0;
}

static int test_priority(void) {
  TaskHandle_t low;
  TaskHandle_t high;

  puts("");
  puts("== priority ==");

  if (xTaskCreate(priority_task, "priority_low", 2048, "low",
                  tskIDLE_PRIORITY + 1, &low) != pdPASS) {

    puts("low priority task creation failed");
    return -1;
  }

  if (xTaskCreate(priority_task, "priority_high", 2048, "high",
                  tskIDLE_PRIORITY + 3, &high) != pdPASS) {

    puts("high priority task creation failed");
    return -1;
  }

  if (!wait_for_task() || !wait_for_task()) {
    puts("priority task timeout");
    return -1;
  }

  return 0;
}

static int test_notification(void) {
  TaskHandle_t task;

  puts("");
  puts("== task notification ==");

  if (xTaskCreate(notification_task, "notification", 2048, NULL,
                  tskIDLE_PRIORITY + 1, &task) != pdPASS) {

    puts("notification task creation failed");
    return -1;
  }

  vTaskDelay(pdMS_TO_TICKS(100));

  xTaskNotify(task, 1234, eSetValueWithOverwrite);

  if (!wait_for_task()) {
    puts("notification task timeout");
    return -1;
  }

  return 0;
}

int test_rtos_tasks_main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  mutex = xSemaphoreCreateMutex();
  completion = xSemaphoreCreateBinary();
  condition = xSemaphoreCreateBinary();

  if (mutex == NULL || completion == NULL || condition == NULL) {
    puts("failed to create synchronization objects");

    if (mutex != NULL) {
      vSemaphoreDelete(mutex);
    }

    if (completion != NULL) {
      vSemaphoreDelete(completion);
    }

    if (condition != NULL) {
      vSemaphoreDelete(condition);
    }

    return 1;
  }

  int result = 0;

  if (test_create() != 0) {
    result = 1;
  }

  if (test_mutex() != 0) {
    result = 1;
  }

  if (test_condition() != 0) {
    result = 1;
  }

  if (test_detached() != 0) {
    result = 1;
  }

  if (test_delay() != 0) {
    result = 1;
  }

  if (test_priority() != 0) {
    result = 1;
  }

  if (test_notification() != 0) {
    result = 1;
  }

  vSemaphoreDelete(condition);
  vSemaphoreDelete(completion);
  vSemaphoreDelete(mutex);

  puts("");
  puts(result == 0 ? "All RTOS task tests completed."
                   : "RTOS task tests FAILED.");

  return result;
}

EOS_NATIVE_APP_ATTR eos_native_app_manifest_t test_rtos_tasks = {
    EOS_NATIVE_APP_INIT, .filename = "test_rtos_tasks",
    .name = "test_rtos_tasks", .entry_point = test_rtos_tasks_main};