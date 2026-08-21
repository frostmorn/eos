#include "ecore/app.h"
#include <dirent.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

static volatile int counter = 0;
static volatile int ready = 0;

static pthread_key_t tls_key;

static void *hello_thread(void *arg) {
  printf("hello thread: arg=%d\n", (intptr_t)arg);

  printf("Testing lost dirs/fds\n");
  opendir("/");                    // should notify about not closed dir
  fopen("/bin/test_pthread", "r"); // should notify about not closed file
  
  return (void *)1234;
}

static void *counter_thread(void *arg) {
  int loops = (intptr_t)arg;

  for (int i = 0; i < loops; i++) {
    pthread_mutex_lock(&mutex);
    counter++;
    pthread_mutex_unlock(&mutex);
  }

  return NULL;
}

static void *cond_waiter(void *arg) {
  (void)arg;

  pthread_mutex_lock(&mutex);

  while (!ready)
    pthread_cond_wait(&cond, &mutex);

  pthread_mutex_unlock(&mutex);

  printf("condition signaled\n");
  return NULL;
}

static void *tls_thread(void *arg) {
  intptr_t value = (intptr_t)arg;

  pthread_setspecific(tls_key, (void *)value);

  printf("TLS=%d\n", (intptr_t)pthread_getspecific(tls_key));

  return NULL;
}

static void *sleep_thread(void *arg) {
  (void)arg;

  for (int i = 0; i < 5; i++) {
    printf("sleep %d\n", i);
    usleep(500000);
  }

  return NULL;
}

static void *detached_thread(void *arg) {
  (void)arg;
  printf("detached thread started\n");
  sleep(1);
  printf("detached thread finished\n");

  return NULL;
}

int test_pthread_main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  pthread_t t;
  void *retval;

  puts("== create/join ==");

  pthread_create(&t, NULL, hello_thread, (void *)42);
  pthread_join(t, &retval);

  printf("return=%d\n", (intptr_t)retval);

  puts("");

  puts("== mutex ==");

  counter = 0;

  pthread_t a, b;

  pthread_create(&a, NULL, counter_thread, (void *)100000);
  pthread_create(&b, NULL, counter_thread, (void *)100000);

  pthread_join(a, NULL);
  pthread_join(b, NULL);

  printf("counter=%d (expect 200000)\n", counter);

  puts("");

  puts("== condition ==");

  ready = 0;

  pthread_create(&t, NULL, cond_waiter, NULL);

  sleep(1);

  pthread_mutex_lock(&mutex);
  ready = 1;
  pthread_cond_signal(&cond);
  pthread_mutex_unlock(&mutex);

  pthread_join(t, NULL);

  puts("");

  puts("== TLS ==");

  pthread_key_create(&tls_key, NULL);

  pthread_create(&a, NULL, tls_thread, (void *)111);
  pthread_create(&b, NULL, tls_thread, (void *)222);

  pthread_join(a, NULL);
  pthread_join(b, NULL);

  pthread_key_delete(tls_key);

  puts("");

  puts("== detached ==");

  pthread_create(&t, NULL, detached_thread, NULL);
  pthread_detach(t);

  sleep(2);

  puts("");

  puts("== usleep ==");

  pthread_create(&t, NULL, sleep_thread, NULL);
  pthread_join(t, NULL);

  puts("");

  puts("All tests completed.");

  return 0;
}

EOS_NATIVE_APP_ATTR eos_native_app_manifest_t test_pthread = {
    EOS_NATIVE_APP_INIT, .filename = "test_pthread", .name = "test_pthread",
    .entry_point = test_pthread_main};