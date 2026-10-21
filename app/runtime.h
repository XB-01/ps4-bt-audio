#ifndef BTA_RUNTIME_H
#define BTA_RUNTIME_H
typedef unsigned long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
__attribute__((noinline)) static long sc3(long n,long a,long b,long c){long o;__asm__ volatile("syscall; jnc 1f; neg %%rax; 1:":"=a"(o),"+d"(c),"+D"(a),"+S"(b):"a"(n):"rcx","r8","r9","r10","r11","cc","memory");return o;}
__attribute__((noinline)) static long sc5(long n,long a,long b,long c,long d,long e){
 register long r10 __asm__("r10")=d;register long r8 __asm__("r8")=e;long o;
 __asm__ volatile("syscall; jnc 1f; neg %%rax; 1:":"=a"(o),"+d"(c),"+D"(a),"+S"(b),"+r"(r10),"+r"(r8):"a"(n):"rcx","r9","r11","cc","memory");return o;
}
void *memset(void*,int,u64);
void *memcpy(void*,const void*,u64);
static inline u64 length(const char*s){u64 n=0;while(s[n])n++;return n;}
static inline int equal(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static inline int starts(const char*s,const char*p){while(*p)if(*s++!=*p++)return 0;return 1;}
static inline char*find(char*s,const char*p){for(;*s;s++)if(starts(s,p))return s;return 0;}
static inline void closefd(long fd){if(fd>=0)sc3(6,fd,0,0);}
static inline int exists(const char*p){long f=sc3(5,(long)p,0,0);if(f<0)return 0;closefd(f);return 1;}
static inline long put(const char*p,const void*b,u64 n){long f=sc3(5,(long)p,0x601,0644);if(f<0)return f;long r=sc3(4,f,(long)b,n);closefd(f);return r==(long)n?0:-1;}
static inline long resolve(const char*n,void*p){return sc3(591,0x2001,(long)n,(long)p);}
struct sockaddr4{u8 len,family;u16 port;u32 addr;u8 zero[8];};
static inline int local_ip(const u8*p){return p[0]==127||p[0]==10||(p[0]==192&&p[1]==168)||(p[0]==172&&p[1]>=16&&p[1]<=31);}
static inline int nonblock(long fd){int v=1;return sc3(54,fd,0x8004667e,(long)&v);}
void notify(const char*message);
void* bta_worker(void*);
#define BTA_PORT 8097
#define BTA_STOP "/data/bta_audio/stop"
#define TV_ON "/data/bt_bta_tv_on"
/* Diagnostic: if present, skip the 3-slot HCI widening on connect so we can
 * test whether a plain audio link keeps the controller alive. */
#define NO_WIDEN "/data/bta_audio/no_widen"
#endif
