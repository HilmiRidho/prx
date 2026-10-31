#define _GNU_SOURCE
#include <stdio.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <math.h>
#include <fcntl.h>
static volatile int got, chld; static sigjmp_buf jb;
static void h(int s){ got=s; }
static void hc(int s){ chld++; }
static void hs(int s,siginfo_t*si,void*u){ got=100+s; siglongjmp(jb,1); }
int main(void){
  signal(SIGUSR1,h); raise(SIGUSR1); printf("usr1=%d\n",got);
  struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_handler=hc; sigaction(SIGCHLD,&sa,0);
  pid_t p=fork(); if(!p){ _exit(9);} int st; waitpid(p,&st,0); printf("child exit=%d chld=%d\n",WEXITSTATUS(st),chld>0);
  sa.sa_sigaction=hs; sa.sa_flags=SA_SIGINFO|SA_NODEFER; sigaction(SIGSEGV,&sa,0);
  if(!sigsetjmp(jb,1)){ volatile int*q=(int*)8; *q=1; } printf("segv recovered=%d\n",got);
  alarm(1); signal(SIGALRM,h); pause(); printf("alarm=%d\n",got);
  int fd[2]; pipe(fd); if(!fork()){ close(fd[0]); dprintf(fd[1],"from child %d",42); _exit(0);} close(fd[1]); char b[64]={0}; read(fd[0],b,63); wait(0); printf("pipe:%s\n",b);
  printf("%.10g %.10g %.10g %.10g\n", sin(1.0), exp(2.5), pow(2.0,0.5), log(10.0));
  printf("%g %e %f\n", 1e300*10, 123456.789, strtod("2.718281828",0));
  double s=0; for(int i=1;i<=100000;i++) s+=1.0/i; printf("H=%.12f\n",s);
  printf("%ld %lu\n", strtol("-9223372036854775807",0,10), strtoul("18446744073709551615",0,10));
  char*e[]={"sh","-c","echo exec-ok $0",0}; if(!fork()){ execv("/bin/sh",e); _exit(1);} wait(0);
  return 0;
}
