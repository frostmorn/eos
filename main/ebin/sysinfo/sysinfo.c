#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ecore/bin.h"

#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_system.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static void print_chip_info(void) {
  esp_chip_info_t info;
  esp_chip_info(&info);

  printf("\n== CHIP ==\n");

  printf("model: %d\n", info.model);
  printf("cores: %d\n", info.cores);

  printf("features:");
  if (info.features & CHIP_FEATURE_WIFI_BGN)
    printf(" wifi");
  if (info.features & CHIP_FEATURE_BT)
    printf(" bt");
  if (info.features & CHIP_FEATURE_BLE)
    printf(" ble");
  if (info.features & CHIP_FEATURE_EMB_FLASH)
    printf(" flash");
  if (info.features & CHIP_FEATURE_EMB_PSRAM)
    printf(" psram");
  printf("\n");

  printf("revision: %d\n", info.revision);
}

static void print_memory(void) {
  printf("\n== MEMORY ==\n");

  printf("free heap: %u\n", (unsigned)esp_get_free_heap_size());

  printf("minimum heap: %u\n", (unsigned)esp_get_minimum_free_heap_size());

#ifdef CONFIG_SPIRAM
  printf("psram total: %u\n",
         (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));

  printf("psram free: %u\n",
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif

  printf("internal RAM free: %u\n",
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

static void print_flash(void) {
  uint32_t size = 0;

  printf("\n== FLASH ==\n");

  if (esp_flash_get_size(NULL, &size) == ESP_OK)
    printf("size: %lu bytes\n", size);
}

static void print_mac(void) {
  uint8_t mac[6];

  printf("\n== MAC ==\n");

  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
    printf("wifi: %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
  }

  if (esp_read_mac(mac, ESP_MAC_BT) == ESP_OK) {
    printf("bt:   %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
  }
}

static void print_tasks(void) {
  printf("\n== TASKS ==\n");

  printf("free RTOS heap: %u\n", (unsigned)xPortGetFreeHeapSize());

  printf("tick rate: %u Hz\n", (unsigned)configTICK_RATE_HZ);
}

static void print_system(void) {
  printf("\n== SYSTEM ==\n");

  printf("sdk: %s\n", esp_get_idf_version());

  printf("reset reason: %d\n", esp_reset_reason());

  printf("uptime ticks: %u\n", (unsigned)xTaskGetTickCount());
}

int sysinfo_main(int argc, char **argv) {
  printf("\nEOS system information\n");

  print_system();
  print_chip_info();
  print_memory();
  print_flash();
  print_mac();
  print_tasks();

  printf("\n");

  return 0;
}

EOS_BIN_ATTR eos_bin_t sysinfo = {
    EOS_BIN_INITIALIZER, .filename = "sysinfo", .name = "sysinfo",
    .entry_point = sysinfo_main};
