#include "procfs.h"
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/log.h"
#include "ecore/proc.h"
#include "emisc/kvec.h"
#include "emisc/psram_alloc.h"
#include <ctype.h>
#include <dirent.h>
#include <driver/gpio.h>
#include <errno.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_intr_alloc.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_vfs.h>
#include <fcntl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
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

typedef enum {
  PROCFS_NODE_INVALID = 0,
  PROCFS_NODE_ROOT,
  PROCFS_NODE_STATIC_FILE,
  PROCFS_NODE_PIPES_DIR,
  PROCFS_NODE_PID_DIR,
  PROCFS_NODE_PID_STATUS,
  PROCFS_NODE_PID_STAT,
  PROCFS_NODE_PID_CMDLINE,
  PROCFS_NODE_PID_CWD,
  PROCFS_NODE_PID_ENVIRON,
  PROCFS_NODE_PID_FD_DIR,
  PROCFS_NODE_PID_FD_ENTRY,
} procfs_node_type_t;

typedef struct {
  procfs_node_type_t type;
  pid_t pid;
  int fd_num;
  const char *static_name;
} procfs_parsed_path_t;

typedef struct {
  uint32_t esp_idf_fs_index; // Must be first field for ESP-IDF VFS
  procfs_node_type_t dir_type;
  pid_t pid;
  uint32_t index;
  kvec_psram_t(pid_t) pids;
  kvec_psram_t(int) fds;
  struct dirent entry;
} procfs_dir_t;

static kvec_psram_t(procfs_fd_t) s_procfs_fds;

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
    "loadavg",
    "stat",
    "pipes",
    "self",
};
#define PROC_ENTRY_COUNT (sizeof(s_proc_entries) / sizeof(s_proc_entries[0]))

static const char *s_pid_entries[] = {
    ".",
    "..",
    "status",
    "stat",
    "cmdline",
    "cwd",
    "environ",
    "fd",
};
#define PID_ENTRY_COUNT (sizeof(s_pid_entries) / sizeof(s_pid_entries[0]))

// Dynamic string accumulator
typedef struct {
  char *buf;
  size_t cap;
  size_t len;
} sbuf_t;

static void sbuf_init(sbuf_t *s) {
  s->cap = 2048;
  s->len = 0;
  s->buf = eos_psram_malloc_prefer(s->cap);
  if (!s->buf) {
    s->buf = malloc(s->cap);
  }
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
    char *new_buf = eos_psram_realloc_prefer(s->buf, new_cap);
    if (!new_buf) {
      new_buf = realloc(s->buf, new_cap);
    }
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
  for (size_t i = 0; i < eos_dev_count(); i++) {
    eos_dev_t *dev = eos_dev_get(i);
    if (!dev || !dev->in_use) continue;

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

static double s_load1 = 0.0;
static double s_load5 = 0.0;
static double s_load15 = 0.0;
static uint64_t s_last_load_sample = 0;

static void update_loadavg(void) {
  uint64_t now = esp_timer_get_time();
  if (s_last_load_sample == 0) {
    s_last_load_sample = now;
    return;
  }
  uint64_t elapsed_us = now - s_last_load_sample;
  if (elapsed_us < 1000000) return; // Sample at most once every second
  s_last_load_sample = now;

  double dt = (double)elapsed_us / 1000000.0;
  double active = 1.0;
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
  UBaseType_t task_count = uxTaskGetNumberOfTasks();
  TaskStatus_t *status_arr = eos_psram_calloc_prefer(task_count, sizeof(TaskStatus_t));
  if (status_arr) {
    UBaseType_t actual = uxTaskGetSystemState(status_arr, task_count, NULL);
    active = 0.0;
    for (UBaseType_t i = 0; i < actual; i++) {
      if (status_arr[i].eCurrentState == eRunning || status_arr[i].eCurrentState == eReady) {
        active += 1.0;
      }
    }
    eos_psram_free(status_arr);
  }
#endif

  double exp1 = exp(-dt / 60.0);
  double exp5 = exp(-dt / 300.0);
  double exp15 = exp(-dt / 900.0);

  s_load1 = s_load1 * exp1 + active * (1.0 - exp1);
  s_load5 = s_load5 * exp5 + active * (1.0 - exp5);
  s_load15 = s_load15 * exp15 + active * (1.0 - exp15);
}

static char *procfs_gen_loadavg(void) {
  update_loadavg();

  sbuf_t sb;
  sbuf_init(&sb);
  if (!sb.buf) return NULL;

  UBaseType_t total_tasks = uxTaskGetNumberOfTasks();
  pid_t last_pid = getpid();

  sbuf_printf(&sb, "%.2f %.2f %.2f 1/%u %d\n",
              s_load1, s_load5, s_load15, (unsigned int)total_tasks, (int)last_pid);

  return sb.buf;
}

static char *procfs_gen_stat(void) {
  sbuf_t sb;
  sbuf_init(&sb);
  if (!sb.buf) return NULL;

  uint64_t total_runtime = esp_timer_get_time();
  unsigned long total_ticks = (unsigned long)(total_runtime / 10000);
  unsigned long idle_ticks = 0;
  unsigned long user_ticks = 0;

#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
  UBaseType_t task_count = uxTaskGetNumberOfTasks();
  TaskStatus_t *status_arr = eos_psram_calloc_prefer(task_count, sizeof(TaskStatus_t));
  if (status_arr) {
    uint32_t total_run = 0;
    UBaseType_t actual = uxTaskGetSystemState(status_arr, task_count, &total_run);
    for (UBaseType_t i = 0; i < actual; i++) {
      if (strncmp(status_arr[i].pcTaskName, "IDLE", 4) == 0) {
        idle_ticks += (unsigned long)(status_arr[i].ulRunTimeCounter / 10000);
      } else {
        user_ticks += (unsigned long)(status_arr[i].ulRunTimeCounter / 10000);
      }
    }
    eos_psram_free(status_arr);
  }
#endif

  if (idle_ticks == 0 && total_ticks > user_ticks) {
    idle_ticks = total_ticks - user_ticks;
  }

  sbuf_printf(&sb, "cpu  %lu 0 0 %lu 0 0 0 0 0 0\n", user_ticks, idle_ticks);
  sbuf_printf(&sb, "btime %llu\n", (unsigned long long)1700000000);
  sbuf_printf(&sb, "processes %zu\n", eos_proc_count_active());

  return sb.buf;
}

// ── VFS Implementation ─────────────────────────────────────────

static int procfs_fd_alloc(char *data) {
  if (!data) return -1;
  for (size_t i = 0; i < kv_size(s_procfs_fds); i++) {
    if (!kv_A(s_procfs_fds, i).in_use) {
      kv_A(s_procfs_fds, i).data = data;
      kv_A(s_procfs_fds, i).size = strlen(data);
      kv_A(s_procfs_fds, i).offset = 0;
      kv_A(s_procfs_fds, i).in_use = true;
      return (int)i;
    }
  }
  procfs_fd_t entry = {
      .data = data,
      .size = strlen(data),
      .offset = 0,
      .in_use = true,
  };
  int fd = (int)kv_size(s_procfs_fds);
  kv_push(procfs_fd_t, s_procfs_fds, entry);
  return fd;
}

static procfs_fd_t *procfs_fd_get(int fd) {
  if (fd < 0 || (size_t)fd >= kv_size(s_procfs_fds) || !kv_A(s_procfs_fds, fd).in_use) {
    return NULL;
  }
  return &kv_A(s_procfs_fds, fd);
}

// ── Dynamic PID Generators & Path Parsing ────────────────────────

static bool procfs_parse_path(const char *path, procfs_parsed_path_t *out) {
  memset(out, 0, sizeof(*out));
  if (!path) return false;

  const char *p = path;
  while (*p == '/') p++;

  if (*p == '\0' || strcmp(p, ".") == 0 || strcmp(p, "..") == 0) {
    out->type = PROCFS_NODE_ROOT;
    return true;
  }

  // Check static files and special dirs at root level
  for (size_t i = 2; i < PROC_ENTRY_COUNT; i++) {
    if (strcmp(p, s_proc_entries[i]) == 0) {
      if (strcmp(p, "pipes") == 0) {
        out->type = PROCFS_NODE_PIPES_DIR;
        return true;
      }
      if (strcmp(p, "self") == 0) {
        out->type = PROCFS_NODE_PID_DIR;
        out->pid = eos_getpid();
        return true;
      }
      out->type = PROCFS_NODE_STATIC_FILE;
      out->static_name = s_proc_entries[i];
      return true;
    }
  }

  // Check self/...
  const char *sub = NULL;
  pid_t target_pid = -1;
  if (strcmp(p, "self") == 0) {
    out->type = PROCFS_NODE_PID_DIR;
    out->pid = eos_getpid();
    return true;
  } else if (strncmp(p, "self/", 5) == 0) {
    target_pid = eos_getpid();
    sub = p + 5;
  } else if (isdigit((unsigned char)*p)) {
    char *end = NULL;
    unsigned long val = strtoul(p, &end, 10);
    target_pid = (pid_t)val;
    if (*end == '\0') {
      out->type = PROCFS_NODE_PID_DIR;
      out->pid = target_pid;
      return true;
    } else if (*end == '/') {
      sub = end + 1;
    } else {
      return false;
    }
  }

  if (target_pid >= 0 && sub) {
    while (*sub == '/') sub++;
    if (*sub == '\0') {
      out->type = PROCFS_NODE_PID_DIR;
      out->pid = target_pid;
      return true;
    }
    out->pid = target_pid;
    if (strcmp(sub, "status") == 0) {
      out->type = PROCFS_NODE_PID_STATUS;
      return true;
    } else if (strcmp(sub, "stat") == 0) {
      out->type = PROCFS_NODE_PID_STAT;
      return true;
    } else if (strcmp(sub, "cmdline") == 0) {
      out->type = PROCFS_NODE_PID_CMDLINE;
      return true;
    } else if (strcmp(sub, "cwd") == 0) {
      out->type = PROCFS_NODE_PID_CWD;
      return true;
    } else if (strcmp(sub, "environ") == 0) {
      out->type = PROCFS_NODE_PID_ENVIRON;
      return true;
    } else if (strcmp(sub, "fd") == 0 || strcmp(sub, "fd/") == 0) {
      out->type = PROCFS_NODE_PID_FD_DIR;
      return true;
    } else if (strncmp(sub, "fd/", 3) == 0) {
      const char *fd_str = sub + 3;
      while (*fd_str == '/') fd_str++;
      if (isdigit((unsigned char)*fd_str)) {
        char *endfd = NULL;
        out->fd_num = (int)strtol(fd_str, &endfd, 10);
        if (*endfd == '\0') {
          out->type = PROCFS_NODE_PID_FD_ENTRY;
          return true;
        }
      }
    }
  }

  return false;
}

static char *procfs_gen_pid_status(eos_proc_t *p) {
  sbuf_t sb;
  sbuf_init(&sb);
  if (!sb.buf) return NULL;

  const char *state_desc = "unknown";
  switch (p->state) {
    case EOS_PROC_STATE_RUNNING: state_desc = "R (running)"; break;
    case EOS_PROC_STATE_SLEEPING: state_desc = "S (sleeping)"; break;
    case EOS_PROC_STATE_STOPPED: state_desc = "T (stopped)"; break;
    case EOS_PROC_STATE_ZOMBIE: state_desc = "Z (zombie)"; break;
    default: break;
  }

  sbuf_printf(&sb, "Name:\t%s\n", p->name);
  sbuf_printf(&sb, "State:\t%s\n", state_desc);
  sbuf_printf(&sb, "Tgid:\t%d\n", (int)p->pid);
  sbuf_printf(&sb, "Pid:\t%d\n", (int)p->pid);
  sbuf_printf(&sb, "PPid:\t%d\n", (int)p->ppid);
  sbuf_printf(&sb, "Pgid:\t%d\n", (int)p->pgid);
  sbuf_printf(&sb, "Sid:\t%d\n", (int)p->sid);
  sbuf_printf(&sb, "TTY:\t%s\n", p->tty[0] ? p->tty : "?");
  sbuf_printf(&sb, "Threads:\t%zu\n", kv_size(p->threads) ? kv_size(p->threads) : (size_t)1);
  sbuf_printf(&sb, "CPUTime:\t%llu\n", (unsigned long long)eos_proc_get_cpu_time_us(p));
  sbuf_printf(&sb, "StartTime:\t%llu\n", (unsigned long long)p->start_time);
  sbuf_printf(&sb, "FDSize:\t%zu\n", kv_size(p->fds));
  sbuf_printf(&sb, "VmData:\t%zu kB\n", (p->heap_sram + p->heap_psram) / 1024);
  sbuf_printf(&sb, "HeapSRAM:\t%zu\n", p->heap_sram);
  sbuf_printf(&sb, "HeapPSRAM:\t%zu\n", p->heap_psram);

  return sb.buf;
}

static char *procfs_gen_pid_stat(eos_proc_t *p) {
  sbuf_t sb;
  sbuf_init(&sb);
  if (!sb.buf) return NULL;

  char st_c = (char)p->state;
  if (st_c == 0) st_c = 'R';

  uint64_t cpu_us = eos_proc_get_cpu_time_us(p);
  unsigned long utime = (unsigned long)(cpu_us / 10000); // 100Hz clock ticks

  sbuf_printf(&sb, "%d (%s) %c %d %d %d 0 0 0 0 0 0 0 %lu 0 0 0 0 0 %zu 0 %llu 0 0\n",
              (int)p->pid, p->name, st_c, (int)p->ppid, (int)p->pgid, (int)p->sid,
              utime,
              kv_size(p->threads) ? kv_size(p->threads) : (size_t)1,
              (unsigned long long)(p->start_time / 1000));

  return sb.buf;
}

static char *procfs_gen_pid_cmdline(eos_proc_t *p) {
  sbuf_t sb;
  sbuf_init(&sb);
  if (!sb.buf) return NULL;

  if (p->cmdline && p->cmdline[0]) {
    sbuf_printf(&sb, "%s\n", p->cmdline);
  } else {
    sbuf_printf(&sb, "%s\n", p->name);
  }

  return sb.buf;
}

static char *procfs_gen_pid_cwd(eos_proc_t *p) {
  sbuf_t sb;
  sbuf_init(&sb);
  if (!sb.buf) return NULL;

  const char *cwd = (p->cwd[0]) ? p->cwd : "/";
  sbuf_printf(&sb, "%s\n", cwd);

  return sb.buf;
}

static char *procfs_gen_pid_environ(eos_proc_t *p) {
  sbuf_t sb;
  sbuf_init(&sb);
  if (!sb.buf) return NULL;

  for (size_t i = 0; i < kv_size(p->env); i++) {
    char *entry = kv_A(p->env, i);
    if (entry) {
      sbuf_printf(&sb, "%s\n", entry);
    }
  }

  return sb.buf;
}

static char *procfs_gen_pid_fd_info(eos_proc_t *p, int fd) {
  sbuf_t sb;
  sbuf_init(&sb);
  if (!sb.buf) return NULL;

  int real_fd = fd;
  int mfd = eos_proc_fd_map_get(p, fd);
  if (mfd >= 0) {
    real_fd = mfd;
  }

  sbuf_printf(&sb, "pos:\t0\nflags:\t0%o\nreal_fd:\t%d\n", O_RDWR, real_fd);

  return sb.buf;
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

  procfs_parsed_path_t parsed;
  if (!procfs_parse_path(path, &parsed)) {
    errno = ENOENT;
    return -1;
  }

  if (parsed.type == PROCFS_NODE_ROOT ||
      parsed.type == PROCFS_NODE_PIPES_DIR ||
      parsed.type == PROCFS_NODE_PID_DIR ||
      parsed.type == PROCFS_NODE_PID_FD_DIR) {
    errno = EISDIR;
    return -1;
  }

  char *content = NULL;

  if (parsed.type == PROCFS_NODE_STATIC_FILE) {
    const char *p = parsed.static_name;
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
    } else if (strcmp(p, "loadavg") == 0) {
      content = procfs_gen_loadavg();
    } else if (strcmp(p, "stat") == 0) {
      content = procfs_gen_stat();
    } else {
      errno = ENOENT;
      return -1;
    }
  } else {
    // Dynamic process file
    eos_proc_t p;
    if (!eos_proc_get_copy(parsed.pid, &p)) {
      errno = ENOENT;
      return -1;
    }

    switch (parsed.type) {
      case PROCFS_NODE_PID_STATUS:
        content = procfs_gen_pid_status(&p);
        break;
      case PROCFS_NODE_PID_STAT:
        content = procfs_gen_pid_stat(&p);
        break;
      case PROCFS_NODE_PID_CMDLINE:
        content = procfs_gen_pid_cmdline(&p);
        break;
      case PROCFS_NODE_PID_CWD:
        content = procfs_gen_pid_cwd(&p);
        break;
      case PROCFS_NODE_PID_ENVIRON:
        content = procfs_gen_pid_environ(&p);
        break;
      case PROCFS_NODE_PID_FD_ENTRY:
        content = procfs_gen_pid_fd_info(&p, parsed.fd_num);
        break;
      default:
        errno = ENOENT;
        return -1;
    }
  }

  if (!content) {
    errno = ENOMEM;
    return -1;
  }

  int fd = procfs_fd_alloc(content);
  if (fd < 0) {
    free(content);
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

  procfs_parsed_path_t parsed;
  if (!procfs_parse_path(path, &parsed)) {
    errno = ENOENT;
    return -1;
  }

  memset(st, 0, sizeof(*st));

  switch (parsed.type) {
    case PROCFS_NODE_ROOT:
    case PROCFS_NODE_PIPES_DIR:
      st->st_mode = S_IFDIR | 0555;
      return 0;

    case PROCFS_NODE_STATIC_FILE:
      st->st_mode = S_IFREG | 0444;
      return 0;

    case PROCFS_NODE_PID_DIR: {
      eos_proc_t p;
      if (!eos_proc_get_copy(parsed.pid, &p)) {
        errno = ENOENT;
        return -1;
      }
      st->st_mode = S_IFDIR | 0555;
      return 0;
    }

    case PROCFS_NODE_PID_FD_DIR: {
      eos_proc_t p;
      if (!eos_proc_get_copy(parsed.pid, &p)) {
        errno = ENOENT;
        return -1;
      }
      st->st_mode = S_IFDIR | 0555;
      return 0;
    }

    case PROCFS_NODE_PID_STATUS:
    case PROCFS_NODE_PID_STAT:
    case PROCFS_NODE_PID_CMDLINE:
    case PROCFS_NODE_PID_CWD:
    case PROCFS_NODE_PID_ENVIRON: {
      eos_proc_t p;
      if (!eos_proc_get_copy(parsed.pid, &p)) {
        errno = ENOENT;
        return -1;
      }
      st->st_mode = S_IFREG | 0444;
      return 0;
    }

    case PROCFS_NODE_PID_FD_ENTRY: {
      eos_proc_t p;
      if (!eos_proc_get_copy(parsed.pid, &p)) {
        errno = ENOENT;
        return -1;
      }
      st->st_mode = S_IFREG | 0444;
      return 0;
    }

    default:
      errno = ENOENT;
      return -1;
  }
}

static DIR *procfs_opendir(void *ctx, const char *path) {
  (void)ctx;
  procfs_parsed_path_t parsed;
  if (!procfs_parse_path(path, &parsed)) {
    errno = ENOENT;
    return NULL;
  }

  if (parsed.type != PROCFS_NODE_ROOT &&
      parsed.type != PROCFS_NODE_PIPES_DIR &&
      parsed.type != PROCFS_NODE_PID_DIR &&
      parsed.type != PROCFS_NODE_PID_FD_DIR) {
    errno = ENOTDIR;
    return NULL;
  }

  // If opening PID or PID_FD dir, verify process exists
  if (parsed.type == PROCFS_NODE_PID_DIR || parsed.type == PROCFS_NODE_PID_FD_DIR) {
    eos_proc_t p_check;
    if (!eos_proc_get_copy(parsed.pid, &p_check)) {
      errno = ENOENT;
      return NULL;
    }
  }

  procfs_dir_t *dir = (procfs_dir_t *)eos_psram_calloc_prefer(1, sizeof(procfs_dir_t));
  if (!dir) {
    errno = ENOMEM;
    return NULL;
  }

  dir->dir_type = parsed.type;
  dir->pid = parsed.pid;
  dir->index = 0;
  kv_init(dir->pids);
  kv_init(dir->fds);

  if (parsed.type == PROCFS_NODE_ROOT) {
    size_t active = eos_proc_count_active();
    if (active > 0) {
      pid_t *temp_pids = malloc(active * sizeof(pid_t));
      if (temp_pids) {
        size_t count = eos_proc_get_all_pids(temp_pids, active);
        for (size_t i = 0; i < count; i++) {
          kv_push(pid_t, dir->pids, temp_pids[i]);
        }
        free(temp_pids);
      }
    }
  } else if (parsed.type == PROCFS_NODE_PID_FD_DIR) {
    eos_proc_t p;
    if (eos_proc_get_copy(parsed.pid, &p)) {
      for (int fd = 0; fd < 3; fd++) {
        kv_push(int, dir->fds, fd);
      }
      for (size_t i = 0; i < kv_size(p.fds); i++) {
        int v = kv_A(p.fds, i);
        if (v >= 3) {
          kv_push(int, dir->fds, v);
        }
      }
    }
  }

  return (DIR *)dir;
}

static struct dirent *procfs_readdir(void *ctx, DIR *pdir) {
  (void)ctx;
  procfs_dir_t *dir = (procfs_dir_t *)pdir;
  if (!dir) {
    errno = EBADF;
    return NULL;
  }

  memset(&dir->entry, 0, sizeof(dir->entry));

  if (dir->dir_type == PROCFS_NODE_ROOT) {
    if (dir->index < PROC_ENTRY_COUNT) {
      const char *name = s_proc_entries[dir->index];
      strncpy(dir->entry.d_name, name, sizeof(dir->entry.d_name) - 1);
      if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
          strcmp(name, "pipes") == 0 || strcmp(name, "self") == 0) {
        dir->entry.d_type = DT_DIR;
      } else {
        dir->entry.d_type = DT_REG;
      }
      dir->index++;
      return &dir->entry;
    }

    size_t pid_idx = dir->index - PROC_ENTRY_COUNT;
    if (pid_idx < kv_size(dir->pids)) {
      snprintf(dir->entry.d_name, sizeof(dir->entry.d_name), "%d", (int)kv_A(dir->pids, pid_idx));
      dir->entry.d_type = DT_DIR;
      dir->index++;
      return &dir->entry;
    }

    return NULL; // End of root directory
  }

  if (dir->dir_type == PROCFS_NODE_PID_DIR) {
    if (dir->index >= PID_ENTRY_COUNT) {
      return NULL;
    }
    const char *name = s_pid_entries[dir->index];
    strncpy(dir->entry.d_name, name, sizeof(dir->entry.d_name) - 1);
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0 || strcmp(name, "fd") == 0) {
      dir->entry.d_type = DT_DIR;
    } else {
      dir->entry.d_type = DT_REG;
    }
    dir->index++;
    return &dir->entry;
  }

  if (dir->dir_type == PROCFS_NODE_PID_FD_DIR) {
    if (dir->index == 0) {
      strcpy(dir->entry.d_name, ".");
      dir->entry.d_type = DT_DIR;
      dir->index++;
      return &dir->entry;
    }
    if (dir->index == 1) {
      strcpy(dir->entry.d_name, "..");
      dir->entry.d_type = DT_DIR;
      dir->index++;
      return &dir->entry;
    }
    size_t fd_idx = dir->index - 2;
    if (fd_idx < kv_size(dir->fds)) {
      snprintf(dir->entry.d_name, sizeof(dir->entry.d_name), "%d", kv_A(dir->fds, fd_idx));
      dir->entry.d_type = DT_CHR;
      dir->index++;
      return &dir->entry;
    }
    return NULL;
  }

  return NULL;
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
  if (pdir) {
    procfs_dir_t *dir = (procfs_dir_t *)pdir;
    kv_destroy(dir->pids);
    kv_destroy(dir->fds);
    free(dir);
  }
  return 0;
}

// ── Initialization ────────────────────────────────────────────

void eos_procfs_init(void) {
  kv_init(s_procfs_fds);

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
