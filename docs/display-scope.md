# Display Driver Scope

Display drivers expose a `/dev` interface to output graphics frames. Multiple displays can be hot-swapped if they implement identical semantics (framebuffer size, color depth). The actual panel is chosen at runtime via device tree initialization.

## Frame Format Contract

The driver accepts `ioctl(WRITE)` to set the framebuffer mode (width, height, bit depth) and then writes the scan-out buffer to the appropriate interface (e.g., SPI or parallel LCD).

```c
// Example frame commit via ioctl():  
ioctl(fd, EOS_IOCTL_DISPLAY_SET_MODE(width, ST7789_RGB565));  
write(fd, buf->data(), width * height * 2); // Frame pixels (BGR layout)
```

## Initialization & Hot-Swap

*   **init():** Initializes the TFT controller via register map.
*   **shutdown():** Powers off the display and releases GPIOs.
*   **Hot-swap:** Drivers must handle mode changes seamlessly (e.g., swapping panels with different SPI timings).

## Adding a Custom Display Driver

1.  **Placement:** Place file in `display/<panel>.c`.
2.  **Guard:** Use an `#ifdef` guard (e.g., `EOS_DRIVER_DISPLAY_OLED_ENABLED`).
3.  **Registration:** Register via `EOSS_DRIVER_REG(display, <name>)`.

---
[Back to EOS Device Model Index](../Readme.md) | [See Bus Drivers](./bus-driver-scope.md)
