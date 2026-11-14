#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include <esp_timer.h>
#include "ecore/proc.h"
#include "ecore/log.h"
#include "emisc/fancymacro.h"

extern esp_err_t __real_esp_timer_create(const esp_timer_create_args_t *create_args,
                                         esp_timer_handle_t *out_handle);
extern esp_err_t __real_esp_timer_delete(esp_timer_handle_t timer);

static void cleanup_timer_hook(void *arg) {
  esp_timer_handle_t timer = (esp_timer_handle_t)arg;
  eos_proc_t *p = eos_proc_current();
  EOS_LOGW("proc: process '%s' (pid %d) leaked timer %p - stopping and deleting\n",
           p ? p->name : "unknown", p ? p->pid : -1, (void *)timer);
  if (esp_timer_is_active(timer)) {
    esp_timer_stop(timer);
  }
  __real_esp_timer_delete(timer);
}

esp_err_t __wrap_esp_timer_create(const esp_timer_create_args_t *create_args,
                                  esp_timer_handle_t *out_handle) {
  bool prev_k = eos_proc_enter_kernel();
  esp_err_t err = __real_esp_timer_create(create_args, out_handle);
  if (err == ESP_OK && out_handle && *out_handle) {
    eos_proc_t *cur = eos_proc_current();
    if (cur && cur->pid > 0) {
      eos_proc_on_exit(cleanup_timer_hook, (void *)*out_handle, "timer");
    }
  }
  eos_proc_leave_kernel(prev_k);
  return err;
}

esp_err_t __wrap_esp_timer_delete(esp_timer_handle_t timer) {
  bool prev_k = eos_proc_enter_kernel();
  if (timer) {
    eos_proc_cancel_exit_match(0, cleanup_timer_hook, (void *)timer);
  }
  esp_err_t err = __real_esp_timer_delete(timer);
  eos_proc_leave_kernel(prev_k);
  return err;
}
