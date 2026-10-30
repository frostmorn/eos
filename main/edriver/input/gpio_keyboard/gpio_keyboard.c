#include "includes.h"
#ifdef EOS_DRV_INPUT_GPIO_KEYBOARD_ENABLED

#include <driver/gpio.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <errno.h>

#include "ecore/capsmgr.h"
#include "ecore/dev.h"
#include "ecore/driver.h"
#include "ecore/input.h"
#include "ecore/ioctl.h"

#define GPIO_KEYBOARD_QUEUE_SIZE 32

typedef struct {
  int gpio_num;
  uint16_t key_code;
  bool last_pressed;
} gpio_keyboard_key_t;

typedef struct {
  gpio_keyboard_key_t keys[16];
  int key_count;
  SemaphoreHandle_t lock;
  SemaphoreHandle_t sem_event;
  bool shutting_down;
  eos_input_event_t queue[GPIO_KEYBOARD_QUEUE_SIZE];
  uint8_t q_head;
  uint8_t q_tail;
} gpio_keyboard_state_t;

static uint16_t map_key_name(const char *name) {
  if (!name) return KEY_RESERVED;
  if (strcmp(name, "up") == 0) return KEY_UP;
  if (strcmp(name, "down") == 0) return KEY_DOWN;
  if (strcmp(name, "left") == 0) return KEY_LEFT;
  if (strcmp(name, "right") == 0) return KEY_RIGHT;
  if (strcmp(name, "select") == 0 || strcmp(name, "enter") == 0) return KEY_ENTER;
  if (strcmp(name, "start") == 0 || strcmp(name, "space") == 0) return KEY_SPACE;
  if (strcmp(name, "esc") == 0 || strcmp(name, "escape") == 0) return KEY_ESC;
  if (strcmp(name, "a") == 0) return KEY_A;
  if (strcmp(name, "b") == 0) return KEY_B;
  if (strcmp(name, "c") == 0) return KEY_C;
  if (strcmp(name, "d") == 0) return KEY_D;
  if (strcmp(name, "sleep") == 0) return KEY_SLEEP;
  return KEY_RESERVED;
}

static void gpio_keyboard_enqueue_event(gpio_keyboard_state_t *state, const eos_input_event_t *ev) {
  if (!state || !ev) return;
  if (state->lock) xSemaphoreTake(state->lock, portMAX_DELAY);
  uint8_t next = (uint8_t)((state->q_head + 1U) % GPIO_KEYBOARD_QUEUE_SIZE);
  if (next == state->q_tail) {
    state->q_tail = (uint8_t)((state->q_tail + 1U) % GPIO_KEYBOARD_QUEUE_SIZE);
  }
  state->queue[state->q_head] = *ev;
  state->q_head = next;
  if (state->lock) xSemaphoreGive(state->lock);
  if (state->sem_event) {
    xSemaphoreGive(state->sem_event);
  }
}

static bool gpio_keyboard_dequeue_event(gpio_keyboard_state_t *state, eos_input_event_t *ev) {
  if (!state || !ev) return false;
  bool got = false;
  if (state->lock) xSemaphoreTake(state->lock, portMAX_DELAY);
  if (state->q_head != state->q_tail) {
    *ev = state->queue[state->q_tail];
    state->q_tail = (uint8_t)((state->q_tail + 1U) % GPIO_KEYBOARD_QUEUE_SIZE);
    got = true;
  }
  if (state->lock) xSemaphoreGive(state->lock);
  return got;
}

bool driver_input_gpio_keyboard_init(eos_dev_t *dev) {
  gpio_keyboard_state_t *state = malloc(sizeof(gpio_keyboard_state_t));
  if (!state) return false;
  memset(state, 0, sizeof(gpio_keyboard_state_t));
  state->lock = xSemaphoreCreateMutex();
  state->sem_event = xSemaphoreCreateCounting(GPIO_KEYBOARD_QUEUE_SIZE, 0);
  dev->state = state;

  if (dev->pins) {
    for (eos_pin_t *p = dev->pins; p->key != NULL; p++) {
      if (state->key_count >= 16) break;
      int gpio_num = p->val;
      if (!eos_cap_claim(EOS_CAPS_GPIO, gpio_num, dev)) {
        for (int j = 0; j < state->key_count; j++) {
          eos_cap_release(EOS_CAPS_GPIO, state->keys[j].gpio_num, dev);
        }
        if (state->sem_event) {
          vSemaphoreDelete(state->sem_event);
        }
        if (state->lock) {
          vSemaphoreDelete(state->lock);
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

void driver_input_gpio_keyboard_shutdown(eos_dev_t *dev) {
  if (!dev || !dev->state) return;
  gpio_keyboard_state_t *state = dev->state;
  state->shutting_down = true;
  for (int i = 0; i < state->key_count; i++) {
    eos_cap_release(EOS_CAPS_GPIO, state->keys[i].gpio_num, dev);
  }
  if (state->sem_event) {
    xSemaphoreGive(state->sem_event);
    vSemaphoreDelete(state->sem_event);
    state->sem_event = NULL;
  }
  if (state->lock) {
    vSemaphoreDelete(state->lock);
    state->lock = NULL;
  }
  free(state);
  dev->state = NULL;
}

ssize_t driver_input_gpio_keyboard_read(eos_dev_t *dev, int fd, void *dst, size_t size) {
  (void)fd;
  if (size < sizeof(eos_input_event_t)) {
    errno = EINVAL;
    return -1;
  }
  if (!dst) {
    errno = EFAULT;
    return -1;
  }

  gpio_keyboard_state_t *state = dev->state;
  if (!state) {
    errno = EBADF;
    return -1;
  }

  while (!state->shutting_down) {
    if (gpio_keyboard_dequeue_event(state, dst)) {
      return sizeof(eos_input_event_t);
    }

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

    if (state->sem_event) {
      xSemaphoreTake(state->sem_event, pdMS_TO_TICKS(10));
    } else {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }

  errno = EIO;
  return -1;
}

ssize_t driver_input_gpio_keyboard_write(eos_dev_t *dev, int fd, const void *src, size_t size) {
  (void)fd;
  if (!dev || !dev->state) {
    errno = EBADF;
    return -1;
  }
  if (!src || size == 0) {
    return 0;
  }

  gpio_keyboard_state_t *state = dev->state;

  if (size >= sizeof(eos_input_event_t) && (size % sizeof(eos_input_event_t) == 0)) {
    const eos_input_event_t *events = (const eos_input_event_t *)src;
    size_t count = size / sizeof(eos_input_event_t);
    for (size_t i = 0; i < count; i++) {
      gpio_keyboard_enqueue_event(state, &events[i]);
    }
    return (ssize_t)size;
  }

  char buf[128];
  size_t copy_len = size < (sizeof(buf) - 1) ? size : (sizeof(buf) - 1);
  memcpy(buf, src, copy_len);
  buf[copy_len] = '\0';

  char *saveptr = NULL;
  char *tok = strtok_r(buf, " \t\r\n,", &saveptr);

  while (tok) {
    uint16_t code = map_key_name(tok);
    if (code == KEY_RESERVED) {
      if (tok[0] >= '0' && tok[0] <= '9') {
        int val = atoi(tok);
        if (val > 0 && val < 512) {
          code = (uint16_t)val;
        }
      } else if (strlen(tok) == 1) {
        char ch = tok[0];
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
        char s[2] = {ch, '\0'};
        code = map_key_name(s);
      }
    }

    if (code != KEY_RESERVED) {
      eos_input_event_t ev = {
          .type = EOS_INPUT_EVENT_KEYBOARD,
          .code = code,
      };
      gpio_keyboard_enqueue_event(state, &ev);
    }
    tok = strtok_r(NULL, " \t\r\n,", &saveptr);
  }

  return (ssize_t)size;
}

int driver_input_gpio_keyboard_ioctl(eos_dev_t *dev, int fd, int cmd, va_list args) {
  (void)fd;
  gpio_keyboard_state_t *state = dev->state;
  if (!state) return -1;

  switch (cmd) {
    case EOS_KEYBOARD_IOCTL_POLL: {
      eos_input_event_t *ev = va_arg(args, eos_input_event_t *);
      if (!ev) return -1;
      if (gpio_keyboard_dequeue_event(state, ev)) {
        return 1;
      }
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

EOS_DRV_ATTR eos_drv_t driver_input_gpio_keyboard = {
    EOS_DRV_INIT,
    .scope = "input",
    .name = "gpio_keyboard",
    .devname = "keyboard",
    .init = driver_input_gpio_keyboard_init,
    .shutdown = driver_input_gpio_keyboard_shutdown,
    .read = driver_input_gpio_keyboard_read,
    .write = driver_input_gpio_keyboard_write,
    .ioctl = driver_input_gpio_keyboard_ioctl,
};

#endif
