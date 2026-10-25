#include "ecore/bin.h"
#include <esp_heap_caps.h>
#include <stdio.h>

static void dump_cap_row(const char *label, uint32_t caps) {
  size_t total = heap_caps_get_total_size(caps);
  size_t free_sz = heap_caps_get_free_size(caps);
  size_t min_free = heap_caps_get_minimum_free_size(caps);
  size_t largest = heap_caps_get_largest_free_block(caps);
  printf("%-10s total:%8u  free:%8u  min:%8u  largest:%8u\n", label,
         (unsigned)total, (unsigned)free_sz, (unsigned)min_free,
         (unsigned)largest);
}

int heapdump_main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  printf("== HEAP SUMMARY ==\n");
  dump_cap_row("INTERNAL", MALLOC_CAP_INTERNAL);
  dump_cap_row("DMA", MALLOC_CAP_DMA);
#ifdef MALLOC_CAP_SPIRAM
  dump_cap_row("SPIRAM", MALLOC_CAP_SPIRAM);
#endif
  dump_cap_row("DEFAULT", MALLOC_CAP_DEFAULT);
  fflush(stdout);

  heap_caps_dump_all();
  return 0;
}

EOS_BIN_ATTR eos_bin_t heapdump_app = {
    EOS_BIN_INITIALIZER, .filename = "heapdump", .name = "heapdump",
    .entry_point = heapdump_main};
