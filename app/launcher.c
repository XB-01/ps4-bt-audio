#include "runtime.h"
extern const char __image_start[],__image_end[];
void*memset(void*p,int value,u64 n){u8*d=p;while(n--)*d++=(u8)value;return p;}
void*memcpy(void*d,const void*s,u64 n){u8*a=d;const u8*b=s;while(n--)*a++=*b++;return d;}
void notify(const char*message){
 int(*send)(int,void*,u64,int)=0;if(resolve("sceKernelSendNotificationRequest",&send)||!send)return;
 u8 req[0xc30];memset(req,0,sizeof(req));*(int*)(req+0x10)=-1;
 u64 n=length(message);if(n>1023)n=1023;memcpy(req+0x2d,message,n);send(0,req,sizeof(req),0);
}
/* Run from our own mapping: GoldHEN is free to reuse its injection buffer. */
__attribute__((section(".text.launcher"))) int _start(void){
 int(*version)(void*)=0;struct{u64 size;char text[28];u32 version;}v={.size=40};
 if(resolve("sceKernelGetSystemSwVersion",&version)||!version||version(&v)||v.version!=0x13520001){notify("Bluetooth Audio requires PS4 firmware 13.520.001.");return 1;}
 long listener=sc3(97,2,1,0);if(listener<0)return 2;
 int one=1;sc5(105,listener,0xffff,4,(long)&one,4);
 struct sockaddr4 sa={.len=16,.family=2,.port=(BTA_PORT>>8)|((BTA_PORT&255)<<8)};
 if(sc3(104,listener,(long)&sa,16)<0||sc3(106,listener,4,0)<0||nonblock(listener)<0){closefd(listener);notify("Bluetooth Audio is already running, or port 8097 is busy. Open http://127.0.0.1:8097/");return 3;}
 void*(*map)(void*,u64,int,int,int,u64)=0;int(*unmap)(void*,u64)=0;
 int(*create)(void**,void*,void*(*)(void*),void*,const char*)=0;int(*detach)(void*)=0;
 if(resolve("mmap",&map)||resolve("munmap",&unmap)||resolve("scePthreadCreate",&create)||resolve("scePthreadDetach",&detach)||!map||!unmap||!create||!detach){closefd(listener);return 4;}
 /* Force runtime RIP-relative addresses. Linker-defined symbols may otherwise
  * be folded into absolute immediates by the static linker. */
 const u8*source;const u8*end;const u8*entry;
 __asm__ volatile("lea __image_start(%%rip),%0; lea __image_end(%%rip),%1; lea bta_worker(%%rip),%2":"=r"(source),"=r"(end),"=r"(entry));
 u64 bytes=end-source;if(bytes<1024||bytes>1024*1024){closefd(listener);return 6;}
 u64 allocation=(bytes+0x3fff)&~0x3fffUL;
 u8*copy=map(0,allocation,7,0x1002,-1,0);if(!copy||copy==(void*)-1){closefd(listener);return 5;}
 memcpy(copy,source,bytes);void*thread=0;
 void*(*worker)(void*)=(void*)(copy+(entry-source));
 int rc=create(&thread,0,worker,(void*)listener,"Bluetooth Audio");
 if(rc){unmap(copy,allocation);closefd(listener);return rc;}detach(thread);return 0;
}
