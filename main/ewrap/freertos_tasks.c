#include "ecore/bin.h"
#include "ecore/proc.h"
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
      pxTaskCode == eos_proc_wrap_freertos ||
      (void *)pxTaskCode == (void *)eos_proc_wrap_pthread) {
    return __real_xTaskCreatePinnedToCore(pxTaskCode, pcName, usStackDepth,
                                          pvParameters, uxPriority,
                                          pvCreatedTask, xCoreID);
  }

  eos_pwrap_t *pwrap = eos_proc_wrap_prepare((void *)pxTaskCode, pvParameters);
  if (!pwrap) {
    EOS_LOGE("Failed to allocate thread wrap data\n");
    return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
  }

  EOS_LOGW("Runing RTOS task %s: core=%d,stack=%d,priority=%d\n",
           pcName ? pcName : "unnamed", (int)xCoreID, (int)usStackDepth,
           (int)uxPriority);
  BaseType_t rc =
      __real_xTaskCreatePinnedToCore(eos_proc_wrap_freertos, pcName, usStackDepth,
                                     pwrap, uxPriority, pvCreatedTask, xCoreID);

  if (rc != pdPASS) {
    free(pwrap);
  }

  return rc;
}

TaskHandle_t __wrap_xTaskCreateStaticPinnedToCore(
    TaskFunction_t pxTaskCode, const char *const pcName,
    const uint32_t ulStackDepth, void *const pvParameters,
    UBaseType_t uxPriority, StackType_t *const pxStackBuffer,
    StaticTask_t *const pxTaskBuffer, const BaseType_t xCoreID) {

  if (!should_wrap_task(pcName, ulStackDepth) ||
      pxTaskCode == eos_proc_wrap_freertos ||
      (void *)pxTaskCode == (void *)eos_proc_wrap_pthread) {
    return __real_xTaskCreateStaticPinnedToCore(
        pxTaskCode, pcName, ulStackDepth, pvParameters, uxPriority,
        pxStackBuffer, pxTaskBuffer, xCoreID);
  }

  eos_pwrap_t *pwrap = eos_proc_wrap_prepare((void *)pxTaskCode, pvParameters);
  if (!pwrap) {
    EOS_LOGE("Failed to allocate thread wrap data\n");
    return NULL;
  }

  EOS_LOGW("Runing RTOS task %s: core=%d,stack=%d,priority=%d\n",
           pcName ? pcName : "unnamed", (int)xCoreID, (int)ulStackDepth,
           (int)uxPriority);
  TaskHandle_t handle = __real_xTaskCreateStaticPinnedToCore(
      eos_proc_wrap_freertos, pcName, ulStackDepth, pwrap, uxPriority,
      pxStackBuffer, pxTaskBuffer, xCoreID);

  if (!handle) {
    free(pwrap);
  }

  return handle;
}
