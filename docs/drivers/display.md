# Display Driver Contracts & Framebuffer Architecture

Display drivers in EOS reside under `scope == "display"`, exposing standard Virtual Filesystem character devices (typically `/dev/fb0`) that receive graphics frame streams and configure display windows.

---

## 1. Supported Display Controllers (`main/edriver/display/`)

| Driver Name | Hardware Interface | Target Panels & Hardware | Typical Resolution & Format |
| :--- | :--- | :--- | :--- |
| **`st7789`** | SPI / Parallel LCD | Lilka v2.3 handheld console | 240x240 or 280x240 RGB565 |
| **`st7701s`** | MIPI-DSI / RGB Parallel | Guition JC4880P4 (JC4880P443C) | 480x480 or 800x480 RGB565 |
| **`qemu_rgb`** | Espressif Virtual RGB | `esp32s3_qemu` emulator | 320x240 Virtual RGB565 |

---

## 2. VFS Framebuffer Contract

Display drivers accept standard POSIX file operations:

* **`open("/dev/fb0", O_RDWR)`**: Prepares display hardware for rendering.
* **`write(fd, buffer, bytes)`**: Transmits an uncompressed pixel stream into the active display window. For RGB565 format, each pixel consumes exactly 2 bytes in little-endian order:
  $$\text{byte\_count} = \text{width} \times \text{height} \times 2$$
* **`close(fd)`**: Releases process-level display file handle.

### Example C Rendering Pipeline:
```c
int fd = open("/dev/fb0", O_RDWR);

// 1. Query display geometry
int width = 0, height = 0;
ioctl(fd, EOS_DISPLAY_IOCTL_GET_WIDTH, &width);
ioctl(fd, EOS_DISPLAY_IOCTL_GET_HEIGHT, &height);

// 2. Configure full-screen window
eos_display_rect_t win = { .x = 0, .y = 0, .w = width, .h = height };
ioctl(fd, EOS_DISPLAY_IOCTL_SET_WINDOW, &win);

// 3. Write RGB565 scan-out buffer
write(fd, pixel_data, width * height * sizeof(uint16_t));
close(fd);
```

---

## 3. Display IOCTL Interface

All display commands reside in the reserved IOCTL range `200–299`:

| IOCTL Command | Code | Argument Type | Description |
| :--- | :---: | :--- | :--- |
| `EOS_DISPLAY_IOCTL_GET_WIDTH` | `200` | `int *out_width` | Queries active display panel horizontal resolution in pixels. |
| `EOS_DISPLAY_IOCTL_GET_HEIGHT` | `201` | `int *out_height` | Queries active display panel vertical resolution in pixels. |
| `EOS_DISPLAY_IOCTL_SET_WINDOW` | `202` | `const eos_display_rect_t *win` | Sets sub-window bounding box `(x, y, w, h)` for partial updates. |
| `EOS_DISPLAY_IOCTL_GET_WINDOW` | `203` | `eos_display_rect_t *out_win` | Queries current active bounding window coordinates. |
| `EOS_DISPLAY_IOCTL_FLUSH` | `204` | `none` | Flushes pending DMA strips or triggers frame synchronization. |

### Geometry Definition (`eos_display_rect_t`)
```c
typedef struct {
  uint32_t x;
  uint32_t y;
  uint32_t w;
  uint32_t h;
} eos_display_rect_t;
```

---

## 4. Hardware Constraints & DMA Rules

1. **DMA Buffer Placement (ESP32-S3):**
   * On ESP32-S3 (such as Lilka v2.3), the General SPI DMA hardware controller cannot source pixel streams directly from external Octal PSRAM.
   * Consequently, DMA line strip buffers inside `st7789` **must strictly allocate from internal SRAM** (`MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA`).
   * Application framebuffers in user space (e.g., LVGL draw buffers or game canvas) may reside in PSRAM; the driver copies strips into internal DMA memory before kicking off the SPI DMA descriptor.
2. **AXI Direct DMA (ESP32-P4):**
   * On ESP32-P4 (MIPI-DSI ST7701S), the high-performance 2D-DMA / AXI bus directly accesses external PSRAM, enabling full-resolution 480x480 double-buffering entirely in external RAM with zero internal SRAM overhead.

---

## 5. Declarative Attachment Examples

```sh
# Lilka v2.3: ST7789 over SPI host 1
devctl attach display st7789 spi0 --pin cs=7,dc=15,rst=45,bl=46 --cfg width=240,height=240,speed=80000000

# Guition JC4880P4: ST7701S over MIPI-DSI
devctl attach display st7701s root --pin rst=27,bl=26 --cfg width=480,height=480,lanes=2

# QEMU Emulator: Virtual RGB
devctl attach display qemu_rgb root --cfg width=320,height=240,bpp=16
```
