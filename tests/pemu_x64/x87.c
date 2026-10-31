#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
int main(void){
  volatile long double a=1.2345678901234567L, b=-7.5L, c=1e100L, z=0;
  printf("%.15Lg %.15Lg %.15Lg %.15Lg\n", sinl(a), cosl(a), tanl(a), atan2l(a,b));
  printf("%.15Lg %.15Lg %.15Lg %.15Lg\n", expl(a), logl(c), powl(a,3.5L), sqrtl(a));
  printf("%.15Lg %.15Lg %.15Lg %.15Lg\n", floorl(b), ceill(a), fmodl(c,a), ldexpl(a,10));
  printf("%Lf %Le %Lg %Lg\n", a*b, c, a/z, z/z);
  long long ll=(long long)(a*1e17L); int ii=(int)b; unsigned long long u=(unsigned long long)(c/1e90L);
  printf("%lld %d %llu\n", ll, ii, u);
  long double s=0; for(int i=1;i<=1000;i++) s+=1.0L/i; printf("%.18Lf\n", s);
  printf("%Lg %Lg\n", strtold("3.14159265358979323846",0), (long double)atof("2.5"));
  double d=2.0/3.0; float f=(float)d; printf("%.17g %.9g\n", d, f);
  printf("%ld\n", lrintl(2.5L)); printf("%Lg %Lg\n", asinl(0.5L), acosl(0.5L));
  return 0;
}
