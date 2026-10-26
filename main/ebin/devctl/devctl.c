#include "includes.h"
#include "ecore/bin.h"
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/log.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
  printf("Usage: devctl <command> [args...]\n\n"
         "Commands:\n"
         "  scopes                     List available driver scopes (from /proc/scopes)\n"
         "  drivers [scope]            List registered drivers (from /proc/drivers)\n"
         "  devices                    List active devtree devices (from /proc/devices)\n"
         "  attach <scope> <drv> [par] Attach a device dynamically (default parent: root)\n"
         "  detach <dev_name>          Detach a device dynamically (e.g. devctl detach br0)\n"
         "  info <dev_name>            Show detailed device information\n");
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

static int cmd_attach(const char *scope, const char *drv_name, const char *parent_name) {
  if (!scope || !drv_name) {
    fprintf(stderr, "Usage: devctl attach <scope> <driver> [parent]\n");
    return 1;
  }

  eos_drv_t *drv = eos_drv_find(scope, drv_name);
  if (!drv) {
    fprintf(stderr, "devctl: driver \"%s/%s\" not found\n", scope, drv_name);
    return 1;
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
      return 1;
    }
  } else {
    parent = eos_devtree_root();
  }

  eos_dev_t *dev = eos_dev_alloc();
  if (!dev) {
    fprintf(stderr, "devctl: failed to allocate device slot (ENFILE)\n");
    return 1;
  }

  dev->driver = drv;
  int rc = eos_dev_attach(dev, parent);
  if (rc != 0) {
    fprintf(stderr, "devctl: failed to attach %s/%s under %s: %s\n",
            scope, drv_name, parent->name[0] ? parent->name : "root", strerror(errno));
    return 1;
  }

  printf("Attached %s/%s as /dev/%s (parent: %s)\n",
         scope, drv_name, dev->name, parent->name[0] ? parent->name : "root");
  return 0;
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
    if (argc < 4) {
      fprintf(stderr, "Usage: devctl attach <scope> <driver> [parent]\n");
      return 1;
    }
    return cmd_attach(argv[2], argv[3], argc >= 5 ? argv[4] : NULL);
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
