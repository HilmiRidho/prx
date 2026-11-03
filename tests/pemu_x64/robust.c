/* Robust mutexes: a thread that dies holding one must make the next locker see EOWNERDEAD. That needs set_robust_list. */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <pthread.h>

static pthread_mutex_t mu;
static void*holder(void*a) { (void) a; pthread_mutex_lock(&mu); return NULL; }           /* exits with the mutex held */

int main(void) {
    pthread_mutexattr_t at; pthread_mutexattr_init(&at); pthread_mutexattr_setrobust(&at, PTHREAD_MUTEX_ROBUST);
    pthread_mutex_init(&mu, &at);
    pthread_t t; pthread_create(&t, NULL, holder, NULL); pthread_join(t, NULL);
    int r = pthread_mutex_lock(&mu);
    printf("lock after owner died: %s\n", r == EOWNERDEAD ? "EOWNERDEAD" : r == 0 ? "0 (not detected)" : "error");
    if (r == EOWNERDEAD) { pthread_mutex_consistent(&mu); pthread_mutex_unlock(&mu); }
    r = pthread_mutex_lock(&mu); printf("lock again: %s\n", r == 0 ? "ok" : "error"); pthread_mutex_unlock(&mu);
    return 0;
}
