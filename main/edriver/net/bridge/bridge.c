#include "includes.h"
#ifdef EOS_DRV_NET_BRIDGE_ENABLED

#include "ecore/driver.h"
#include "ecore/ioctl.h"
#include "ecore/log.h"
#include "ecore/netif.h"
#include "ecore/net_common.h"

#include <errno.h>
#include <string.h>

#define BRIDGE_MAX_PORTS 8

typedef struct {
  bool is_up;
  bool napt_enabled;
  char ports[BRIDGE_MAX_PORTS][16];
  uint8_t port_count;
  char napt_uplink[16];
  eos_netif_ip_info_t ip_info;
} bridge_state_t;

bool driver_net_bridge_init(eos_dev_t *dev) {
  bridge_state_t *st = calloc(1, sizeof(bridge_state_t));
  if (!st) return false;

  st->is_up = false;
  st->napt_enabled = false;
  st->port_count = 0;
  strncpy(st->napt_uplink, "sta0", sizeof(st->napt_uplink) - 1);
  strncpy(st->ip_info.ip, "192.168.4.1", sizeof(st->ip_info.ip) - 1);
  strncpy(st->ip_info.netmask, "255.255.255.0", sizeof(st->ip_info.netmask) - 1);
  strncpy(st->ip_info.gw, "192.168.4.1", sizeof(st->ip_info.gw) - 1);

  dev->state = st;
  EOS_LOGI("bridge: virtual network bridge initialized (/dev/%s)", dev->name[0] ? dev->name : "br0");
  return true;
}

void driver_net_bridge_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) return;
  bridge_state_t *st = (bridge_state_t *)dev->state;

  if (st->napt_enabled) {
    eos_net_set_napt_enabled(false);
  }

  free(st);
  dev->state = NULL;
}

int driver_net_bridge_open(eos_dev_t *dev, const char *path, int flags, int mode) {
  (void)dev;
  (void)path;
  (void)flags;
  (void)mode;
  return 0;
}

int driver_net_bridge_close(eos_dev_t *dev, int fd) {
  (void)dev;
  (void)fd;
  return 0;
}

int driver_net_bridge_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  if (!dev || !dev->state) return -1;
  bridge_state_t *st = (bridge_state_t *)dev->state;

  switch (cmd) {
    case EOS_NETIF_IOCTL_GET_INFO: {
      eos_netif_info_t *info = va_arg(args, eos_netif_info_t *);
      if (!info) return -1;
      memset(info, 0, sizeof(*info));
      strncpy(info->name, dev->name[0] ? dev->name : "br0", sizeof(info->name) - 1);
      info->type = EOS_NETIF_TYPE_BRIDGE;
      info->flags = EOS_NETIF_FLAG_BRIDGE;
      if (st->is_up) info->flags |= EOS_NETIF_FLAG_UP;
      if (st->port_count > 0) info->flags |= EOS_NETIF_FLAG_LINK_UP;

      info->ip = st->ip_info;

      char port_list[64] = {0};
      for (size_t i = 0; i < st->port_count; i++) {
        if (i > 0) strncat(port_list, ", ", sizeof(port_list) - strlen(port_list) - 1);
        strncat(port_list, st->ports[i], sizeof(port_list) - strlen(port_list) - 1);
      }
      snprintf(info->extra, sizeof(info->extra), "ports: [%.48s] (NAPT: %s)",
               st->port_count > 0 ? port_list : "none",
               st->napt_enabled ? "enabled" : "disabled");
      return 0;
    }

    case EOS_NETIF_IOCTL_SET_UP: {
      st->is_up = true;
      if (st->napt_enabled) {
        eos_net_set_napt_enabled(true);
      }
      EOS_LOGI("bridge: %s set to UP", dev->name[0] ? dev->name : "br0");
      return 0;
    }

    case EOS_NETIF_IOCTL_SET_DOWN: {
      st->is_up = false;
      if (st->napt_enabled) {
        eos_net_set_napt_enabled(false);
      }
      EOS_LOGI("bridge: %s set to DOWN", dev->name[0] ? dev->name : "br0");
      return 0;
    }

    case EOS_NETIF_IOCTL_SET_IP: {
      const eos_netif_ip_info_t *ip_info = va_arg(args, const eos_netif_ip_info_t *);
      if (!ip_info) return -1;
      st->ip_info = *ip_info;
      return 0;
    }

    case EOS_NETIF_IOCTL_GET_IP: {
      eos_netif_ip_info_t *out_ip = va_arg(args, eos_netif_ip_info_t *);
      if (!out_ip) return -1;
      *out_ip = st->ip_info;
      return 0;
    }

    case EOS_NETIF_IOCTL_BRIDGE_ADD_IF: {
      const char *ifname = va_arg(args, const char *);
      if (!ifname || ifname[0] == '\0') return -1;

      // Check if already in member list
      for (size_t i = 0; i < st->port_count; i++) {
        if (strcmp(st->ports[i], ifname) == 0) {
          return 0; // Already added
        }
      }

      if (st->port_count >= BRIDGE_MAX_PORTS) {
        errno = ENOSPC;
        return -1;
      }

      strncpy(st->ports[st->port_count], ifname, 15);
      st->ports[st->port_count][15] = '\0';
      st->port_count++;

      // Check for uplink interface (e.g. sta or eth) and local AP/downlink
      bool has_uplink = false;
      bool has_downlink = false;
      for (size_t i = 0; i < st->port_count; i++) {
        if (strstr(st->ports[i], "sta") || strstr(st->ports[i], "eth")) {
          has_uplink = true;
          strncpy(st->napt_uplink, st->ports[i], sizeof(st->napt_uplink) - 1);
        }
        if (strstr(st->ports[i], "ap")) has_downlink = true;
      }
      if (has_uplink && has_downlink) {
        st->napt_enabled = true;
        eos_net_set_napt_enabled(true);
      }

      EOS_LOGI("bridge: attached port \"%s\" to %s (total ports: %d)",
               ifname, dev->name[0] ? dev->name : "br0", st->port_count);
      return 0;
    }

    case EOS_NETIF_IOCTL_BRIDGE_DEL_IF: {
      const char *ifname = va_arg(args, const char *);
      if (!ifname || ifname[0] == '\0') return -1;

      int idx = -1;
      for (size_t i = 0; i < st->port_count; i++) {
        if (strcmp(st->ports[i], ifname) == 0) {
          idx = (int)i;
          break;
        }
      }
      if (idx < 0) {
        errno = ENOENT;
        return -1;
      }

      for (size_t i = (size_t)idx; i + 1 < st->port_count; i++) {
        memcpy(st->ports[i], st->ports[i + 1], 16);
      }
      st->port_count--;

      // If removed either uplink or downlink, disable NAPT
      bool has_uplink = false;
      bool has_downlink = false;
      for (size_t i = 0; i < st->port_count; i++) {
        if (strstr(st->ports[i], "sta") || strstr(st->ports[i], "eth")) has_uplink = true;
        if (strstr(st->ports[i], "ap")) has_downlink = true;
      }
      if (!has_uplink || !has_downlink) {
        st->napt_enabled = false;
        eos_net_set_napt_enabled(false);
      }

      EOS_LOGI("bridge: removed port \"%s\" from %s",
               ifname, dev->name[0] ? dev->name : "br0");
      return 0;
    }

    case EOS_NETIF_IOCTL_BRIDGE_GET_INFO: {
      eos_netif_bridge_info_t *binfo = va_arg(args, eos_netif_bridge_info_t *);
      if (!binfo) return -1;
      memset(binfo, 0, sizeof(*binfo));
      strncpy(binfo->name, dev->name[0] ? dev->name : "br0", sizeof(binfo->name) - 1);
      binfo->port_count = st->port_count;
      binfo->napt_enabled = st->napt_enabled;
      strncpy(binfo->napt_uplink, st->napt_uplink, sizeof(binfo->napt_uplink) - 1);
      for (size_t i = 0; i < st->port_count; i++) {
        strncpy(binfo->ports[i], st->ports[i], sizeof(binfo->ports[i]) - 1);
      }
      return 0;
    }

    case EOS_NETIF_IOCTL_BRIDGE_SET_NAPT: {
      bool enable = (bool)va_arg(args, int);
      st->napt_enabled = enable;
      return eos_net_set_napt_enabled(enable);
    }

    default:
      return -1;
  }
}

EOS_DRV_ATTR eos_drv_t driver_net_bridge = {
    EOS_DRV_INIT,
    .scope = "net",
    .name = "bridge",
    .devname = "br",
    .init = driver_net_bridge_init,
    .shutdown = driver_net_bridge_shutdown,
    .open = driver_net_bridge_open,
    .close = driver_net_bridge_close,
    .ioctl = driver_net_bridge_ioctl,
};

#endif
