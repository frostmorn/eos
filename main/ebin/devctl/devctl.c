#include "includes.h"
#include "ecore/bin.h"
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/log.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void print_usage(void) {
  printf("Usage: devctl <command> [args...]\n\n"
         "Commands:\n"
         "  scopes                                List available driver scopes (from /proc/scopes)\n"
         "  drivers [scope]                       List registered drivers (from /proc/drivers)\n"
         "  devices                               List active devtree devices (from /proc/devices)\n"
         "  attach <scope> <drv> [par] [options]  Attach a device dynamically (default parent: root)\n"
         "                                          --pin <key=val,...>  Pin mapping\n"
         "                                          --cfg <key=val,...>  Device configuration\n"
         "  detach <dev_name>                     Detach a device dynamically (e.g. devctl detach br0)\n"
         "  info <dev_name>                       Show detailed device information\n");
}

static int cmd_scopes(void) {
  FILE *f = fopen("/proc/scopes", "r");
  if (!f) {
    perror("devctl: failed to open /proc/scopes");
    return 1;
  }

  printf("Driver Scopes:\n");
  char line[64];
  while (fgets(line, sizeof(line), f)) {
    printf("  %s", line);
  }
  fclose(f);
  return 0;
}

static int cmd_drivers(const char *filter_scope) {
  FILE *f = fopen("/proc/drivers", "r");
  if (!f) {
    perror("devctl: failed to open /proc/drivers");
    return 1;
  }

  char line[128];
  bool is_header = true;
  while (fgets(line, sizeof(line), f)) {
    if (is_header) {
      printf("%s", line);
      is_header = false;
      continue;
    }
    if (filter_scope) {
      char scope[32];
      if (sscanf(line, "%31s", scope) == 1) {
        if (strcmp(scope, filter_scope) != 0) {
          continue;
        }
      }
    }
    printf("%s", line);
  }
  fclose(f);
  return 0;
}

static int cmd_devices(void) {
  FILE *f = fopen("/proc/devices", "r");
  if (!f) {
    perror("devctl: failed to open /proc/devices");
    return 1;
  }

  char line[128];
  while (fgets(line, sizeof(line), f)) {
    printf("%s", line);
  }
  fclose(f);
  return 0;
}

static char *next_csv_token(char **cursor) {
  if (!cursor || !*cursor || !**cursor) return NULL;
  char *start = *cursor;
  bool in_quotes = false;
  char quote_char = 0;
  char *p = start;

  while (*p) {
    if (!in_quotes && (*p == '"' || *p == '\'')) {
      in_quotes = true;
      quote_char = *p;
    } else if (in_quotes && *p == quote_char) {
      in_quotes = false;
    } else if (!in_quotes && *p == ',') {
      *p = '\0';
      *cursor = p + 1;
      return start;
    }
    p++;
  }
  *cursor = NULL;
  return start;
}

static int parse_pin_arg(const char *arg, eos_pin_t *pins, size_t *count, size_t max_count) {
  if (!arg || !arg[0]) return 0;
  char *copy = strdup(arg);
  if (!copy) {
    perror("devctl: strdup failed");
    return -1;
  }

  char *cursor = copy;
  char *token = NULL;
  while ((token = next_csv_token(&cursor)) != NULL) {
    while (*token == ' ' || *token == '\t') token++;
    char *end = token + strlen(token) - 1;
    while (end > token && (*end == ' ' || *end == '\t')) {
      *end = '\0';
      end--;
    }

    if (*token) {
      char *eq = strchr(token, '=');
      if (!eq || eq == token) {
        fprintf(stderr, "devctl: invalid pin definition '%s', expected key=val\n", token);
        free(copy);
        return -1;
      }
      *eq = '\0';
      char *key = token;
      char *val_str = eq + 1;
      while (*val_str == ' ' || *val_str == '\t') val_str++;

      char *endptr = NULL;
      long val = strtol(val_str, &endptr, 0);
      if (endptr == val_str || (*endptr != '\0' && *endptr != ' ' && *endptr != '\t')) {
        fprintf(stderr, "devctl: invalid pin number '%s' for pin '%s'\n", val_str, key);
        free(copy);
        return -1;
      }

      if (*count >= max_count - 1) {
        fprintf(stderr, "devctl: too many pins (max %zu)\n", max_count - 1);
        free(copy);
        return -1;
      }

      pins[*count].key = strdup(key);
      pins[*count].val = (int32_t)val;
      (*count)++;
    }
  }

  free(copy);
  return 0;
}

static int parse_cfg_arg(const char *arg, eos_cfg_t *cfg, size_t *count, size_t max_count) {
  if (!arg || !arg[0]) return 0;
  char *copy = strdup(arg);
  if (!copy) {
    perror("devctl: strdup failed");
    return -1;
  }

  char *cursor = copy;
  char *token = NULL;
  while ((token = next_csv_token(&cursor)) != NULL) {
    while (*token == ' ' || *token == '\t') token++;
    char *end = token + strlen(token) - 1;
    while (end > token && (*end == ' ' || *end == '\t')) {
      *end = '\0';
      end--;
    }

    if (*token) {
      char *eq = strchr(token, '=');
      if (!eq || eq == token) {
        fprintf(stderr, "devctl: invalid cfg definition '%s', expected key=val\n", token);
        free(copy);
        return -1;
      }
      *eq = '\0';
      char *key = token;
      char *val_str = eq + 1;
      while (*val_str == ' ' || *val_str == '\t') val_str++;

      if (*count >= max_count - 1) {
        fprintf(stderr, "devctl: too many cfg options (max %zu)\n", max_count - 1);
        free(copy);
        return -1;
      }

      eos_cfg_type_t type = EOS_CFG_INT;
      int32_t val_i = 0;
      float val_f = 0.0f;
      char *val_s = NULL;

      size_t vlen = strlen(val_str);
      if (vlen >= 2 && ((val_str[0] == '"' && val_str[vlen - 1] == '"') ||
                        (val_str[0] == '\'' && val_str[vlen - 1] == '\''))) {
        type = EOS_CFG_STR;
        val_str[vlen - 1] = '\0';
        val_s = strdup(val_str + 1);
      } else if (strcasecmp(val_str, "true") == 0 || strcasecmp(val_str, "yes") == 0 ||
                 strcasecmp(val_str, "on") == 0) {
        type = EOS_CFG_INT;
        val_i = 1;
      } else if (strcasecmp(val_str, "false") == 0 || strcasecmp(val_str, "no") == 0 ||
                 strcasecmp(val_str, "off") == 0) {
        type = EOS_CFG_INT;
        val_i = 0;
      } else if (val_str[0] == '0' && (val_str[1] == 'x' || val_str[1] == 'X')) {
        char *endptr = NULL;
        long num = strtol(val_str, &endptr, 16);
        if (endptr != val_str && *endptr == '\0') {
          type = EOS_CFG_INT;
          val_i = (int32_t)num;
        } else {
          type = EOS_CFG_STR;
          val_s = strdup(val_str);
        }
      } else if (strchr(val_str, '.') != NULL) {
        char *endptr = NULL;
        float f = strtof(val_str, &endptr);
        if (endptr != val_str && *endptr == '\0') {
          type = EOS_CFG_FLOAT;
          val_f = f;
        } else {
          type = EOS_CFG_STR;
          val_s = strdup(val_str);
        }
      } else {
        char *endptr = NULL;
        long num = strtol(val_str, &endptr, 10);
        if (endptr != val_str && *endptr == '\0') {
          type = EOS_CFG_INT;
          val_i = (int32_t)num;
        } else {
          type = EOS_CFG_STR;
          val_s = strdup(val_str);
        }
      }

      cfg[*count].key = strdup(key);
      cfg[*count].type = type;
      if (type == EOS_CFG_INT) {
        cfg[*count].val.i = val_i;
      } else if (type == EOS_CFG_FLOAT) {
        cfg[*count].val.f = val_f;
      } else if (type == EOS_CFG_STR) {
        cfg[*count].val.s = val_s;
      }
      (*count)++;
    }
  }

  free(copy);
  return 0;
}

static int cmd_attach(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "Usage: devctl attach <scope> <driver> [parent] [--pin key=val,...] [--cfg key=val,...]\n");
    return 1;
  }

  const char *scope = argv[2];
  const char *drv_name = argv[3];
  const char *parent_name = NULL;

  eos_pin_t parsed_pins[64];
  size_t pin_count = 0;
  memset(parsed_pins, 0, sizeof(parsed_pins));

  eos_cfg_t parsed_cfg[64];
  size_t cfg_count = 0;
  memset(parsed_cfg, 0, sizeof(parsed_cfg));

  for (int i = 4; i < argc; i++) {
    const char *arg = argv[i];
    if (strcmp(arg, "--pin") == 0 || strcmp(arg, "-p") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "devctl: option '%s' requires an argument\n", arg);
        goto cleanup_err;
      }
      i++;
      if (parse_pin_arg(argv[i], parsed_pins, &pin_count, 64) != 0) {
        goto cleanup_err;
      }
    } else if (strncmp(arg, "--pin=", 6) == 0) {
      if (parse_pin_arg(arg + 6, parsed_pins, &pin_count, 64) != 0) {
        goto cleanup_err;
      }
    } else if (strcmp(arg, "--cfg") == 0 || strcmp(arg, "-c") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "devctl: option '%s' requires an argument\n", arg);
        goto cleanup_err;
      }
      i++;
      if (parse_cfg_arg(argv[i], parsed_cfg, &cfg_count, 64) != 0) {
        goto cleanup_err;
      }
    } else if (strncmp(arg, "--cfg=", 6) == 0) {
      if (parse_cfg_arg(arg + 6, parsed_cfg, &cfg_count, 64) != 0) {
        goto cleanup_err;
      }
    } else if (arg[0] == '-') {
      fprintf(stderr, "devctl: unrecognized option '%s'\n", arg);
      goto cleanup_err;
    } else {
      if (!parent_name) {
        parent_name = arg;
      } else {
        fprintf(stderr, "devctl: unexpected positional argument '%s'\n", arg);
        goto cleanup_err;
      }
    }
  }

  eos_drv_t *drv = eos_drv_find(scope, drv_name);
  if (!drv) {
    fprintf(stderr, "devctl: driver \"%s/%s\" not found\n", scope, drv_name);
    goto cleanup_err;
  }

  eos_dev_t *parent = NULL;
  if (parent_name && parent_name[0]) {
    if (strcmp(parent_name, "root") == 0) {
      parent = eos_devtree_root();
    } else {
      parent = eos_dev_find_by_name(parent_name);
    }
    if (!parent) {
      fprintf(stderr, "devctl: parent device \"%s\" not found\n", parent_name);
      goto cleanup_err;
    }
  } else {
    parent = eos_devtree_root();
  }

  eos_dev_t *dev = eos_dev_alloc();
  if (!dev) {
    fprintf(stderr, "devctl: failed to allocate device slot (ENFILE)\n");
    goto cleanup_err;
  }

  dev->driver = drv;

  if (pin_count > 0) {
    eos_pin_t *pins_arr = calloc(pin_count + 1, sizeof(eos_pin_t));
    if (!pins_arr) {
      fprintf(stderr, "devctl: out of memory for pins\n");
      bzero(dev, sizeof(eos_dev_t));
      goto cleanup_err;
    }
    memcpy(pins_arr, parsed_pins, pin_count * sizeof(eos_pin_t));
    dev->pins = pins_arr;
    dev->dynamic_pins = true;
  }

  if (cfg_count > 0) {
    eos_cfg_t *cfg_arr = calloc(cfg_count + 1, sizeof(eos_cfg_t));
    if (!cfg_arr) {
      fprintf(stderr, "devctl: out of memory for cfg\n");
      if (dev->pins) free(dev->pins);
      bzero(dev, sizeof(eos_dev_t));
      goto cleanup_err;
    }
    memcpy(cfg_arr, parsed_cfg, cfg_count * sizeof(eos_cfg_t));
    dev->cfg = cfg_arr;
    dev->dynamic_cfg = true;
  }

  int rc = eos_dev_attach(dev, parent);
  if (rc != 0) {
    fprintf(stderr, "devctl: failed to attach %s/%s under %s: %s\n",
            scope, drv_name, parent->name[0] ? parent->name : "root", strerror(errno));
    if (dev->parent == NULL) {
      if (dev->dynamic_pins && dev->pins) {
        for (eos_pin_t *p = dev->pins; p->key != NULL; p++) free(p->key);
        free(dev->pins);
      }
      if (dev->dynamic_cfg && dev->cfg) {
        for (eos_cfg_t *c = dev->cfg; c->key != NULL; c++) {
          free((void *)c->key);
          if (c->type == EOS_CFG_STR && c->val.s) free(c->val.s);
        }
        free(dev->cfg);
      }
      bzero(dev, sizeof(eos_dev_t));
    }
    return 1;
  }

  printf("Attached %s/%s as /dev/%s (parent: %s)\n",
         scope, drv_name, dev->name, parent->name[0] ? parent->name : "root");
  return 0;

cleanup_err:
  for (size_t k = 0; k < pin_count; k++) {
    if (parsed_pins[k].key) free(parsed_pins[k].key);
  }
  for (size_t k = 0; k < cfg_count; k++) {
    if (parsed_cfg[k].key) free((void *)parsed_cfg[k].key);
    if (parsed_cfg[k].type == EOS_CFG_STR && parsed_cfg[k].val.s) {
      free(parsed_cfg[k].val.s);
    }
  }
  return 1;
}

static int cmd_detach(const char *dev_name) {
  if (!dev_name || !dev_name[0]) {
    fprintf(stderr, "Usage: devctl detach <device_name>\n");
    return 1;
  }

  eos_dev_t *dev = eos_dev_find_by_name(dev_name);
  if (!dev) {
    fprintf(stderr, "devctl: device \"%s\" not found\n", dev_name);
    return 1;
  }

  if (dev == eos_devtree_root()) {
    fprintf(stderr, "devctl: cannot detach root bus device\n");
    return 1;
  }

  char name[EOS_SMALL_STR_LEN];
  strncpy(name, dev->name, sizeof(name) - 1);
  name[sizeof(name) - 1] = '\0';

  int rc = eos_dev_detach(dev);
  if (rc != 0) {
    fprintf(stderr, "devctl: failed to detach /dev/%s: %s\n", name, strerror(errno));
    return 1;
  }

  printf("Detached /dev/%s\n", name);
  return 0;
}

static int cmd_info(const char *dev_name) {
  if (!dev_name || !dev_name[0]) {
    fprintf(stderr, "Usage: devctl info <device_name>\n");
    return 1;
  }

  eos_dev_t *dev = eos_dev_find_by_name(dev_name);
  if (!dev) {
    fprintf(stderr, "devctl: device \"%s\" not found\n", dev_name);
    return 1;
  }

  printf("Device:     /dev/%s\n", dev->name[0] ? dev->name : "(unnamed)");
  printf("ID:         %" PRIu32 "\n", dev->id);
  printf("Driver:     %s/%s\n",
         dev->driver ? dev->driver->scope : "-",
         dev->driver ? dev->driver->name : "-");
  printf("DevName:    %s\n", dev->driver ? dev->driver->devname : "-");
  printf("Parent:     %s\n",
         dev->parent ? (dev->parent->name[0] ? dev->parent->name : "root") : "none");

  printf("Children:   ");
  eos_dev_t *ch = dev->child;
  if (!ch) {
    printf("(none)\n");
  } else {
    bool first = true;
    while (ch) {
      if (!first) printf(", ");
      printf("%s", ch->name[0] ? ch->name : "(unnamed)");
      first = false;
      ch = ch->next;
    }
    printf("\n");
  }

  if (dev->pins && dev->pins[0].key) {
    printf("Pins:       ");
    bool first = true;
    for (eos_pin_t *p = dev->pins; p->key != NULL; p++) {
      if (!first) printf(", ");
      printf("%s=%" PRId32, p->key, p->val);
      first = false;
    }
    printf("\n");
  }

  if (dev->cfg && dev->cfg[0].key) {
    printf("Config:     ");
    bool first = true;
    for (eos_cfg_t *c = dev->cfg; c->key != NULL; c++) {
      if (!first) printf(", ");
      if (c->type == EOS_CFG_INT) {
        printf("%s=%" PRId32, c->key, c->val.i);
      } else if (c->type == EOS_CFG_FLOAT) {
        printf("%s=%.2f", c->key, (double)c->val.f);
      } else if (c->type == EOS_CFG_STR) {
        printf("%s=\"%s\"", c->key, c->val.s ? c->val.s : "");
      } else if (c->type == EOS_CFG_PTR) {
        printf("%s=%p", c->key, c->val.ptr);
      }
      first = false;
    }
    printf("\n");
  }

  return 0;
}

int devctl_main(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return 0;
  }

  const char *cmd = argv[1];
  if (strcmp(cmd, "scopes") == 0) {
    return cmd_scopes();
  } else if (strcmp(cmd, "drivers") == 0) {
    return cmd_drivers(argc >= 3 ? argv[2] : NULL);
  } else if (strcmp(cmd, "devices") == 0 || strcmp(cmd, "list") == 0) {
    return cmd_devices();
  } else if (strcmp(cmd, "attach") == 0) {
    return cmd_attach(argc, argv);
  } else if (strcmp(cmd, "detach") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Usage: devctl detach <dev_name>\n");
      return 1;
    }
    return cmd_detach(argv[2]);
  } else if (strcmp(cmd, "info") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Usage: devctl info <dev_name>\n");
      return 1;
    }
    return cmd_info(argv[2]);
  } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
    print_usage();
    return 0;
  } else {
    fprintf(stderr, "devctl: unknown command \"%s\"\n", cmd);
    print_usage();
    return 1;
  }
}

EOS_BIN_ATTR eos_bin_t devctl_bin = {
    EOS_BIN_INITIALIZER,
    .filename = "devctl",
    .name = "Device Control Utility",
    .group = "System",
    .description = "Inspect /proc, manage and attach/detach devices at runtime",
    .entry_point = devctl_main};
