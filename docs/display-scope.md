# Display Driver Scope & Framebuffer Architecture

Display drivers expose a standard VFS interface under `/dev` (typically `/dev/fb*`) for rendering graphics frames. Multiple display panels can be utilized or hot-swapped provided they adhere to compatible display semantics (such as framebuffer dimensions and color depth). The active panel is selected at runtime via device tree initialization.

---

## Supported Display Controllers (`edriver/display/`)

EOS includes drivers for popular display panels:
*   **`st7789`:** Standard SPI/parallel TFT LCD controller for general embedded displays.
*   **`st7701s`:** Advanced MIPI-DSI / RGB parallel display controller supporting high-resolution panels (e.g., JC4880P443C boards).

---

## Frame Format & IOCTL Contract

Display drivers accept standard POSIX file operations (`write`) combined with `ioctl` calls to query dimensions or configure display windows.

```c
// Example frame rendering via VFS ioctl and write:
int width, height;
ioctl(fd, EOS_DISPLAY_IOCTL_GET_WIDTH, &width);
ioctl(fd, EOS_DISPLAY_IOCTL_GET_HEIGHT, &height);

// Write scan-out buffer (e.g., RGB565 / BGR layout)
write(fd, frame_buffer, width * height * 2);
```

---

## Lifecycle Operations

*   **`init(eos_dev_t *dev)`:** Initializes the TFT controller via SPI/parallel register configuration, allocating panel state and DMA buffers.
*   **`shutdown(eos_dev_t *dev)`:** Powers off the display panel, de-initializes hardware interfaces, and releases allocated GPIO capability pins.
*   **Hot-swapping:** Drivers support seamless transition of display targets and window sizing across panel switches.

---

## Adding a Custom Display Driver

1.  **Placement:** Create your driver implementation under `main/edriver/display/<panel>/<panel>.c`.
2.  **Guarding:** Guard the driver with an appropriate configuration macro (e.g., `EOS_DRIVER_DISPLAY_<NAME>_ENABLED`).
3.  **Registration:** Register the driver using the standard macro (`EOS_DRIVER_REG(display, <name>, priority)`).

---
[Back to Documentation Index](./Readme.md) | [See Sound Drivers](./sound-driver-scope.md)
