#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
int main(int argc,char**argv){
  printf("hello %s argc=%d\n", argc>1?argv[1]:"-", argc);
  char b[100]; snprintf(b,sizeof b,"%d %5.2f %s %lx",42,3.14159,"str",0xabcdefUL); puts(b);
  printf("%zu %.3f\n", strlen("abcdefghijklmnop"), sqrt(2.0));
  void*p=malloc(100000); memset(p,7,100000); printf("%d\n",((char*)p)[99999]); free(p);
  return 3;
}
