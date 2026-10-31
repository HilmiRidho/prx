#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static long total, ready;
static void*w(void*a){ long id=(long)a, s=0; for(int i=0;i<200000;i++) s+=i%(id+3);
  pthread_mutex_lock(&mu); total+=s; ready++; pthread_cond_broadcast(&cv); pthread_mutex_unlock(&mu); return (void*)(id*10);}
int main(){ pthread_t t[4]; long sum=0;
  for(long i=0;i<4;i++) if(pthread_create(&t[i],0,w,(void*)i)){puts("create fail");return 1;}
  pthread_mutex_lock(&mu); while(ready<4) pthread_cond_wait(&cv,&mu); pthread_mutex_unlock(&mu);
  for(int i=0;i<4;i++){void*r; pthread_join(t[i],&r); sum+=(long)r;}
  printf("total=%ld joined=%ld\n",total,sum); return 0;}
