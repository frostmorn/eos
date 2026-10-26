#include "procfs.h"
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/log.h"
#include <dirent.h>
#include <driver/gpio.h>
#include <errno.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_intr_alloc.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_vfs.h>
#include <fcntl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define EOS_PROCFS_MAX_FDS 8

typedef struct {
  char *data;
  size_t size;
  off_t offset;
  bool in_use;
} procfs_fd_t;

typedef struct {
  uint32_t esp_idf_fs_index; // Must be first field for ESP-IDF VFS
  uint32_t index;
  struct dirent entry;
} procfs_dir_t;

static procfs_fd_t s_procfs_fds[EOS_PROCFS_MAX_FDS];

static const char *s_proc_entries[] = {
    ".",
    "..",
    "scopes",
    "drivers",
    "devices",
    "meminfo",
    "version",
    "mounts",
    "interrupts",
    "fds",
    "gpio",
    "heapinfo",
    "cpuinfo",
    "uptime",
};
#define PROC_ENTRY_COUNT (sizeof(s_proc_entries) / sizeof(s_proc_entries[0]))

// Dynamic string accumulator
typedef struct {
  char *buf;
  size_t cap;
  size_t len;
} sbuf_t;

static void sbuf_init(sbuf_t *s) {
  s->cap = 256;
  s->len = 0;
  s->buf = malloc(s->cap);
  if (s->buf) s->buf[0] = '\0';
}

static void sbuf_printf(sbuf_t *s, const char *fmt, ...) {
  if (!s->buf) return;
  va_list args;
  va_start(args, fmt);
  va_list args_copy;
  va_copy(args_copy, args);
  int needed = vsnprintf(NULL, 0, fmt, args_copy);
  va_end(args_copy);
  if (needed < 0) {
    va_end(args);
    return;
  }
  if (s->len + (size_t)needed + 1 > s->cap) {
    size_t new_cap = s->cap * 2;
    if (new_cap < s->len + (size_t)needed + 1)
      new_cap = s->len + (size_t)needed + 64;
    char *new_buf = realloc(s->buf, new_cap);
    if (!new_buf) {
      va_end(args);
      return;
    }
    s->buf = new_buf;
    s->cap = new_cap;
  }
  vsnprintf(s->buf + s->len, s->cap - s->len, fmt, args);
  s->len += (size_t)needed;
  va_end(args);
}

// ── Content Generators ─────────────────────────────────────────

static char *procfs_gen_scopes(void) {
  sbuf_t sb;
  sbuf_init(&sb);
  if (!sb.buf) return NULL;

  char seen[32][EOS_XSMALL_STR_LEN];
  size_t seen_count = 0;

  for (const eos_drv_t *drv = _eos_drivers_start; drv < _eos_drivers_end; ++drv) {
    if (drv->scope[0] == '\0') continue;
    bool already = false;
    for (size_t i = 0; i < seen_count; i++) {
      if (strcmp(seen[i], drv->scope) == 0) {
        already = true;
        break;
      }
    }
    if (!already && seen_count < 32) {
      strncpy(seen[seen_count], drv->scope, EOS_XSMALL_STR_LEN - 1);
      seen[seen_count][EOS_XSMALL_STR_LEN - 1] = '\0';
      seen_count++;
    }
  }

  // Bubble sort scopes alphabetically
  for (size_t i = 0; i < seen_count; i++) {
    for (size_t j = i + 1; j < seen_count; j++) {
      if (strcmp(seen[i], seen[j]) > 0) {
        char tmp[EOS_XSMALL_STR_LEN];
        memcpy(tmp, seen[i], sizeof(tmp));
        memcpy(seen[i], seen[j], sizeof(tmp));
        memcpy(seen[j], tmp, sizeof(tmp));
      }
    }
  }

  for (size_t i = 0; i < seen_count; i++) {
    sbuf_printf(&sb, "%s\n", seen[i]);
  }

  return sb.buf;
}

static char *procfs_gen_drivers(void) {
  sbuf_t sb;
  sbuf_init(&sb);
  if (!sb.buf) return NULL;

  sbuf_printf(&sb, "%-12s %-14s %-12s %s\n", "SCOPE", "NAME", "DEVNAME", "FLAGS");
  for (const eos_drv_t *drv = _eos_drivers_start; drv < _eos_drivers_end; ++drv) {
    sbuf_printf(&sb, "%-12s %-14s %-12s 0x%08" PRIx32 "\n",
                drv->scope, drv->name, drv->devname[0] ? drv->devname : "-", drv->flags);
  }

  return sb.buf;
}

static char *procfs_gen_devices(void) {
  sbuf_t sb;
  sbuf_init(&sb);
  if (!sb.buf) return NULL;

  sbuf_printf(&sb, "%-4s %-14s %-20s %s\n", "ID", "NAME", "DRIVER", "PARENT");
  for (size_t i = 0; i < EOS_MAX_DEVICES; i++) {
    eos_dev_t *dev = &eos_devices[i];
    if (!dev->in_use) continue;

    char drv_str[48];
    if (dev->driver) {
      snprintf(drv_str, sizeof(drv_str), "%s/%s", dev->driver->scope, dev->driver->name);
    } else {
      strncpy(drv_str, "none", sizeof(drv_str));
    }

    const char *parent_name = dev->parent ? dev->parent->name : "-";
    if (parent_name[0] == '\0') parent_name = "root";

    sbuf_printf(&sb, "%-4" PRIu32 " %-14s %-20s %s\n",
                dev->id, dev->name[0] ? dev->name : "(unnamed)", drv_str, parent_name);
  }

  return sb.buf;
}

static char *procfs_gen_meminfo(void) {
  sbuf_t sb;
  sbuf_init(&sb);
  if (!sb.buf) return NULL;

  uint32_t free_heap = esp_get_free_heap_size();
  uint32_t min_free = esp_get_minimum_free_heap_size();
  uint32_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  uint32_t spi_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

  sbuf_printf(&sb, "MemFree:         %8" PRIu32 " kB\n", free_heap / 1024);
  sbuf_printf(&sb, "MemMinimum:      %8" PRIu32 " kB\n", min_free / 1024);
  sbuf_printf(&sb, "InternalFree:    %8" PRIu32 " kB\n", int_free / 1024);
  sbuf_printf(&sb, "SPIRAMFree:      %8" PRIu32 " kB\n", spi_free / 1024);

  return sb.buf;
}

static char *procfs_gen_version(void) {
  sbuf_t sb;
  sbuf_init(&sb);
  if (!sb.buf) return NULL;

  sbuf_printf(&sb, "EOS 1.0.0 (IDF %s, built %s %s)\n",
              esp_get_idf_version(), __DATE__, __TIME__);

  return sb.buf;
}

static char *procfs_gen_mounts(void) {
  char *buf = NULL;
  size_t sz = 0;
  FILE *f = open_memstream(&buf, &sz);
  if (!f) return NULL;
  esp_vfs_dump_registered_paths(f);
  fclose(f);
  return buf;
}

static char *procfs_gen_interrupts(void) {
  char *buf = NULL;
  size_t sz = 0;
  FILE *f = open_memstream(&buf, &sz);
  if (!f) return NULL;
  esp_intr_dump(f);
  fclose(f);
  return buf;
}

static char *procfs_gen_fds(void) {
  char *buf = NULL;
  size_t sz = 0;
  FILE *f = open_memstream(&buf, &sz);
  if (!f) return NULL;
  esp_vfs_dump_fds(f);
  fclose(f);
  return buf;
}

static char *procfs_gen_gpio(void) {
  char *buf = NULL;
  size_t sz = 0;
  FILE *f = open_memstream(&buf, &sz);
  if (!f) return NULL;
  gpio_dump_io_configuration(f, SOC_GPIO_VALID_GPIO_MASK);
  fclose(f);
  return buf;
}

static void procfs_dump_cap_row(sbuf_t *sb, const char *label, uint32_t caps) {
  size_t total = heap_caps_get_total_size(caps);
  size_t free_sz = heap_caps_get_free_size(caps);
  size_t min_free = heap_caps_get_minimum_free_size(caps);
  size_t largest = heap_caps_get_largest_free_block(caps);
  sbuf_printf(sb, "%-10s total:%8u  free:%8u  min:%8u  largest:%8u\n", label,
              (unsigned)total, (unsigned)free_sz, (unsigned)min_free,
              (unsigned)largest);
}

static char *procfs_gen_heapinfo(void) {
  sbuf_t sb;
  sbuf_init(&sb);
  if (!sb.buf) return NULL;

  sbuf_printf(&sb, "== HEAP SUMMARY ==\n");
  procfs_dump_cap_row(&sb, "INTERNAL", MALLOC_CAP_INTERNAL);
  procfs_dump_cap_row(&sb, "DMA", MALLOC_CAP_DMA);
#ifdef MALLOC_CAP_SPIRAM
  procfs_dump_cap_row(&sb, "SPIRAM", MALLOC_CAP_SPIRAM);
#endif
  procfs_dump_cap_row(&sb, "DEFAULT", MALLOC_CAP_DEFAULT);

  return sb.buf;
}

static const char *procfs_chip_model_name(esp_chip_model_t model) {
  switch (model) {
    case CHIP_ESP32: return "ESP32";
    case CHIP_ESP32S2: return "ESP32-S2";
    case CHIP_ESP32S3: return "ESP32-S3";
    case CHIP_ESP32C3: return "ESP32-C3";
    case CHIP_ESP32C2: return "ESP32-C2";
    case CHIP_ESP32C6: return "ESP32-C6";
    case CHIP_ESP32H2: return "ESP32-H2";
    case CHIP_ESP32P4: return "ESP32-P4";
    default: return "ESP32";
  }
}

static char *procfs_gen_cpuinfo(void) {
  sbuf_t sb;
  sbuf_init(&sb);
  if (!sb.buf) return NULL;

  esp_chip_info_t info;
  esp_chip_info(&info);

  sbuf_printf(&sb, "processor\t: 0\n");
  sbuf_printf(&sb, "model name\t: %s (model %d, rev %d)\n",
              procfs_chip_model_name(info.model), (int)info.model, (int)info.revision);
  sbuf_printf(&sb, "cpu cores\t: %d\n", (int)info.cores);

  sbuf_printf(&sb, "features\t:");
  if (info.features & CHIP_FEATURE_WIFI_BGN) sbuf_printf(&sb, " wifi");
  if (info.features & CHIP_FEATURE_BT) sbuf_printf(&sb, " bt");
  if (info.features & CHIP_FEATURE_BLE) sbuf_printf(&sb, " ble");
  if (info.features & CHIP_FEATURE_EMB_FLASH) sbuf_printf(&sb, " flash");
  if (info.features & CHIP_FEATURE_EMB_PSRAM) sbuf_printf(&sb, " psram");
  sbuf_printf(&sb, "\n");

  uint32_t flash_sz = 0;
  if (esp_flash_get_size(NULL, &flash_sz) == ESP_OK) {
    sbuf_printf(&sb, "flash size\t: %" PRIu32 " bytes\n", flash_sz);
  }

  uint8_t mac[6];
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
    sbuf_printf(&sb, "mac (wifi)\t: %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }
  if (esp_read_mac(mac, ESP_MAC_BT) == ESP_OK) {
    sbuf_printf(&sb, "mac (bt)\t: %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }

  return sb.buf;
}

static const char *procfs_reset_reason_name(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external-pin";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt-wdt";
    case ESP_RST_TASK_WDT: return "task-wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    case ESP_RST_USB: return "usb";
    case ESP_RST_JTAG: return "jtag";
    case ESP_RST_EFUSE: return "efuse";
    case ESP_RST_PWR_GLITCH: return "pwr-glitch";
    case ESP_RST_CPU_LOCKUP: return "cpu-lockup";
    default: return "unknown";
  }
}

static char *procfs_gen_uptime(void) {
  sbuf_t sb;
  sbuf_init(&sb);
  if (!sb.buf) return NULL;

  uint32_t ticks = (uint32_t)xTaskGetTickCount();
  uint32_t hz = (uint32_t)configTICK_RATE_HZ;
  uint32_t sec = ticks / hz;
  uint32_t csec = (ticks % hz) * 100 / hz;

  sbuf_printf(&sb, "%" PRIu32 ".%02" PRIu32 " 0.00\n", sec, csec);
  sbuf_printf(&sb, "uptime: %" PRIu32 "s (%" PRIu32 " ticks @ %" PRIu32 " Hz)\n",
              sec, ticks, hz);
  sbuf_printf(&sb, "reset reason: %s (%d)\n",
              procfs_reset_reason_name(esp_reset_reason()), (int)esp_reset_reason());

  return sb.buf;
}

// ── VFS Implementation ─────────────────────────────────────────

static int procfs_fd_alloc(char *data) {
  if (!data) return -1;
  for (int i = 0; i < EOS_PROCFS_MAX_FDS; i++) {
    if (!s_procfs_fds[i].in_use) {
      s_procfs_fds[i].data = data;
      s_procfs_fds[i].size = strlen(data);
      s_procfs_fds[i].offset = 0;
      s_procfs_fds[i].in_use = true;
      return i;
    }
  }
  free(data);
  return -1;
}

static procfs_fd_t *procfs_fd_get(int fd) {
  if (fd < 0 || fd >= EOS_PROCFS_MAX_FDS || !s_procfs_fds[fd].in_use) {
    return NULL;
  }
  return &s_procfs_fds[fd];
}

static int procfs_open(void *ctx, const char *path, int flags, int mode) {
  (void)ctx;
  (void)mode;

  // ProcFS is strictly read-only
  if (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) {
    errno = EROFS;
    return -1;
  }

  if (!path) {
    errno = ENOENT;
    return -1;
  }

  const char *p = path;
  while (*p == '/') p++;

  char *content = NULL;
  if (strcmp(p, "scopes") == 0) {
    content = procfs_gen_scopes();
  } else if (strcmp(p, "drivers") == 0) {
    content = procfs_gen_drivers();
  } else if (strcmp(p, "devices") == 0) {
    content = procfs_gen_devices();
  } else if (strcmp(p, "meminfo") == 0) {
    content = procfs_gen_meminfo();
  } else if (strcmp(p, "version") == 0) {
    content = procfs_gen_version();
  } else if (strcmp(p, "mounts") == 0) {
    content = procfs_gen_mounts();
  } else if (strcmp(p, "interrupts") == 0) {
    content = procfs_gen_interrupts();
  } else if (strcmp(p, "fds") == 0) {
    content = procfs_gen_fds();
  } else if (strcmp(p, "gpio") == 0) {
    content = procfs_gen_gpio();
  } else if (strcmp(p, "heapinfo") == 0) {
    content = procfs_gen_heapinfo();
  } else if (strcmp(p, "cpuinfo") == 0) {
    content = procfs_gen_cpuinfo();
  } else if (strcmp(p, "uptime") == 0) {
    content = procfs_gen_uptime();
  } else {
    errno = ENOENT;
    return -1;
  }

  if (!content) {
    errno = ENOMEM;
    return -1;
  }

  int fd = procfs_fd_alloc(content);
  if (fd < 0) {
    errno = ENFILE;
    return -1;
  }
  return fd;
}

static int procfs_close(void *ctx, int fd) {
  (void)ctx;
  procfs_fd_t *e = procfs_fd_get(fd);
  if (!e) {
    errno = EBADF;
    return -1;
  }
  if (e->data) {
    free(e->data);
    e->data = NULL;
  }
  e->in_use = false;
  e->size = 0;
  e->offset = 0;
  return 0;
}

static ssize_t procfs_read(void *ctx, int fd, void *dst, size_t size) {
  (void)ctx;
  procfs_fd_t *e = procfs_fd_get(fd);
  if (!e) {
    errno = EBADF;
    return -1;
  }
  if (!dst || size == 0) return 0;

  if (e->offset >= (off_t)e->size) {
    return 0; // EOF
  }

  size_t available = e->size - (size_t)e->offset;
  size_t to_copy = (size > available) ? available : size;

  memcpy(dst, e->data + e->offset, to_copy);
  e->offset += (off_t)to_copy;
  return (ssize_t)to_copy;
}

static off_t procfs_lseek(void *ctx, int fd, off_t offset, int whence) {
  (void)ctx;
  procfs_fd_t *e = procfs_fd_get(fd);
  if (!e) {
    errno = EBADF;
    return -1;
  }

  off_t new_off = e->offset;
  if (whence == SEEK_SET) {
    new_off = offset;
  } else if (whence == SEEK_CUR) {
    new_off += offset;
  } else if (whence == SEEK_END) {
    new_off = (off_t)e->size + offset;
  } else {
    errno = EINVAL;
    return -1;
  }

  if (new_off < 0) {
    errno = EINVAL;
    return -1;
  }

  e->offset = new_off;
  return new_off;
}

static int procfs_fstat(void *ctx, int fd, struct stat *st) {
  (void)ctx;
  procfs_fd_t *e = procfs_fd_get(fd);
  if (!e || !st) {
    errno = EBADF;
    return -1;
  }

  memset(st, 0, sizeof(*st));
  st->st_mode = S_IFREG | 0444;
  st->st_size = (off_t)e->size;
  return 0;
}

static int procfs_stat(void *ctx, const char *path, struct stat *st) {
  (void)ctx;
  if (!path || !st) {
    errno = EINVAL;
    return -1;
  }

  const char *p = path;
  while (*p == '/') p++;

  if (*p == '\0') {
    // Root /proc directory
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFDIR | 0555;
    return 0;
  }

  for (size_t i = 2; i < PROC_ENTRY_COUNT; i++) {
    if (strcmp(p, s_proc_entries[i]) == 0) {
      memset(st, 0, sizeof(*st));
      st->st_mode = S_IFREG | 0444;
      return 0;
    }
  }

  errno = ENOENT;
  return -1;
}

static DIR *procfs_opendir(void *ctx, const char *path) {
  (void)ctx;
  if (path) {
    const char *p = path;
    while (*p == '/') p++;
    if (*p != '\0') {
      errno = ENOTDIR;
      return NULL;
    }
  }

  procfs_dir_t *dir = calloc(1, sizeof(procfs_dir_t));
  if (!dir) {
    errno = ENOMEM;
    return NULL;
  }
  dir->index = 0;
  return (DIR *)dir;
}

static struct dirent *procfs_readdir(void *ctx, DIR *pdir) {
  (void)ctx;
  procfs_dir_t *dir = (procfs_dir_t *)pdir;
  if (!dir) {
    errno = EBADF;
    return NULL;
  }

  if (dir->index >= PROC_ENTRY_COUNT) {
    return NULL; // End of directory
  }

  memset(&dir->entry, 0, sizeof(dir->entry));
  const char *name = s_proc_entries[dir->index];
  strncpy(dir->entry.d_name, name, sizeof(dir->entry.d_name) - 1);
  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
    dir->entry.d_type = DT_DIR;
  } else {
    dir->entry.d_type = DT_REG;
  }
  dir->index++;
  return &dir->entry;
}

static long procfs_telldir(void *ctx, DIR *pdir) {
  (void)ctx;
  procfs_dir_t *dir = (procfs_dir_t *)pdir;
  if (!dir) return -1;
  return (long)dir->index;
}

static void procfs_seekdir(void *ctx, DIR *pdir, long offset) {
  (void)ctx;
  procfs_dir_t *dir = (procfs_dir_t *)pdir;
  if (!dir) return;
  dir->index = (uint32_t)offset;
}

static int procfs_closedir(void *ctx, DIR *pdir) {
  (void)ctx;
  if (pdir) free(pdir);
  return 0;
}

// ── Initialization ────────────────────────────────────────────

void eos_procfs_init(void) {
  for (int i = 0; i < EOS_PROCFS_MAX_FDS; i++) {
    s_procfs_fds[i].in_use = false;
    s_procfs_fds[i].data = NULL;
    s_procfs_fds[i].size = 0;
    s_procfs_fds[i].offset = 0;
  }

  static const esp_vfs_t vfs = {
      .flags = ESP_VFS_FLAG_CONTEXT_PTR,
      .open_p = procfs_open,
      .close_p = procfs_close,
      .read_p = procfs_read,
      .lseek_p = procfs_lseek,
      .fstat_p = procfs_fstat,
      .stat_p = procfs_stat,
      .opendir_p = procfs_opendir,
      .readdir_p = procfs_readdir,
      .seekdir_p = procfs_seekdir,
      .telldir_p = procfs_telldir,
      .closedir_p = procfs_closedir,
  };

  mkdir(EOS_PROCFS_ROOT, 0755);
  esp_vfs_register(EOS_PROCFS_ROOT, &vfs, NULL);
  EOS_LOGI("procfs mounted at %s", EOS_PROCFS_ROOT);
}
