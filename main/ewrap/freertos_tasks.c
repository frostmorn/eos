/*

Should work, but it doesn't. Save for history.
In case you would like to try that again, you have to add 

"-Wl,--wrap=xTaskCreateStaticPinnedToCore"
"-Wl,--wrap=xTaskCreatePinnedToCore"

in target_link_libraries section of CMakeLists.txt

#include "ecore/threadctx.h"
#include "emisc/fancymacro.h"
#include <freertos/FreeRTOS.h>

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

BaseType_t __wrap_xTaskCreatePinnedToCore(
    TaskFunction_t pxTaskCode, const char *const pcName,
    const configSTACK_DEPTH_TYPE usStackDepth, void *const pvParameters,
    UBaseType_t uxPriority, TaskHandle_t *const pvCreatedTask,
    const BaseType_t xCoreID) {

  eos_twrap_t *twrap = eos_twrap_prepare(pxTaskCode, pvParameters);
  if (!twrap) {
    EOS_LOGE("Failed to allocate thread wrap data\n");
    return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
  }

  EOS_LOGW("Runing RTOS task %s: core=%d,stack=%d,priority=%d\n", pcName,
           xCoreID, usStackDepth, uxPriority);
  BaseType_t rc =
      __real_xTaskCreatePinnedToCore(eos_twrap_freertos, pcName, usStackDepth,
                                     twrap, uxPriority, pvCreatedTask, xCoreID);

  if (rc != pdPASS)
    free(twrap);

  return rc;
}

TaskHandle_t __wrap_xTaskCreateStaticPinnedToCore(
    TaskFunction_t pxTaskCode, const char *const pcName,
    const uint32_t ulStackDepth, void *const pvParameters,
    UBaseType_t uxPriority, StackType_t *const pxStackBuffer,
    StaticTask_t *const pxTaskBuffer, const BaseType_t xCoreID) {

  eos_twrap_t *twrap = eos_twrap_prepare(pxTaskCode, pvParameters);
  if (!twrap) {
    EOS_LOGE("Failed to allocate thread wrap data\n");
    return NULL;
  }

  EOS_LOGW("Runing RTOS task %s: core=%d,stack=%d,priority=%d\n", pcName,
           xCoreID, ulStackDepth, uxPriority);
  TaskHandle_t handle = __real_xTaskCreateStaticPinnedToCore(
      eos_twrap_freertos, pcName, ulStackDepth, twrap, uxPriority,
      pxStackBuffer, pxTaskBuffer, xCoreID);

  if (!handle)
    free(twrap);

  return handle;
}

*/