#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <pthread.h>
#include <unistd.h>
#include <ucontext.h>
/* sigaltstack / SA_ONSTACK, and thread-directed signals */
static char *alt; static const size_t altsz = 65536;
static volatile int on_alt, fl_in, uc_alt, got2, tready, tstop;
static sigjmp_buf jb;
static pthread_t target, hthread;
static void h(int s, siginfo_t *si, void *u) {
  char x; (void) si; (void) s;
  on_alt = (&x >= alt && &x < alt + altsz);
  stack_t cur; sigaltstack(0, &cur); fl_in = cur.ss_flags;
  uc_alt = ((ucontext_t *) u)->uc_stack.ss_sp == (void *) alt;
}
static void hsegv(int s, siginfo_t *si, void *u) { h(s, si, u); siglongjmp(jb, 1); }
static void hu2(int s) { (void) s; hthread = pthread_self(); got2 = 1; }
static void *tf(void *a) { (void) a; tready = 1; while (!tstop) sched_yield(); return 0; }
int main(void) {
  stack_t old, ss;
  alt = malloc(altsz);
  sigaltstack(0, &old); printf("initial flags=%d\n", old.ss_flags);                      /* SS_DISABLE = 2 */
  ss.ss_sp = alt; ss.ss_size = altsz; ss.ss_flags = 0;
  printf("set=%d\n", sigaltstack(&ss, 0));
  sigaltstack(0, &old); printf("now flags=%d sp_ok=%d size=%zu\n", old.ss_flags, old.ss_sp == alt, old.ss_size);
  stack_t small = { alt, 0, 100 }; printf("too small=%d\n", sigaltstack(&small, 0));
  struct sigaction sa; memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = h; sa.sa_flags = SA_SIGINFO | SA_ONSTACK; sigaction(SIGUSR1, &sa, 0);
  raise(SIGUSR1); printf("usr1 on_alt=%d flags_in=%d uc_stack=%d\n", on_alt, fl_in, uc_alt);
  sa.sa_flags = SA_SIGINFO; sigaction(SIGUSR1, &sa, 0);
  on_alt = -1; raise(SIGUSR1); printf("no SA_ONSTACK on_alt=%d flags_in=%d\n", on_alt, fl_in);
  sa.sa_sigaction = hsegv; sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER; sigaction(SIGSEGV, &sa, 0);
  on_alt = -1;
  if (!sigsetjmp(jb, 1)) { *(volatile int *) 8 = 1; }
  printf("segv on_alt=%d\n", on_alt);
  ss.ss_flags = SS_DISABLE; printf("disable=%d\n", sigaltstack(&ss, 0));
  sigaltstack(0, &old); printf("after disable flags=%d\n", old.ss_flags);
  /* a signal sent to one thread must run in that thread */
  signal(SIGUSR2, hu2);
  pthread_create(&target, 0, tf, 0);
  while (!tready) usleep(1000);
  pthread_kill(target, SIGUSR2);
  for (int i = 0; i < 5000 && !got2; i++) usleep(1000);
  printf("handler ran=%d in target thread=%d\n", got2, pthread_equal(hthread, target) != 0);
  tstop = 1; pthread_join(target, 0);
  return 0;
}
