#include <freertos/FreeRTOS.h>
#include "emisc/fancymacro.h"
#include "ecorea/threadctx.h"

extern BaseType_t __real_xTaskCreatePinnedToCore( TaskFunction_t pxTaskCode,
                                                  const char * const pcName,
                                                  const configSTACK_DEPTH_TYPE usStackDepth,
                                                  void * const pvParameters,
                                                  UBaseType_t uxPriority,
                                                  TaskHandle_t * const pvCreatedTask,
                                                  const BaseType_t xCoreID );

extern TaskHandle_t __real_xTaskCreateStaticPinnedToCore( TaskFunction_t pxTaskCode,
                                                          const char * const pcName,
                                                          const uint32_t ulStackDepth,
                                                          void * const pvParameters,
                                                          UBaseType_t uxPriority,
                                                          StackType_t * const pxStackBuffer,
                                                          StaticTask_t * const pxTaskBuffer,
                                                          const BaseType_t xCoreID );

BaseType_t __wrap_xTaskCreatePinnedToCore( TaskFunction_t pxTaskCode,
                                           const char * const pcName,
                                           const configSTACK_DEPTH_TYPE usStackDepth,
                                           void * const pvParameters,
                                           UBaseType_t uxPriority,
                                           TaskHandle_t * const pvCreatedTask,
                                           const BaseType_t xCoreID ){
  EOS_LOGW("Runing RTOS task %s: core=%d,stack=%d,priority=%d\n", pcName, xCoreID, usStackDepth, uxPriority);
  return __real_xTaskCreatePinnedToCore(pxTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pvCreatedTask, xCoreID);
}

TaskHandle_t __wrap_xTaskCreateStaticPinnedToCore( TaskFunction_t pxTaskCode,
                                                   const char * const pcName,
                                                   const uint32_t ulStackDepth,
                                                   void * const pvParameters,
                                                   UBaseType_t uxPriority,
                                                   StackType_t * const pxStackBuffer,
                                                   StaticTask_t * const pxTaskBuffer,
                                                   const BaseType_t xCoreID ){

  EOS_LOGW("Runing RTOS task %s: core=%d,stack=%d,priority=%d\n", pcName, xCoreID, ulStackDepth, uxPriority);
  return __real_xTaskCreateStaticPinnedToCore(pxTaskCode, pcName, ulStackDepth, pvParameters, uxPriority, pxStackBuffer, pxTaskBuffer, xCoreID);
}

