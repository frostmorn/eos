# Network Driver Scope & TCP/IP Architecture

EOS provides comprehensive networking support integrating Espressif's ESP-IDF TCP/IP stack (`esp_netif`), Wi-Fi station (STA) and access point (AP) modes, network bridging, and VFS-accessible network management commands.

---

## 1. Network Subsystem & Stack Architecture (`edriver/net/wifi/`, `netif.h`)

*   **Parent Driver Encapsulation:** The Wi-Fi driver (`edriver/net/wifi`) encapsulates NVS flash, lwIP TCP/IP stack (`esp_netif`), the default system event loop, and physical Wi-Fi radio lifecycle in its `dev->state`.
*   **Child Driver Delegation via IOCTL:** Child station (`sta0`) and access point (`ap0`) drivers coordinate Wi-Fi initialization, operational modes (`WIFI_MODE_STA`, `WIFI_MODE_AP`, `WIFI_MODE_APSTA`), and connection synchronization exclusively through `ioctl` calls on their parent (`dev->parent`, e.g. `EOS_WIFI_IOCTL_*`).
*   **Decoupled State:** No global driver variables or loose cross-driver C functions are used; all cross-driver communication is strictly mediated by the Unified Device Model and IOCTL interfaces.
*   **Attachment Topologies:** On SoCs with integrated Wi-Fi radios (such as ESP32-S3), the Wi-Fi driver attaches directly to `root` (`devctl attach net wifi root`). On dual-chip boards without onboard radio silicon (such as ESP32-P4 on the Guition JC4880P4), the Wi-Fi driver attaches to an SDIO bus slot (`devctl attach net wifi sdio1 --pin chip_pu=54`), communicating with the external co-processor (ESP32-C6) via ESP-Hosted SDIO.

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
[Back to Documentation Index](./Readme.md) | [See Storage Drivers](./storage-driver-scope.md) | [JC4880P4 Co-Processor Guide](./jc4880p4-coprocessor-guide.md)
