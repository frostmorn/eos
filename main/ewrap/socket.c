#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <lwip/sockets.h>
#include "ecore/proc.h"
#include "ecore/log.h"
#include "emisc/fancymacro.h"

extern int __real_lwip_socket(int domain, int type, int protocol);
extern int __real_lwip_accept(int s, struct sockaddr *addr, socklen_t *addrlen);
extern int __real_lwip_close(int s);

static void cleanup_socket_hook(void *arg) {
  int sock = (int)(intptr_t)arg;
  eos_proc_t *p = eos_proc_current();
  EOS_LOGW("proc: process '%s' (pid %d) leaked socket %d - auto-closing\n",
           p ? p->name : "unknown", p ? p->pid : -1, sock);
  __real_lwip_close(sock);
}

int __wrap_lwip_socket(int domain, int type, int protocol) {
  bool prev_k = eos_proc_enter_kernel();
  extern esp_err_t esp_netif_init(void);
  esp_netif_init();
  int s = __real_lwip_socket(domain, type, protocol);
  if (s >= 0) {
    eos_proc_t *cur = eos_proc_current();
    if (cur && cur->pid > 0) {
      eos_proc_on_exit(cleanup_socket_hook, (void *)(intptr_t)s, "socket");
    }
  }
  eos_proc_leave_kernel(prev_k);
  return s;
}

int __wrap_lwip_accept(int s, struct sockaddr *addr, socklen_t *addrlen) {
  bool prev_k = eos_proc_enter_kernel();
  int client_s = __real_lwip_accept(s, addr, addrlen);
  if (client_s >= 0) {
    eos_proc_t *cur = eos_proc_current();
    if (cur && cur->pid > 0) {
      eos_proc_on_exit(cleanup_socket_hook, (void *)(intptr_t)client_s, "socket");
    }
  }
  eos_proc_leave_kernel(prev_k);
  return client_s;
}

int __wrap_lwip_close(int s) {
  bool prev_k = eos_proc_enter_kernel();
  if (s >= 0) {
    eos_proc_cancel_exit_match(0, cleanup_socket_hook, (void *)(intptr_t)s);
  }
  int ret = __real_lwip_close(s);
  eos_proc_leave_kernel(prev_k);
  return ret;
}
