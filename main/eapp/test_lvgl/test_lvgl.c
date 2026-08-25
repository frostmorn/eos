#include "ecore/app.h"
#include "ecore/error.h"
#include "ecore/ioctl.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <lvgl.h>

static int fb_fd = -1;

static void lvgl_flush_cb(lv_display_t *display,
                          const lv_area_t *area,
                          uint8_t *px_map)
{
    /*
     * The EOS ST7789 driver at a725313 currently treats every
     * write() as a complete framebuffer:
     *
     *     esp_lcd_panel_draw_bitmap(panel, 0, 0,
     *                                width, height, buf);
     *
     * Therefore we intentionally use LV_DISPLAY_RENDER_MODE_DIRECT
     * with a full-screen buffer and ignore the dirty area here.
     */
    (void)area;

    uint32_t width = lv_display_get_horizontal_resolution(display);
    uint32_t height = lv_display_get_vertical_resolution(display);

    size_t size = (size_t)width * height * 2;

    ssize_t written = write(fb_fd, px_map, size);

    if (written != (ssize_t)size) {
        printf("LVGL: display write failed: %d/%d bytes\n",
               (int)written,
               (int)size);
    }

    lv_display_flush_ready(display);
}

static uint32_t lvgl_tick_cb(void)
{
    /*
     * EOS/ESP-IDF provides esp_timer, but keep this adapter independent
     * of the ESP timer API for now.
     *
     * Replace this with the EOS time API if/when one is standardized.
     */
    static uint32_t tick;

    return ++tick;
}

static int test_lvgl_main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/dev/fb0";

    printf("Starting LVGL Display Test...\n");

    fb_fd = open(path, O_RDWR);

    if (fb_fd < 0) {
        perror("Failed to open display device");
        return -1;
    }

    int width;
    int height;

    if (ioctl(fb_fd, EOS_DISPLAY_IOCTL_GET_WIDTH, &width)
        != EOS_ERR_NO_ERROR) {
        printf("Failed to get display width\n");
        close(fb_fd);
        return -1;
    }

    if (ioctl(fb_fd, EOS_DISPLAY_IOCTL_GET_HEIGHT, &height)
        != EOS_ERR_NO_ERROR) {
        printf("Failed to get display height\n");
        close(fb_fd);
        return -1;
    }

    printf("Display resolution: %dx%d\n", width, height);

    /*
     * LVGL is pinned by EOS commit a725313 to ce07667.
     *
     * The ST7789 EOS driver is 16-bit RGB565, so one framebuffer
     * requires width * height * 2 bytes.
     */
    size_t framebuffer_size = (size_t)width * height * 2;

    void *framebuffer = malloc(framebuffer_size);

    if (!framebuffer) {
        perror("Failed to allocate LVGL framebuffer");
        close(fb_fd);
        return -1;
    }

    lv_init();

    /*
     * LVGL needs a millisecond tick source.
     *
     * TODO: replace this with the proper EOS monotonic-time API.
     */
    lv_tick_set_cb(lvgl_tick_cb);

    lv_display_t *display = lv_display_create(width, height);

    if (!display) {
        printf("Failed to create LVGL display\n");
        free(framebuffer);
        close(fb_fd);
        return -1;
    }

    /*
     * The EOS ST7789 driver uses 16-bit RGB565.
     *
     * DIRECT mode requires a full display-sized buffer.
     */
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);

    lv_display_set_buffers(
        display,
        framebuffer,
        NULL,
        framebuffer_size,
        LV_DISPLAY_RENDER_MODE_DIRECT
    );

    lv_display_set_flush_cb(display, lvgl_flush_cb);

    /*
     * Simple test UI.
     */
    lv_obj_t *label = lv_label_create(lv_screen_active());

    lv_label_set_text(label, "Hello from EOS + LVGL!");

    lv_obj_center(label);

    /*
     * LVGL application loop.
     */
    while (1) {
        uint32_t delay = lv_timer_handler();

        /*
         * Don't spin at 100% CPU.
         *
         * This is intentionally simple for the first test.
         */
        if (delay > 10)
            delay = 10;

        usleep(delay * 1000);
    }

    /*
     * Normally unreachable.
     */
    free(framebuffer);
    close(fb_fd);

    return 0;
}

EOS_NATIVE_APP_ATTR const eos_native_app_manifest_t lvgl_test_app = {
    .magic = EOS_NATIVE_APP_MAGIC,
    .filename = "test_lvgl",
    .name = "LVGL Test",
    .group = "Tools",
    .description = "LVGL display test",
    .entry_point = test_lvgl_main
};

