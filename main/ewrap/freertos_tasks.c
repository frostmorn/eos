#include "ecore/bin.h"
#include "ecore/threadctx.h"
#include "emisc/fancymacro.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

extern BaseType_t __real_xTaskCreatePinnedToCore(
    TaskFunction_t pxTaskCode, const char *const pcName,
    const configSTACK_DEPTH_TYPE usStackDepth, void *const pvParameters,
    UBaseType_t uxPriority, TaskHandle_t *const pvCreatedTask,
    const BaseType_t xCoreID);

extern TaskHandle_t __real_xTaskCreateStaticPinnedToCore(
    TaskFunction_t pxTaskCode, const char *const pcName,
    const uint32_t ulStackDepth, void *const pvParameters,
    UBaseType_t uxPriority, StackType_t *const pxStackBuffer,
    StaticTask_t *const pxTaskBuffer, const BaseType_t xCoreID);

static bool should_wrap_task(const char *const pcName, uint32_t stack_depth) {
  if (stack_depth < EOS_BIN_MIN_STACK_SIZE) {
    return false;
  }
  if (!pcName) {
    return true;
  }
  // Internal ESP-IDF system tasks must not be wrapped
  if (strncmp(pcName, "ipc", 3) == 0 ||
      strncmp(pcName, "esp_", 4) == 0 ||
      strncmp(pcName, "IDLE", 4) == 0 ||
      strcmp(pcName, "tiT") == 0 ||
      strcmp(pcName, "sys_evt") == 0 ||
      strcmp(pcName, "wifi") == 0) {
    return false;
  }
  return true;
}

BaseType_t __wrap_xTaskCreatePinnedToCore(
    TaskFunction_t pxTaskCode, const char *const pcName,
    const configSTACK_DEPTH_TYPE usStackDepth, void *const pvParameters,
    UBaseType_t uxPriority, TaskHandle_t *const pvCreatedTask,
    const BaseType_t xCoreID) {

  if (!should_wrap_task(pcName, (uint32_t)usStackDepth) ||
      pxTaskCode == eos_twrap_freertos ||
      (void *)pxTaskCode == (void *)eos_twrap_pthread) {
    return __real_xTaskCreatePinnedToCore(pxTaskCode, pcName, usStackDepth,
                                          pvParameters, uxPriority,
                                          pvCreatedTask, xCoreID);
  }

  eos_twrap_t *twrap = eos_twrap_prepare(pxTaskCode, pvParameters);
  if (!twrap) {
    EOS_LOGE("Failed to allocate thread wrap data\n");
    return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
  }

  EOS_LOGW("Runing RTOS task %s: core=%d,stack=%d,priority=%d\n",
           pcName ? pcName : "unnamed", (int)xCoreID, (int)usStackDepth,
           (int)uxPriority);
  BaseType_t rc =
      __real_xTaskCreatePinnedToCore(eos_twrap_freertos, pcName, usStackDepth,
                                     twrap, uxPriority, pvCreatedTask, xCoreID);

  if (rc != pdPASS) {
    if (twrap->env_copy) {
      for (size_t i = 0; i < twrap->env_count; i++) {
        free(twrap->env_copy[i]);
      }
      free(twrap->env_copy);
    }
    free(twrap);
  }

  return rc;
}

TaskHandle_t __wrap_xTaskCreateStaticPinnedToCore(
    TaskFunction_t pxTaskCode, const char *const pcName,
    const uint32_t ulStackDepth, void *const pvParameters,
    UBaseType_t uxPriority, StackType_t *const pxStackBuffer,
    StaticTask_t *const pxTaskBuffer, const BaseType_t xCoreID) {

  if (!should_wrap_task(pcName, ulStackDepth) ||
      pxTaskCode == eos_twrap_freertos ||
      (void *)pxTaskCode == (void *)eos_twrap_pthread) {
    return __real_xTaskCreateStaticPinnedToCore(
        pxTaskCode, pcName, ulStackDepth, pvParameters, uxPriority,
        pxStackBuffer, pxTaskBuffer, xCoreID);
  }

  eos_twrap_t *twrap = eos_twrap_prepare(pxTaskCode, pvParameters);
  if (!twrap) {
    EOS_LOGE("Failed to allocate thread wrap data\n");
    return NULL;
  }

  EOS_LOGW("Runing RTOS task %s: core=%d,stack=%d,priority=%d\n",
           pcName ? pcName : "unnamed", (int)xCoreID, (int)ulStackDepth,
           (int)uxPriority);
  TaskHandle_t handle = __real_xTaskCreateStaticPinnedToCore(
      eos_twrap_freertos, pcName, ulStackDepth, twrap, uxPriority,
      pxStackBuffer, pxTaskBuffer, xCoreID);

  if (!handle) {
    if (twrap->env_copy) {
      for (size_t i = 0; i < twrap->env_count; i++) {
        free(twrap->env_copy[i]);
      }
      free(twrap->env_copy);
    }
    free(twrap);
  }

  return handle;
}
