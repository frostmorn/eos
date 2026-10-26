# Network Driver Scope & TCP/IP Architecture

EOS provides comprehensive networking support integrating Espressif's ESP-IDF TCP/IP stack (`esp_netif`), Wi-Fi station (STA) and access point (AP) modes, network bridging, and VFS-accessible network management commands.

---

## 1. Network Subsystem & Stack Initialization (`net_common.c`, `netif.h`)

*   **Core Stack Initialization (`eos_net_ensure_init`):** Initializes NVS flash, the underlying lwIP TCP/IP stack, `esp_netif`, and the default system event loop.
*   **Wi-Fi Initialization (`eos_net_wifi_ensure_init`):** Initializes Wi-Fi with default configurations (`WIFI_INIT_CONFIG_DEFAULT`) and allocates operational netif structures.
*   **Netif & Device Tracking:** Maintains pointers for Station netif (`esp_netif_t`), AP netif (`esp_netif_t`), and respective driver devices (`eos_dev_t`) via `net_common.h`.

---

## 2. Implemented Network Drivers (`edriver/net/`)

EOS includes modular drivers for wireless and network functions under `main/edriver/net/`:
*   **Wi-Fi Core (`wifi/`):** Manages physical radio state, power management, event callbacks, and mode updates (`WIFI_MODE_STA`, `WIFI_MODE_AP`, `WIFI_MODE_APSTA`).
*   **Station Mode (`sta/`):** Connects EOS to external wireless access points, handling DHCP client assignment and association timeouts.
*   **Access Point Mode (`ap/`):** Hosts a local wireless access point, managing DHCP server configuration and client associations.
*   **Network Bridge (`bridge/`):** Bridges network interfaces for routing and forwarding.

---

## 3. Network Management Binaries (`ebin/`)

EOS exposes network control through built-in binaries:
*   **`wlanctl`:** Manages wireless adapter state, scanning, connecting, and status queries.
*   **`netctl`:** Configures network interfaces, IP addresses, and routing tables.
*   **`lvgl_net`:** Provides an interactive graphical interface for monitoring and configuring network connections.

---
[Back to Documentation Index](./Readme.md) | [See Storage Drivers](./storage-driver-scope.md)
