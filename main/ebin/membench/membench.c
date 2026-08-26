#include "ecore/bin.h"
#include "emisc/fancymacro.h"

#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ── Config ────────────────────────────────────────────────────

static const size_t block_sizes[] = {64, 256, 1024, 4096, 16384, 65536};

static const struct {
  const char *name;
  uint32_t caps;
} mem_types[] = {
    {"DRAM", MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT},
    {"PSRAM", MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT},
    {"DRAM DMA", MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA},
};

#define BENCH_DURATION_US 500000
#define MB (1024.0f * 1024.0f)

// ── Table formatting ──────────────────────────────────────────

static void print_table_line(void) {
  printf("+----------+--------------+--------------+--------------+\n");
}

static void print_table_header(void) {
  print_table_line();
  printf("|  block   |  write MB/s  |   read MB/s  |   copy MB/s  |\n");
  print_table_line();
}

static void print_copy_table_line(void) {
  printf("+----------+--------------+\n");
}

static void print_copy_table_header(void) {
  print_copy_table_line();
  printf("|  block   |   copy MB/s  |\n");
  print_copy_table_line();
}

static void print_block(size_t block) {
  if (block < 1024)
    printf("| %6u B ", (unsigned)block);
  else
    printf("| %5u KB ", (unsigned)(block / 1024));
}

// ── Benchmark routines ────────────────────────────────────────

static float bench_write(void *buf, size_t block) {
  uint32_t *p = (uint32_t *)buf;
  size_t words = block / sizeof(uint32_t);

  int64_t start = esp_timer_get_time();
  int64_t end = start + BENCH_DURATION_US;

  uint64_t bytes = 0;
  uint32_t value = 0xAAAAAAAA;

  while (esp_timer_get_time() < end) {

    for (size_t i = 0; i < words; i++)
      p[i] = value;

    bytes += words * sizeof(uint32_t);
    value += 0x11111111;
  }

  int64_t elapsed = esp_timer_get_time() - start;

  vTaskDelay(pdMS_TO_TICKS(100));

  return (float)bytes / (elapsed / 1e6f) / MB;
}

static float bench_read(void *buf, size_t block) {
  const uint32_t *p = (const uint32_t *)buf;

  size_t words = block / sizeof(uint32_t);

  volatile uint32_t sink;

  int64_t start = esp_timer_get_time();
  int64_t end = start + BENCH_DURATION_US;

  uint64_t bytes = 0;

  while (esp_timer_get_time() < end) {

    uint32_t s0 = 0;
    uint32_t s1 = 0;
    uint32_t s2 = 0;
    uint32_t s3 = 0;

    size_t i = 0;

    for (; i + 3 < words; i += 4) {
      s0 += p[i + 0];
      s1 += p[i + 1];
      s2 += p[i + 2];
      s3 += p[i + 3];
    }

    for (; i < words; i++)
      s0 += p[i];

    sink = s0 + s1 + s2 + s3;

    bytes += words * sizeof(uint32_t);
  }

  (void)sink;

  int64_t elapsed = esp_timer_get_time() - start;

  vTaskDelay(pdMS_TO_TICKS(100));

  return (float)bytes / (elapsed / 1e6f) / MB;
}

static float bench_copy(void *dst, void *src, size_t block) {
  uint32_t *d = (uint32_t *)dst;
  const uint32_t *s = (const uint32_t *)src;

  size_t words = block / sizeof(uint32_t);

  int64_t start = esp_timer_get_time();
  int64_t end = start + BENCH_DURATION_US;

  uint64_t bytes = 0;

  while (esp_timer_get_time() < end) {

    for (size_t i = 0; i < words; i++)
      d[i] = s[i];

    bytes += words * sizeof(uint32_t);
  }

  int64_t elapsed = esp_timer_get_time() - start;

  vTaskDelay(pdMS_TO_TICKS(100));

  return (float)bytes / (elapsed / 1e6f) / MB;
}

// ── Main ──────────────────────────────────────────────────────

static int membench_main(int argc, char **argv) {
  printf("\n");
  printf("===========================================================\n");
  printf("*                 EOS Memory Benchmark                    *\n");
  printf("===========================================================\n\n");

  size_t max_block = block_sizes[EOS_ARR_COUNT(block_sizes) - 1];

  for (size_t t = 0; t < EOS_ARR_COUNT(mem_types); t++) {

    uint32_t caps = mem_types[t].caps;

    if (heap_caps_get_free_size(caps) < max_block * 2) {

      printf("[ %-8s ] - not available or insufficient memory\n\n",
             mem_types[t].name);

      continue;
    }

    printf("** %s\n\n", mem_types[t].name);

    print_table_header();

    for (size_t b = 0; b < EOS_ARR_COUNT(block_sizes); b++) {

      size_t block = block_sizes[b];

      void *buf1 = heap_caps_malloc(block, caps);
      void *buf2 = heap_caps_malloc(block, caps);

      if (!buf1 || !buf2) {

        printf("| %8u | %12s | %12s | %12s |\n", (unsigned)block, "oom", "oom",
               "oom");

        heap_caps_free(buf1);
        heap_caps_free(buf2);

        continue;
      }

      memset(buf1, 0xAA, block);
      memset(buf2, 0x55, block);

      float w = bench_write(buf1, block);
      float r = bench_read(buf1, block);
      float c = bench_copy(buf2, buf1, block);

      print_block(block);
      printf("| %12.2f | %12.2f | %12.2f |\n", w, r, c);

      heap_caps_free(buf1);
      heap_caps_free(buf2);
    }

    print_table_line();
    printf("\n");
  }

  // ── DRAM → PSRAM copy test ──────────────────────────────────

  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) > 4096 &&
      heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) > 4096) {

    printf("** DRAM -> PSRAM copy\n\n");

    print_copy_table_header();

    for (size_t b = 0; b < EOS_ARR_COUNT(block_sizes); b++) {

      size_t block = block_sizes[b];

      void *dram =
          heap_caps_malloc(block, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

      void *psram =
          heap_caps_malloc(block, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

      if (!dram || !psram) {

        heap_caps_free(dram);
        heap_caps_free(psram);

        continue;
      }

      memset(dram, 0xAA, block);

      float c = bench_copy(psram, dram, block);

      print_block(block);
      printf("| %12.2f |\n", c);

      heap_caps_free(dram);
      heap_caps_free(psram);
    }

    print_copy_table_line();

    printf("\n");
  }

  printf("Benchmark complete.\n\n");

  return 0;
}

// ── Application manifest ──────────────────────────────────────

EOS_BIN_ATTR const eos_bin_t membench_app = {
    EOS_BIN_INITIALIZER,
    .filename = "membench",
    .name = "Memory Benchmark",
    .group = "Tools",
    .description = "Read/write/copy speed across DRAM, PSRAM, DMA memory at "
                   "various block sizes",
    .entry_point = membench_main,
};