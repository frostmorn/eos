#include "includes.h"
#ifdef EOS_DRV_INPUT_KEYBOARD_ENABLED

#include <driver/gpio.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <errno.h>

#include "ecore/capsmgr.h"
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/input.h"
#include "ecore/ioctl.h"

typedef struct {
  int gpio_num;
  uint16_t key_code;
  bool last_pressed;
} keyboard_key_t;

typedef struct {
  keyboard_key_t keys[16];
  int key_count;
} keyboard_state_t;

static uint16_t map_key_name(const char *name) {
  if (!name) return KEY_RESERVED;
  if (strcmp(name, "up") == 0) return KEY_UP;
  if (strcmp(name, "down") == 0) return KEY_DOWN;
  if (strcmp(name, "left") == 0) return KEY_LEFT;
  if (strcmp(name, "right") == 0) return KEY_RIGHT;
  if (strcmp(name, "select") == 0) return KEY_ENTER;
  if (strcmp(name, "start") == 0) return KEY_SPACE;
  if (strcmp(name, "a") == 0) return KEY_A;
  if (strcmp(name, "b") == 0) return KEY_B;
  if (strcmp(name, "c") == 0) return KEY_C;
  if (strcmp(name, "d") == 0) return KEY_D;
  if (strcmp(name, "sleep") == 0) return KEY_SLEEP;
  return KEY_RESERVED;
}

bool driver_input_keyboard_init(eos_dev_t *dev) {
  keyboard_state_t *state = malloc(sizeof(keyboard_state_t));
  if (!state) return false;
  memset(state, 0, sizeof(keyboard_state_t));
  dev->state = state;

  if (dev->pins) {
    for (eos_pin_t *p = dev->pins; p->key != NULL; p++) {
      if (state->key_count >= 16) break;
      int gpio_num = p->val;
      if (!eos_cap_claim(EOS_CAPS_GPIO, gpio_num, dev)) {
        for (int j = 0; j < state->key_count; j++) {
          eos_cap_release(EOS_CAPS_GPIO, state->keys[j].gpio_num, dev);
        }
        free(state);
        dev->state = NULL;
        return false;
      }

      gpio_config_t io_conf = {
          .pin_bit_mask = BIT64(gpio_num),
          .mode = GPIO_MODE_INPUT,
          .pull_up_en = GPIO_PULLUP_ENABLE,
          .pull_down_en = GPIO_PULLDOWN_DISABLE,
          .intr_type = GPIO_INTR_DISABLE,
      };
      gpio_config(&io_conf);

      state->keys[state->key_count].gpio_num = gpio_num;
      state->keys[state->key_count].key_code = map_key_name(p->key);
      state->keys[state->key_count].last_pressed = (gpio_get_level(gpio_num) == 0);
      state->key_count++;
    }
  }

  return true;
}

void driver_input_keyboard_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) return;
  keyboard_state_t *state = dev->state;
  for (int i = 0; i < state->key_count; i++) {
    eos_cap_release(EOS_CAPS_GPIO, state->keys[i].gpio_num, dev);
  }
  free(state);
  dev->state = NULL;
}

ssize_t driver_input_keyboard_read(eos_dev_t *dev, int fd, void *dst, size_t size) {
  if (size < sizeof(eos_input_event_t)) {
    errno = EINVAL;
    return -1;
  }
  if (!dst) {
    errno = EFAULT;
    return -1;
  }

  keyboard_state_t *state = dev->state;
  if (!state) {
    errno = EBADF;
    return -1;
  }

  while (1) {
    for (int i = 0; i < state->key_count; i++) {
      int level = gpio_get_level(state->keys[i].gpio_num);
      bool pressed = (level == 0); // Active low
      if (pressed && !state->keys[i].last_pressed) {
        state->keys[i].last_pressed = true;
        eos_input_event_t *ev = dst;
        ev->type = EOS_INPUT_EVENT_KEYBOARD;
        ev->code = state->keys[i].key_code;
        return sizeof(eos_input_event_t);
      } else if (!pressed) {
        state->keys[i].last_pressed = false;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

int driver_input_keyboard_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  keyboard_state_t *state = dev->state;
  if (!state) return -1;

  switch (cmd) {
    case EOS_KEYBOARD_IOCTL_POLL: {
      eos_input_event_t *ev = va_arg(args, eos_input_event_t *);
      if (!ev) return -1;
      for (int i = 0; i < state->key_count; i++) {
        int level = gpio_get_level(state->keys[i].gpio_num);
        bool pressed = (level == 0); // Active low
        if (pressed && !state->keys[i].last_pressed) {
          state->keys[i].last_pressed = true;
          ev->type = EOS_INPUT_EVENT_KEYBOARD;
          ev->code = state->keys[i].key_code;
          return 1;
        } else if (!pressed) {
          state->keys[i].last_pressed = false;
        }
      }
      return 0;
    }
    default:
      return -1;
  }
}

EOS_DRV_ATTR eos_drv_t driver_input_keyboard = {
    EOS_DRV_INIT,
    .scope = "input",
    .name = "keyboard",
    .devname = "keyboard",
    .init = driver_input_keyboard_init,
    .shutdown = driver_input_keyboard_shutdown,
    .read = driver_input_keyboard_read,
    .ioctl = driver_input_keyboard_ioctl,
};

#endif
