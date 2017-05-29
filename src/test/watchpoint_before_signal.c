/* -*- Mode: C; tab-width: 8; c-basic-offset: 2; indent-tabs-mode: nil; -*- */

#include "rrutil.h"

static int x;

static void* do_thread(__attribute__((unused)) void* p) {
  while (1) {
    sched_yield();
  }
  return NULL;
}

int main(void) {
  pthread_t thread;
  int i;
  int v = 0;

  pthread_create(&thread, NULL, do_thread, NULL);

  x = 1;

  /* Trigger async SCHED signal */
  for (i = 0; i < 1000; ++i) {
    v = v * 7 + 3;
  }

  x = v;

  /* Trigger synchronous signal */
  rdtsc();

  atomic_puts("EXIT-SUCCESS");
  return 0;
}
