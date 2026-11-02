static long sc3(long n,long a,long b,long c){long r;__asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c):"rcx","r11","memory");return r;}
static unsigned long fact(unsigned long n){return n<2?1:n*fact(n-1);}
static void puts_(const char*s){long n=0;while(s[n])n++;sc3(1,1,(long)s,n);}
static void putn(long v){char b[24];int i=23;b[i]=0;int neg=v<0;if(neg)v=-v;do{b[--i]='0'+v%10;v/=10;}while(v);if(neg)b[--i]='-';puts_(b+i);puts_("\n");}
volatile double dv=3.5;
void _start(void){
  putn(fact(15));
  putn(-1234567LL*89);
  long s=0;for(int i=1;i<=1000;i++)s+=i*i; putn(s);
  putn((long)(dv*dv*100));
  unsigned long u=0xdeadbeefcafef00dUL; putn((long)(u%1000003)); putn((long)((__int128)u*u>>64)%1000);
  char buf[64]; for(int i=0;i<64;i++)buf[i]=i; long t=0;for(int i=0;i<64;i++)t+=buf[i]*3; putn(t);
  sc3(60,0,0,0);
}
