/* 13.52-only locked snapshot of the driver's existing Classic device cache,
 * and the same normal HCI packet-type request used by the proven BTA bridge.
 * Device selection is runtime data; the audio service check remains enforced. */
#include "protocol.h"
typedef unsigned long u64;
__attribute__((noinline)) static long sc3(long n,long a,long b,long c){long o;__asm__ volatile("syscall; jnc 1f; neg %%rax; 1:":"=a"(o),"+d"(c),"+D"(a),"+S"(b):"a"(n):"rcx","r8","r9","r10","r11","cc","memory");return o;}
struct input{u64 address;int mode;struct bt_devices*out;};
struct args{void*fn;struct input*input;};
static int callback(void*td,struct args*a){
 (void)td;unsigned lo,hi;__asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(0xc0000082));u64 base=(((u64)hi<<32)|lo)-0x1c0;
 if((base>>48)!=0xffff||(base&0x3fff))return 22;
 if(*(u64*)(base+0x5b7570)!=0x56415741e5894855UL||*(u64*)(base+0x5ae3b0)!=0x56415741e5894855UL)return 22;
 /* Input is the calling worker's private stack, as in existing syscall-11 helpers. */
 u64 address=a->input->address;int mode=a->input->mode;struct bt_devices*destination=a->input->out;
 if(mode!=0&&mode!=1)return 22;if(mode==1&&(!address||address>>48))return 22;
 struct bt_devices out;for(unsigned i=0;i<sizeof(out);i++)((volatile unsigned char*)&out)[i]=0;
 int(*lock)(void)=(void*)(base+0x5a73f0);int(*unlock)(int)=(void*)(base+0x5a7470);
 int rc=lock();if(rc)return rc;unsigned char*ctx=*(void**)(base+0x2612550);
 if(!ctx||((u64)ctx>>48)!=0xffff){unlock(0);return 6;}
 if(mode==0){
  /* Bounds/stride recovered from cache lookup at +5b7570 and name lookup +5b89c0. */
  for(unsigned off=0x18da8;off<0x20ea8;off+=0x408){
   unsigned char*d=ctx+off;u64 mac=*(u64*)d;if(!mac||mac>>48)continue;
   struct bt_device*v=&out.items[out.count++];v->address=mac;v->cod=*(unsigned*)(d+8);v->services=*(unsigned*)(d+0x20);v->flags=*(unsigned*)(d+0x24);
   for(unsigned i=0;i<sizeof(v->name)-1;i++){unsigned char c=d[0x80+i];if(!c)break;v->name[i]=(c>=32&&c<127)?c:'?';}
   if(out.count==BT_DEVICE_LIMIT)break;
  }
 }else{
  rc=2;
  for(unsigned off=0x42a8;off<0xd960;off+=0x1588){unsigned char*c=ctx+off;if(*(u64*)(c+0x30)!=address)continue;
   unsigned char*d=*(void**)(c+0x638);if(!d||((u64)d>>48)!=0xffff||!(*(unsigned*)(d+0x20)&8)){rc=93;break;}
   if(*(unsigned*)(c+0x1560)>=3){rc=0;break;}
   int(*command)(void*,unsigned,const char*,...)=(void*)(base+0x5ae3b0);
   rc=command(ctx,0x40f,(const char*)(base+0xad7562),(unsigned)*(unsigned short*)(c+0x48),0x3c18);
   if(rc>=0){*(unsigned*)(c+0x1564)=0x3c18;rc=0;}break;
  }
 }
 unlock(0);
 if(mode==0){int(*copyout)(const void*,void*,u64)=(void*)(base+0x2bd6a0);return copyout(&out,destination,sizeof(out));}
 return rc;
}
static int invoke(struct input*in){
 struct{u64 size;char text[28];unsigned version;}v={.size=40};int(*getver)(void*)=0;
 if(sc3(591,0x2001,(long)"sceKernelGetSystemSwVersion",(long)&getver)||!getver||getver(&v)||v.version!=0x13520001)return 22;
 return (int)sc3(11,(long)callback,(long)in,0);
}
int bt_list(struct bt_devices*out){struct input in={.mode=0,.out=out};return invoke(&in);}
int bt_prepare(u64 address){struct input in={.address=address,.mode=1};return invoke(&in);}
