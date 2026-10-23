#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/SystemService.h>
#include "ui.h"

static int pad=-1,pad_connected;
static uint64_t pad_retry;

/* All file I/O uses sceKernel* (never stdio): the app runs in a restricted
 * sandbox where a blocked libc syscall raises SIGSYS. app.log confirms these
 * kernel calls succeed. */
static void logline(const char*s){
 int fd=sceKernelOpen("/data/bta_audio/app.log",0x209,0644);
 if(fd>=0){sceKernelWrite(fd,s,strlen(s));sceKernelWrite(fd,"\n",1);sceKernelClose(fd);}
}
static unsigned controller(uint64_t now){
 OrbisPadData data;memset(&data,0,sizeof(data));
 if(pad>=0){int rc=scePadReadState(pad,&data);if(rc>=0&&data.connected){pad_connected=1;return data.buttons;}pad_connected=0;}
 if(now<pad_retry)return 0;pad_retry=now+2000000;
 if(pad>=0){scePadClose(pad);pad=-1;}
 OrbisUserServiceLoginUserIdList users;for(int i=0;i<4;i++)users.userId[i]=-1;
 int rc=sceUserServiceGetLoginUserIdList(&users);
 if(rc<0)sceUserServiceGetInitialUser(&users.userId[0]);
 for(int i=0;i<4;i++)if(users.userId[i]>=0){
  int h=scePadGetHandle(users.userId[i],ORBIS_PAD_PORT_TYPE_STANDARD,0);
  if(h<0)h=scePadOpen(users.userId[i],ORBIS_PAD_PORT_TYPE_STANDARD,0,0);
  if(h<0)continue;memset(&data,0,sizeof(data));
  if(scePadReadState(h,&data)>=0&&data.connected){pad=h;pad_connected=1;logline("Controller acquired");return data.buttons;}
  scePadClose(h);
 }
 return 0;
}

/* ---- HTTP client: the audio service owns Bluetooth; this app only displays and requests ---- */
static int local_socket(int port){
 int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return -1;
 struct timeval t={1,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof(t));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof(t));
 int one=1;setsockopt(fd,SOL_SOCKET,0x800,&one,sizeof(one));
 struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_len=sizeof(a);a.sin_family=AF_INET;a.sin_port=htons(port);a.sin_addr.s_addr=htonl(0x7f000001);
 if(connect(fd,(void*)&a,sizeof(a))<0){close(fd);return -1;}return fd;
}
static int write_all(int fd,const void*p,size_t n){const char*b=p;while(n){ssize_t k=send(fd,b,n,0);if(k<=0)return -1;b+=k;n-=k;}return 0;}
/* Returns the HTTP status code (e.g. 200, 409) or -1 on transport error. Copies
 * the response body (NUL-terminated) into out when provided. Every path closes fd. */
static int http(const char*method,const char*path,void*out,size_t cap){
 int fd=local_socket(8097);if(fd<0)return -1;
 char request[256];int len=snprintf(request,sizeof(request),"%s %s HTTP/1.1\r\nHost: 127.0.0.1:8097\r\nX-BTA-Control: 1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",method,path);
 if(len<0||len>=(int)sizeof(request)||write_all(fd,request,(size_t)len)){close(fd);return -1;}
 char response[16384];size_t used=0;
 while(used+1<sizeof(response)){ssize_t n=recv(fd,response+used,sizeof(response)-used-1,0);if(n==0)break;if(n<0){close(fd);return -1;}used+=(size_t)n;}
 close(fd);response[used]=0;
 if(used<12||strncmp(response,"HTTP/1.1 ",9))return -1;
 int code=atoi(response+9);
 char*b=strstr(response,"\r\n\r\n");
 if(out&&cap){size_t bytes=0;if(b){b+=4;bytes=used-(size_t)(b-response);if(bytes>cap-1)bytes=cap-1;memcpy(out,b,bytes);}((char*)out)[bytes]=0;}
 return code>0?code:-1;
}
/* Reads /api/native. Older services send a shorter reply without the link fields;
 * those are accepted (ext_ok=0) and the UI falls back to the basic flags. */
static int refresh(void){
 struct bt_native next;memset(&next,0,sizeof(next));
 int fd=local_socket(8097);if(fd<0){online=0;return -1;}
 const char*req="GET /api/native HTTP/1.1\r\nHost: 127.0.0.1:8097\r\nConnection: close\r\n\r\n";
 if(write_all(fd,req,strlen(req))){close(fd);online=0;return -1;}
 char resp[8192];size_t used=0;
 while(used<sizeof(resp)){ssize_t n=recv(fd,resp+used,sizeof(resp)-used,0);if(n<=0)break;used+=(size_t)n;}
 close(fd);
 if(used<12||strncmp(resp,"HTTP/1.1 200",12)){online=0;return -1;}
 char*body=0;for(size_t i=0;i+3<used;i++)if(resp[i]=='\r'&&resp[i+1]=='\n'&&resp[i+2]=='\r'&&resp[i+3]=='\n'){body=resp+i+4;break;}
 size_t avail=body?(size_t)(resp+used-body):0;
 if(!body||avail<offsetof(struct bt_native,link)){online=0;return -1;}
 memcpy(&next,body,avail<sizeof(next)?avail:sizeof(next));
 if(next.magic!=BT_NATIVE_MAGIC||next.version!=2||next.count<0||next.count>BT_DEVICE_LIMIT){online=0;return -1;}
 unsigned long selected=(focus>=0&&focus<state.count)?state.devices[focus].address:0;
 state=next;ext_ok=avail>=sizeof(next);state.message[127]=0;for(int i=0;i<state.count;i++)state.devices[i].name[95]=0;
 if(selected){for(int i=0;i<state.count;i++)if(state.devices[i].address==selected)focus=i;}
 if(focus<0||focus>=state.count)focus=0;
 online=1;return 0;
}
static void set_notice(const char*s){snprintf(notice,sizeof(notice),"%s",s);notice_until=g_now+6000000;}
/* The audio service normally starts at boot (GoldHEN AutoRun). If it is not running
 * when the app opens, send the bundled payload to GoldHEN's local BinLoader (port 9090)
 * once. The payload's launcher refuses to start twice, and we only try while port 8097
 * is not answering. The payload goes out on a single connection: never probe 9090. */
static void service_check(void){refresh();}
static int launch_attempted;
static void start_service(void){
 if(online||launch_attempted)return;launch_attempted=1;
 static char payload[131072];
 int f=sceKernelOpen("/app0/assets/BTA-Audio.bin",0,0);
 if(f<0){logline("start: bundled payload not found");return;}
 size_t used=0;
 while(used<sizeof(payload)){long n=sceKernelRead(f,payload+used,sizeof(payload)-used);if(n<=0)break;used+=(size_t)n;}
 sceKernelClose(f);
 if(used<1024||used>=sizeof(payload)){logline("start: bundled payload size invalid");return;}
 int fd=local_socket(9090);
 if(fd<0){logline("start: BinLoader (9090) not reachable; enable it in GoldHEN and jailbreak again");return;}
 int rc=write_all(fd,payload,used);close(fd);
 logline(rc?"start: send failed":"start: payload sent to BinLoader");
 if(!rc)set_notice("Starting audio service...");
}
static void action(const char*path){
 char body[160];int code=http("POST",path,body,sizeof(body));
 if(code==200)notice[0]=0;
 else if(code<0)set_notice("No response from the audio service.");
 else{for(int i=0;body[i];i++)if(body[i]=='\n'||body[i]=='\r')body[i]=0;set_notice(body[0]?body:"That didn't work.");}
 refresh();
}
static void volume_change(int amount){
 if(!online)return;int value=state.volume+amount;if(value<0)value=0;if(value>100)value=100;
 char path[40];snprintf(path,sizeof(path),"/api/volume/%d",value);action(path);
}
static void activate(void){
 if(!online){launch_attempted=0;service_check();start_service();return;}
 if(state.count<=0||focus<0||focus>=state.count)return;
 const struct bt_device*d=&state.devices[focus];int l=ui_link();
 if(d->address==state.selected&&l!=BT_LINK_IDLE){action("/api/stop");return;}
 char path[64];snprintf(path,sizeof(path),"/api/select/%012lX",d->address);action(path);
}

static unsigned char snaprow[WIDTH*3];
static void snapshot(void){
 int fd=sceKernelOpen("/data/bta_audio/app-frame.ppm",0x601,0644);
 if(fd<0){logline("snapshot: open failed");return;}
 char hdr[32];int hl=snprintf(hdr,sizeof(hdr),"P6\n%d %d\n255\n",WIDTH,HEIGHT);sceKernelWrite(fd,hdr,(size_t)hl);
 for(int y=0;y<HEIGHT;y++){
  for(int x=0;x<WIDTH;x++){uint32_t v=back[y*WIDTH+x];snaprow[x*3]=(unsigned char)(v>>16);snaprow[x*3+1]=(unsigned char)(v>>8);snaprow[x*3+2]=(unsigned char)v;}
  sceKernelWrite(fd,snaprow,sizeof(snaprow));
 }
 sceKernelClose(fd);logline("snapshot: written");
}
int main(void){
 sceKernelMkdir("/data/bta_audio",0777);sceKernelChmod("/data/bta_audio",0777);logline("Bluetooth Audio 2.7.5 native app starting");
 int video=sceVideoOutOpen(0xff,0,0,0);if(video<0){logline("VideoOutOpen failed");return 1;}
 off_t memory;size_t size=8*1024*1024;void*base=0;
 if(sceKernelAllocateDirectMemory(0,sceKernelGetDirectMemorySize(),size,0x200000,3,&memory)<0||sceKernelMapDirectMemory(&base,size,0x33,0,memory,0x200000)<0){logline("Video memory failed");return 2;}
 void*buffers[2]={base,(char*)base+WIDTH*HEIGHT*4};OrbisVideoOutBufferAttribute attr;
 sceVideoOutSetBufferAttribute(&attr,0x80000000,1,0,WIDTH,HEIGHT,WIDTH);
 if(sceVideoOutRegisterBuffers(video,0,buffers,2,&attr)<0){logline("Register buffers failed");return 3;}
 sceVideoOutSetFlipRate(video,1);
 OrbisUserServiceInitializeParams userparams={ORBIS_KERNEL_PRIO_FIFO_LOWEST};
 int user_rc=sceUserServiceInitialize(&userparams);int pad_rc=scePadInit();
 char diag[128];snprintf(diag,sizeof(diag),"User init=0x%x Pad init=0x%x",user_rc,pad_rc);logline(diag);
 sceSystemServiceHideSplashScreen();
 logline("Checking audio service");service_check();start_service();
 logline("Starting display loop");
 focus=0;for(int i=0;i<state.count;i++)if(state.devices[i].address==state.selected)focus=i;
 unsigned previous=0;int buffer=0,armed=0;long flip=0;uint64_t poll=0,repeat=0,first_draw=0,snap_cd=0;int pinmode=0,pinignore=0,digit=0,auto_snapped=0;char pin[5]="0000";
 for(;;){
  uint64_t now=sceKernelGetProcessTime();g_now=now;
  int l=ui_link();
  if(now>=poll){refresh();poll=now+(!online?2000000:(l==BT_LINK_STREAMING?300000:500000));}
  if(!state.pin_pending)pinignore=0;if(state.pin_pending&&!pinmode&&!pinignore){pinmode=1;digit=0;memcpy(pin,"0000",5);}
  unsigned buttons=controller(now);
  /* A button still held from launching the app is not a new press. */
  if(!armed&&pad_connected){previous=buttons;armed=1;}
  unsigned pressed=buttons&~previous;
  if(buttons==previous&&now>=repeat){pressed=buttons&(ORBIS_PAD_BUTTON_UP|ORBIS_PAD_BUTTON_DOWN|ORBIS_PAD_BUTTON_LEFT|ORBIS_PAD_BUTTON_RIGHT|ORBIS_PAD_BUTTON_L1|ORBIS_PAD_BUTTON_R1);repeat=now+150000;}
  else if(buttons!=previous)repeat=now+450000;
  previous=buttons;
  if(!armed)pressed=0;
  if(pinmode){
   if(pressed&ORBIS_PAD_BUTTON_LEFT)digit=(digit+3)%4;if(pressed&ORBIS_PAD_BUTTON_RIGHT)digit=(digit+1)%4;
   if(pressed&ORBIS_PAD_BUTTON_UP)pin[digit]=(char)('0'+(pin[digit]-'0'+1)%10);if(pressed&ORBIS_PAD_BUTTON_DOWN)pin[digit]=(char)('0'+(pin[digit]-'0'+9)%10);
   if(pressed&ORBIS_PAD_BUTTON_CIRCLE){pinmode=0;pinignore=1;}
   if(pressed&ORBIS_PAD_BUTTON_CROSS){char path[40];snprintf(path,sizeof(path),"/api/pin/%s",pin);logline("action: pin");action(path);pinmode=0;pinignore=1;}
  }else{
   if(pressed&ORBIS_PAD_BUTTON_CIRCLE)break;
   if(state.count>0){
    if(pressed&ORBIS_PAD_BUTTON_DOWN)focus=(focus+1)%state.count;
    if(pressed&ORBIS_PAD_BUTTON_UP)focus=(focus+state.count-1)%state.count;
   }
   if(pressed&ORBIS_PAD_BUTTON_CROSS){logline("action: connect/disconnect");activate();}
   if(pressed&ORBIS_PAD_BUTTON_TRIANGLE){if(online&&l!=BT_LINK_IDLE){logline("action: disconnect");action("/api/stop");}else{logline("action: scan");action("/api/scan");}}
   if(pressed&ORBIS_PAD_BUTTON_SQUARE){logline("action: tv");action(state.tv?"/api/tv-off":"/api/tv-on");}
   if(pressed&ORBIS_PAD_BUTTON_OPTIONS){logline("action: test sound");action("/api/test");if(!notice[0])testing_until=g_now+2200000;}
   if(pressed&(ORBIS_PAD_BUTTON_L1|ORBIS_PAD_BUTTON_LEFT))volume_change(-5);
   if(pressed&(ORBIS_PAD_BUTTON_R1|ORBIS_PAD_BUTTON_RIGHT))volume_change(5);
  }
  if(focus<0||focus>=state.count)focus=0;
  if(focus<top)top=focus;if(focus>=top+5)top=focus-4;if(top<0)top=0;
  {int target=(ext_ok&&ui_link()==BT_LINK_STREAMING)?state.meter:0;if(target<0)target=0;if(target>100)target=100;
   if(target>meter_s)meter_s+=(target-meter_s+1)/2;else meter_s-=(meter_s-target+3)/4;}
  if(notice[0]&&g_now>=notice_until)notice[0]=0;
  draw(pinmode,pin,digit);
  memcpy(buffers[buffer],back,sizeof(back));
  if(!first_draw)first_draw=now;
  if(!auto_snapped&&now-first_draw>2000000){snapshot();auto_snapped=1;}
  if((buttons&(ORBIS_PAD_BUTTON_L3|ORBIS_PAD_BUTTON_R3))==(ORBIS_PAD_BUTTON_L3|ORBIS_PAD_BUTTON_R3)&&now>=snap_cd){snapshot();snap_cd=now+2000000;}
  if(sceVideoOutSubmitFlip(video,buffer,1,flip)<0){sceKernelUsleep(33000);continue;}
  for(int tries=0;tries<1000;tries++){OrbisVideoOutFlipStatus status;if(!sceVideoOutGetFlipStatus(video,&status)&&status.flipArg==flip)break;sceKernelUsleep(1000);}
  buffer^=1;flip++;
 }
 logline("Native app closed; audio service remains running");if(pad>=0)scePadClose(pad);sceVideoOutClose(video);return 0;
}
