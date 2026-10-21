#include "runtime.h"
#include "web_page.h"
#include "protocol.h"
int bta_apply_patch(void);

/* Keep HDMI unmuted for this long after streaming starts so the system audio
 * mix that feeds /shm_acap.full actually activates, THEN apply the user's
 * mute preference. Starting muted leaves the capture ring empty (silence). */
#define PRIME_US 2000000
/* One cycle of a sine at ~8000/32767 (~24%) full scale for the safe test tone. */
static const short SINE32[32]={0,1561,3061,4445,5657,6652,7391,7846,8000,7846,7391,6652,5657,4445,3061,1561,0,-1561,-3061,-4445,-5657,-6652,-7391,-7846,-8000,-7846,-7391,-6652,-5657,-4445,-3061,-1561};

struct audio_req{u64 address;void*samples;u64 length;u32 mode,pad;};
struct client{long fd;u64 deadline;u32 used,sent,total;char input[2048];char output[20000];};
struct app{
 int(*sleep)(unsigned);u64(*clock)(void);int(*shm)(const char*,int,int);
 void*(*map)(void*,u64,int,int,int,u64);int(*unmap)(void*,u64);
 long listener,bt,hdmi,shmfd;u8*ring;u32 readpos;int wanted,streaming,tv,muted,quit,phase;
 long patch_rc,packet_rc,connect_rc,last_rc,hdmi_rc,btstate;
 u64 next_try,deadline,scan_at,heartbeat,poll_at,net_at,packets,errors,resyncs;
 unsigned level;int volume;char message[128];
 u64 stream_at,test_until,test_next,silent_since;u32 test_phase,reprimes;
 /* Truthful link state (see link_state). dropped: the link was lost while
  * streaming. failed: the last attempt failed. announced: a "disconnected"
  * notice was shown. session: this connection has already been announced. */
 int dropped,failed,announced,session,meter;u32 attempts;u64 ok_at,meter_at,notify_at[3];unsigned meter_peak;
 struct bt_device target;struct bt_devices devices;int scanning,pin_pending;u64 scan_end,list_at;
 struct client clients[4];
};
static void message(struct app*a,const char*s){memset(a->message,0,sizeof(a->message));u64 n=length(s);if(n>127)n=127;memcpy(a->message,s,n);}
/* pre + device name (clipped to 40 bytes) + suf, always NUL-terminated within cap. */
static u64 compose(char*out,u64 cap,const char*pre,const char*name,const char*suf){
 u64 n=0;
 while(*pre&&n+1<cap)out[n++]=*pre++;
 for(u64 k=0;name[k]&&k<40&&n+1<cap;k++)out[n++]=name[k];
 while(*suf&&n+1<cap)out[n++]=*suf++;
 out[n]=0;return n;
}
static const char*target_name(struct app*a){return a->target.name[0]?a->target.name:"your headphones";}
static void say(struct app*a,const char*pre,const char*suf){char b[128];compose(b,sizeof(b),pre,target_name(a),suf);message(a,b);}
/* On-screen notices only on real transitions; a flapping link must not spam them. */
static int may_announce(struct app*a,int kind){u64 now=a->clock();if(a->notify_at[kind]&&now-a->notify_at[kind]<10000000)return 0;a->notify_at[kind]=now;return 1;}
static void mute(struct app*a,int on){
 if(a->hdmi<0||a->muted==on)return;int v=on;a->hdmi_rc=sc3(54,a->hdmi,0xc0048d05,(long)&v);
 if(!a->hdmi_rc)a->muted=on;
 long result[4]={a->hdmi,a->hdmi_rc,on,v};put("/data/bt_hdmi_mute_status.bin",result,sizeof(result));
}
static void stop_audio(struct app*a){
 if(a->phase==4&&a->bt>=0){sc3(54,a->bt,0x20046205,0);a->scanning=0;}
 if(a->streaming){short silence[384];memset(silence,0,sizeof(silence));struct audio_req q={a->target.address,silence,sizeof(silence),0,0};sc3(54,a->bt,0x80206216,(long)&q);}
 a->streaming=0;a->phase=0;a->level=0;a->meter=0;a->meter_peak=0;a->pin_pending=0;a->test_until=0;mute(a,0);
 if(a->ring){a->unmap(a->ring,0x10000);a->ring=0;}closefd(a->shmfd);a->shmfd=-1;
}
/* A failed attempt: stop, explain, and try again after delay_us. */
static void retry_after(struct app*a,const char*s,u64 delay_us){stop_audio(a);message(a,s);a->failed=1;a->next_try=a->clock()+delay_us;}
static void retry(struct app*a,const char*s){retry_after(a,s,10000000);}
static void retry_named(struct app*a,const char*pre,const char*suf,u64 delay_us){char m[128];compose(m,sizeof(m),pre,target_name(a),suf);retry_after(a,m,delay_us);}
/* The PS4 audio stream was rebuilt (e.g. a game started). The headphone link is
 * fine, so this is a quiet reconnect of the audio path, not a failure or a drop. */
static void restart_audio(struct app*a,const char*s){stop_audio(a);message(a,s);a->next_try=a->clock()+2000000;}
/* The console's capture mix can go silent while the link is healthy (for example after a
 * game change, or when the TV mute outlives the mix). Redo the useful half of a reconnect
 * without touching Bluetooth: re-open the capture ring and hold the TV unmuted for PRIME_US
 * so the mix restarts. The source is silent at this point, so nothing is audible. */
static void reprime(struct app*a,u64 now){
 long fd=a->shm("/shm_acap.full",0,0);
 if(fd>=0){
  void*p=a->map(0,0x10000,1,1,fd,0);
  if(p&&p!=(void*)-1){
   u32 w=(u32)(*(volatile u64*)((u8*)p+8)>>32);
   if(*(volatile u32*)p==0x10000&&w>=16&&w<0x10000&&!(w&7)){
    if(a->ring)a->unmap(a->ring,0x10000);
    closefd(a->shmfd);a->ring=p;a->shmfd=fd;a->readpos=w;fd=-1;
   }else a->unmap(p,0x10000);
  }
  if(fd>=0)closefd(fd);
 }
 a->stream_at=now;mute(a,0);
}
/* The headphones went away mid-stream. Detected only by consecutive audio-send
 * failures (never by polling the driver while streaming: that starved the
 * controller). stop_audio restores TV sound. */
static void link_lost(struct app*a){
 stop_audio(a);a->dropped=1;a->failed=0;a->next_try=a->clock()+2000000;
 say(a,""," disconnected. TV sound is back. Reconnecting...");
 if(may_announce(a,1)){notify("Bluetooth headphones disconnected. TV sound is back on. Reconnecting...");a->announced=1;}
}
static struct bt_device*device(struct app*a,u64 address){for(unsigned i=0;i<a->devices.count;i++)if(a->devices.items[i].address==address)return &a->devices.items[i];return 0;}
static int ensure_bt(struct app*a){if(a->bt>=0)return 0;a->bt=sc3(5,(long)"/dev/bt",6,0);if(a->bt<0)return -1;u64 mask=0xffffff;sc3(4,a->bt,(long)&mask,8);return 0;}
static void refresh_devices(struct app*a){struct bt_devices d;if(!bt_list(&d)&&d.count<=BT_DEVICE_LIMIT)a->devices=d;struct bt_device*t=device(a,a->target.address);if(t)a->target=*t;}
static void end_scan(struct app*a){if(a->scanning&&a->bt>=0)sc3(54,a->bt,0x20046205,0);a->scanning=0;}
static int scan(struct app*a){if(ensure_bt(a))return -1;long rc=sc3(54,a->bt,0x20046204,0);if(rc<0)return -1;a->scanning=1;a->scan_end=a->clock()+16000000;a->list_at=0;return 0;}
struct saved_device{u64 magic;struct bt_device device;};
static int save_target(struct app*a,const struct bt_device*d){
 struct saved_device s={.magic=0x3254454752415442UL,.device=*d};
 if(put("/data/bta_audio/device.tmp",&s,sizeof(s)))return -1;
 if(sc3(128,(long)"/data/bta_audio/device.tmp",(long)"/data/bta_audio/device.bin",0)<0)return -1;
 a->target=*d;return 0;
}
static void load_target(struct app*a){
 a->target.address=0;a->target.services=0;
 long fd=sc3(5,(long)"/data/bta_audio/device.bin",0,0);if(fd<0)return;
 struct saved_device s;long n=sc3(3,fd,(long)&s,sizeof(s));closefd(fd);
 if(n==sizeof(s)&&s.magic==0x3254454752415442UL&&s.device.address&&!(s.device.address>>48)){s.device.name[95]=0;a->target=s.device;}
}
static int save_volume(struct app*a,int volume){
 if(volume<0||volume>100)return -1;unsigned value=(unsigned)volume;
 if(put("/data/bta_audio/volume.tmp",&value,sizeof(value)))return -1;
 if(sc3(128,(long)"/data/bta_audio/volume.tmp",(long)"/data/bta_audio/volume.bin",0)<0)return -1;
 a->volume=volume;return 0;
}
static void load_volume(struct app*a){
 a->volume=40;unsigned value=101;long fd=sc3(5,(long)"/data/bta_audio/volume.bin",0,0);
 if(fd<0)return;long n=sc3(3,fd,(long)&value,sizeof(value));closefd(fd);
 if(n==sizeof(value)&&value<=100)a->volume=(int)value;
}
static void begin_connect(struct app*a){
 a->patch_rc=bta_apply_patch();if(a->patch_rc){retry(a,"Bluetooth setup failed. Check the diagnostic status.");return;}
 if(ensure_bt(a)){retry(a,"Bluetooth is not ready. Retrying shortly.");return;}
 refresh_devices(a);struct bt_device*t=device(a,a->target.address);
 if(!t){if(scan(a)){retry(a,"Bluetooth scan could not start. Retrying shortly.");return;}a->phase=4;a->deadline=a->clock()+16000000;say(a,"Looking for ","... Put it in pairing mode if needed.");return;}
 if(!bt_is_audio(t)){a->wanted=0;retry(a,"This device does not advertise headphone audio support.");return;}
 end_scan(a);u64 address=a->target.address;a->btstate=sc3(54,a->bt,0x80086203,(long)&address);
 a->phase=1;a->deadline=a->clock()+30000000;say(a,"Connecting to ","...");
 if(a->btstate<5||a->btstate>6){sc3(54,a->bt,0x20046205,0);a->connect_rc=sc3(54,a->bt,0x8008620a,(long)&address);
  /* An immediate driver error (e.g. 0x802F0204) means this device will not
   * connect right now; fail fast instead of waiting out the 30 s window. */
  if(a->connect_rc<0){retry_named(a,"Couldn't connect to ",". Make sure it's on, nearby and not connected to another device.",8000000);return;}
 }else a->connect_rc=0;
}
static void start_audio(struct app*a){
 a->shmfd=a->shm("/shm_acap.full",0,0);if(a->shmfd<0){retry(a,"Waiting for the PS4 audio service.");return;}
 void*p=a->map(0,0x10000,1,1,a->shmfd,0);
 if(!p||p==(void*)-1){retry(a,"Unable to open the game audio stream.");return;}a->ring=p;
 a->readpos=*(volatile u32*)(a->ring+12);
 if(*(volatile u32*)a->ring!=0x10000||a->readpos<16||a->readpos>=0x10000||(a->readpos&7)){retry(a,"The PS4 audio stream has an unexpected format.");return;}
 a->streaming=1;a->phase=3;a->errors=0;a->stream_at=a->clock();a->test_until=0;a->test_next=0;a->test_phase=0;
 int was_dropped=a->dropped,told=a->announced,fresh=!a->session;
 a->attempts=0;a->failed=0;a->dropped=0;a->announced=0;a->session=1;a->ok_at=a->stream_at;a->meter=0;a->meter_peak=0;
 say(a,"Connected to ",a->tv?". TV sound is on.":". TV sound is muted.");
 if(was_dropped){if(told&&may_announce(a,2))notify("Bluetooth headphones reconnected.");}
 else if(fresh&&may_announce(a,0)){char m[200];compose(m,sizeof(m),"Bluetooth headphones connected: ",target_name(a),a->tv?". TV sound stays on.":". TV sound muted.");notify(m);}
}
static void bluetooth_tick(struct app*a,u64 now){
 /* While audio is streaming, do NOT walk the driver cache: refresh_devices()
  * takes the kernel's Bluetooth link lock, and polling it mid-stream starves
  * the wireless controller's HID link. The proven BTA bridge never did this.
  * Device enumeration resumes as soon as streaming stops. */
 if(!a->streaming&&now>=a->list_at){refresh_devices(a);a->list_at=now+(a->scanning?500000:3000000);}
 if(a->scanning&&now>=a->scan_end)end_scan(a);
 if(a->bt>=0){
  struct{u8 type,subtype;u16 reserved;u32 data;u64 address;}ev[16];long n=sc3(3,a->bt,(long)ev,sizeof(ev));
  if(n>0&&n<=(long)sizeof(ev))for(unsigned i=0;i<(unsigned)n/16;i++)if(ev[i].address==a->target.address&&a->wanted){
   if(ev[i].type==4){u64 q[2]={a->target.address,1};sc3(54,a->bt,0x80106208,(long)q);}
   if(ev[i].type==3){a->pin_pending=1;a->deadline=now+60000000;message(a,"These headphones require a pairing PIN.");}
  }
 }
 if(!a->wanted)return;
 if(a->phase==0){if(now>=a->next_try){a->attempts++;a->failed=0;begin_connect(a);}return;}
 if(a->phase==4){
  if(device(a,a->target.address)){end_scan(a);a->phase=0;begin_connect(a);return;}
  if(now>=a->deadline)retry_named(a,"Couldn't find ",". Put it in pairing mode.",8000000);return;
 }
 if(a->phase==1){
  u64 address=a->target.address;a->btstate=sc3(54,a->bt,0x80086203,(long)&address);
  if(a->btstate>=5&&a->btstate<=6){a->pin_pending=0;
   if(exists(NO_WIDEN)){a->packet_rc=0;/* diagnostic: leave controller link params untouched */}
   else{a->packet_rc=bt_prepare(address);if(a->packet_rc){retry(a,"No supported Bluetooth audio link. Retrying shortly.");return;}}
   a->phase=2;a->deadline=now+400000;}
  else if(now>=a->deadline)retry_named(a,""," didn't respond. Is it on and nearby?",8000000);
 }else if(a->phase==2&&now>=a->deadline)start_audio(a);
}
static void audio_tick(struct app*a,u64 now){
 if(!a->streaming)return;
 short pcm[384];unsigned peak=0;
 if(now<a->test_until){
  /* Self-paced test tone (works even with no game audio): one 192-frame packet
   * every ~4ms. Independent of the capture ring; moderate, volume-independent. */
  if(now<a->test_next)return;
  a->test_next=now+4000;
  for(unsigned i=0;i<192;i++){short s=SINE32[(a->test_phase>>8)&31];a->test_phase+=75;pcm[2*i]=s;pcm[2*i+1]=s;unsigned m=s<0?-s:s;if(m>a->level)a->level=m;if(m>peak)peak=m;}
 }else{
  u32 writepos=(u32)(*(volatile u64*)(a->ring+8)>>32);
  if(writepos<16||writepos>=0x10000||(writepos&7)){restart_audio(a,"Audio stream changed. Reconnecting...");return;}
  unsigned available=writepos>=a->readpos?writepos-a->readpos:0xfff0-a->readpos+writepos;
  if(available>0xc000){a->readpos=writepos;a->resyncs++;return;}if(available<1536)return;
  u32 cursor=a->readpos;
  for(unsigned i=0;i<384;i++){float f=*(volatile float*)(a->ring+cursor);cursor+=4;if(cursor==0x10000)cursor=16;if(f!=f)f=0;if(f>1)f=1;if(f<-1)f=-1;int v=(int)(f*(32767.0f*(float)a->volume/100.0f));pcm[i]=(short)v;unsigned m=v<0?-v:v;if(m>a->level)a->level=m;if(m>peak)peak=m;}
  a->readpos=cursor;
  if(peak){a->silent_since=0;a->reprimes=0;}
  else{
   if(!a->silent_since)a->silent_since=now;
   u64 quiet=now-a->silent_since;
   if((a->reprimes==0&&quiet>=3000000)||(a->reprimes==1&&quiet>=10000000)||(a->reprimes==2&&quiet>=30000000)){a->reprimes++;reprime(a,now);}
  }
 }
 struct audio_req req={a->target.address,pcm,sizeof(pcm),3,0};a->last_rc=sc3(54,a->bt,0x80206216,(long)&req);
 if(a->last_rc<0){if(++a->errors>20)link_lost(a);}
 else{a->errors=0;a->packets++;a->ok_at=now;if(peak>a->meter_peak)a->meter_peak=peak;
  /* Prime the mix: hold HDMI unmuted for PRIME_US after stream start, then mute. */
  int want=!a->tv;if(want&&now-a->stream_at<PRIME_US)want=0;if(a->muted!=want)mute(a,want);}
}
/* Bounded JSON writer. Values are numbers and fixed internal strings only. */
struct writer{char*b;unsigned used,cap;};
static void append(struct writer*w,const char*s){while(*s&&w->used+1<w->cap)w->b[w->used++]=*s++;w->b[w->used]=0;}
static void number(struct writer*w,long n){char b[24];unsigned i=0;u64 v;if(n<0){append(w,"-");v=0-(u64)n;}else v=n;do{b[i++]=(char)('0'+v%10);v/=10;}while(v);while(i){char s[2]={b[--i],0};append(w,s);}}
static void field(struct writer*w,const char*key,long value){append(w,",\"");append(w,key);append(w,"\":");number(w,value);}
static void quoted(struct writer*w,const char*s){append(w,"\"");for(unsigned i=0;s[i]&&i<127;i++){char c=s[i];if(c=='"'||c=='\\')append(w,"\\");char b[2]={(c>=32&&c<127)?c:'?',0};append(w,b);}append(w,"\"");}
static void mac_text(u64 address,char out[13]){const char digits[]="0123456789ABCDEF";for(int i=0;i<12;i++)out[i]=digits[(address>>(44-4*i))&15];out[12]=0;}
static u64 parse_mac(const char*p){u64 v=0;for(int i=0;i<12;i++){char c=p[i];unsigned n;if(c>='0'&&c<='9')n=c-'0';else if(c>='A'&&c<='F')n=c-'A'+10;else if(c>='a'&&c<='f')n=c-'a'+10;else return 0;v=(v<<4)|n;}if(!starts(p+12," HTTP/1."))return 0;return v;}
/* The one place the connection's real state is decided. A UI must show this,
 * never infer it from "selected" or "wanted". */
static int link_state(struct app*a){
 if(a->streaming)return BT_LINK_STREAMING;
 if(!a->wanted)return BT_LINK_IDLE;
 if(a->phase==4)return BT_LINK_SEARCHING;
 if(a->dropped)return BT_LINK_LOST;
 if(a->failed)return BT_LINK_FAILED;
 return BT_LINK_CONNECTING;
}
static int retry_in_ms(struct app*a,u64 now){return(a->wanted&&!a->streaming&&!a->phase&&a->next_try>now)?(int)((a->next_try-now)/1000):0;}
static int quiet_for_ms(struct app*a,u64 now){if(!a->streaming||now<a->ok_at)return 0;u64 ms=(now-a->ok_at)/1000;return ms>2000000000UL?2000000000:(int)ms;}
static unsigned status(struct app*a,char*out,unsigned cap){
 u64 now=a->clock();
 struct writer w={out,0,cap};append(&w,"{\"version\":\"2.6\",\"message\":");quoted(&w,a->message);
 field(&w,"volume",a->volume);field(&w,"wanted",a->wanted);field(&w,"streaming",a->streaming);field(&w,"tv_enabled",a->tv);field(&w,"hdmi_muted",a->muted==1);field(&w,"scanning",a->scanning);field(&w,"pin_pending",a->pin_pending);field(&w,"test",now<a->test_until);field(&w,"link",link_state(a));field(&w,"attempts",a->attempts);field(&w,"retry_ms",retry_in_ms(a,now));field(&w,"meter",a->meter);field(&w,"quiet_ms",quiet_for_ms(a,now));field(&w,"level",a->level);field(&w,"packets",a->packets);field(&w,"errors",a->errors);field(&w,"resyncs",a->resyncs);field(&w,"patch_rc",a->patch_rc);field(&w,"packet_rc",a->packet_rc);field(&w,"connect_rc",a->connect_rc);field(&w,"audio_rc",a->last_rc);field(&w,"hdmi_rc",a->hdmi_rc);field(&w,"bt_state",a->btstate);field(&w,"no_widen",exists(NO_WIDEN));field(&w,"uptime_us",a->clock());
 char mac[13];mac_text(a->target.address,mac);append(&w,",\"selected\":");quoted(&w,mac);append(&w,",\"name\":");quoted(&w,a->target.name);
 append(&w,",\"devices\":[");int first=1;
 for(unsigned i=0;i<a->devices.count;i++){struct bt_device*d=&a->devices.items[i];if(!bt_is_audio(d)&&d->address!=a->target.address)continue;
  if(!first)append(&w,",");first=0;mac_text(d->address,mac);append(&w,"{\"address\":");quoted(&w,mac);append(&w,",\"name\":");quoted(&w,d->name);field(&w,"audio",!!(d->services&8));append(&w,"}");}
 append(&w,"]}");return w.used;
}
static unsigned native_status(struct app*a,char*out){struct bt_native*n=(void*)out;memset(n,0,sizeof(*n));n->magic=BT_NATIVE_MAGIC;n->version=2;n->volume=a->volume;n->streaming=a->streaming;n->wanted=a->wanted;n->tv=a->tv;n->muted=a->muted==1;n->scanning=a->scanning;n->pin_pending=a->pin_pending;n->selected=a->target.address;memcpy(n->message,a->message,sizeof(n->message));for(unsigned i=0;i<a->devices.count;i++)if(bt_is_audio(&a->devices.items[i])||a->devices.items[i].address==a->target.address)n->devices[n->count++]=a->devices.items[i];
 u64 now=a->clock();n->link=link_state(a);n->attempts=(int)a->attempts;n->retry_ms=retry_in_ms(a,now);n->meter=a->meter;n->quiet_ms=quiet_for_ms(a,now);n->test=now<a->test_until;n->connect_rc=(int)a->connect_rc;n->audio_rc=(int)a->last_rc;
 return sizeof(*n);}
static void response(struct client*c,const char*code,const char*type,const char*body,unsigned n){
 struct writer w={c->output,0,sizeof(c->output)};append(&w,"HTTP/1.1 ");append(&w,code);append(&w,"\r\nContent-Type: ");append(&w,type);append(&w,"\r\nContent-Length: ");number(&w,n);
 append(&w,"\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; frame-ancestors 'none'\r\n\r\n");
 if(w.used+n>sizeof(c->output)){c->total=0;return;}memcpy(c->output+w.used,body,n);c->total=w.used+n;
}
static char*header(char*request,const char*name){
 char*p=find(request,"\r\n");if(!p)return 0;p+=2;
 while(*p&&*p!='\r'){
  char*q=p;const char*k=name;while(*k){char c=*q;if(c>='A'&&c<='Z')c+=32;if(c!=*k)break;q++;k++;}
  if(!*k&&*q==':'){q++;while(*q==' '||*q=='\t')q++;return q;}
  p=find(p,"\r\n");if(!p)return 0;p+=2;
 }
 return 0;
}
static int host_valid(char*request){
 char*p=header(request,"host");if(!p)return 0;
 u8 ip[4];for(int i=0;i<4;i++){unsigned n=0,digits=0;while(*p>='0'&&*p<='9'){n=n*10+*p++-'0';if(++digits>3||n>255)return 0;}if(!digits)return 0;ip[i]=n;if(i<3&&*p++!='.')return 0;}
 return local_ip(ip)&&starts(p,":8097\r\n");
}
static void dispatch(struct app*a,struct client*c){
 char body[12000];
 if(!host_valid(c->input)){response(c,"403 Forbidden","text/plain","Use the PS4 IP address.",23);return;}
 if(starts(c->input,"GET / HTTP/1.")){response(c,"200 OK","text/html; charset=utf-8",web_page,sizeof(web_page)-1);return;}
 if(starts(c->input,"GET /api/native HTTP/1.")){unsigned n=native_status(a,body);response(c,"200 OK","application/octet-stream",body,n);return;}
 if(starts(c->input,"GET /api/status HTTP/1.")){unsigned n=status(a,body,sizeof(body));response(c,"200 OK","application/json",body,n);return;}
 char*control=header(c->input,"x-bta-control");
 if(!starts(c->input,"POST ")||!control||!starts(control,"1\r\n")){response(c,"403 Forbidden","text/plain","Control header required.",24);return;}
 if(starts(c->input,"POST /api/scan HTTP/1.")){if(a->streaming){response(c,"409 Conflict","text/plain","Stop audio before scanning.",27);return;}if(scan(a)){response(c,"503 Service Unavailable","text/plain","Scan could not start",20);return;}}
 else if(starts(c->input,"POST /api/select/")){
  u64 mac=parse_mac(c->input+17);refresh_devices(a);struct bt_device*d=device(a,mac);
  if(!mac||!d||!bt_is_audio(d)){response(c,"400 Bad Request","text/plain","Select a listed audio device.",29);return;}
  struct bt_device chosen=*d;u64 old=a->target.address;end_scan(a);stop_audio(a);
  if(save_target(a,&chosen)){response(c,"500 Internal Server Error","text/plain","Cannot save selection.",22);return;}
  if(old!=mac&&a->bt>=0)sc3(54,a->bt,0x8008620b,(long)&old);
  a->wanted=1;a->next_try=0;a->dropped=a->failed=a->announced=a->session=0;a->attempts=0;sc3(10,(long)TV_ON,0,0);a->tv=0;say(a,"Connecting to ","...");
 }
 else if(starts(c->input,"POST /api/pin/")){
  char pin[17];unsigned n=0;const char*p=c->input+14;while(p[n]>='0'&&p[n]<='9'&&n<16){pin[n]=p[n];n++;}
  if(!a->pin_pending||!n||!starts(p+n," HTTP/1.")){response(c,"400 Bad Request","text/plain","Invalid pairing PIN.",20);return;}
  pin[n]=0;struct{u64 address;char*pin;unsigned length,pad;}q={a->target.address,pin,n,0};long rc=sc3(54,a->bt,0x80186207,(long)&q);
  if(rc){response(c,"500 Internal Server Error","text/plain","PIN submission failed.",22);return;}a->pin_pending=0;message(a,"Pairing PIN submitted.");
 }
 else if(starts(c->input,"POST /api/volume/")){
  const char*p=c->input+17;unsigned value=0,n=0;
  while(p[n]>='0'&&p[n]<='9'&&n<3){value=value*10+(unsigned)(p[n]-'0');n++;}
  if(!n||value>100||!starts(p+n," HTTP/1.")){response(c,"400 Bad Request","text/plain","Volume must be 0-100.",21);return;}
  if(save_volume(a,(int)value)){response(c,"500 Internal Server Error","text/plain","Cannot save volume.",19);return;}
 }
 else if(starts(c->input,"POST /api/test HTTP/1.")){
  if(!a->streaming||a->bt<0||a->btstate<5||a->btstate>6){response(c,"409 Conflict","text/plain","Connect headphones first.",25);return;}
  a->test_phase=0;a->test_next=0;a->test_until=a->clock()+2000000;message(a,"Playing test tone.");
 }
 else if(starts(c->input,"POST /api/connect HTTP/1.")){a->wanted=1;sc3(10,(long)TV_ON,0,0);a->tv=0;if(!a->streaming){end_scan(a);a->phase=0;a->next_try=0;a->dropped=a->failed=a->announced=a->session=0;a->attempts=0;say(a,"Connecting to ","...");}}
 else if(starts(c->input,"POST /api/stop HTTP/1.")){a->wanted=0;end_scan(a);stop_audio(a);closefd(a->bt);a->bt=-1;a->dropped=a->failed=a->announced=a->session=0;a->attempts=0;message(a,"Earbud audio stopped. TV sound restored.");}
 else if(starts(c->input,"POST /api/tv-on HTTP/1.")){if(put(TV_ON,"on\n",3)){response(c,"500 Internal Server Error","text/plain","Cannot save preference.",23);return;}a->tv=1;mute(a,0);}
 else if(starts(c->input,"POST /api/tv-off HTTP/1.")){if(exists(TV_ON)&&sc3(10,(long)TV_ON,0,0)<0){response(c,"500 Internal Server Error","text/plain","Cannot save preference.",23);return;}a->tv=0;if(a->streaming)mute(a,1);}
 else if(starts(c->input,"POST /api/quit HTTP/1.")){a->wanted=0;stop_audio(a);a->quit=1;a->dropped=a->failed=a->announced=a->session=0;a->attempts=0;message(a,"Bluetooth Audio stopped. TV sound restored.");}
 else{response(c,"404 Not Found","text/plain","Not found",9);return;}
 unsigned n=status(a,body,sizeof(body));response(c,"200 OK","application/json",body,n);
}
static void network_tick(struct app*a,u64 now){
 for(int i=0;i<4;i++){
  struct client*c=&a->clients[i];
  if(c->fd<0){struct sockaddr4 peer;u32 len=sizeof(peer);long fd=sc3(30,a->listener,(long)&peer,(long)&len);if(fd<0)continue;
   if(len!=16||!local_ip((u8*)&peer.addr)||nonblock(fd)<0){closefd(fd);continue;}int one=1;sc5(105,fd,0xffff,0x800,(long)&one,4);c->fd=fd;c->used=c->sent=c->total=0;c->input[0]=0;c->deadline=now+3000000;
  }
  if(now>c->deadline){closefd(c->fd);c->fd=-1;continue;}
  if(!c->total){long n=sc3(3,c->fd,(long)(c->input+c->used),sizeof(c->input)-c->used-1);
   if(n==0||(n<0&&n!=-35)){closefd(c->fd);c->fd=-1;continue;}
   if(n>0){c->used+=(unsigned)n;c->input[c->used]=0;if(find(c->input,"\r\n\r\n"))dispatch(a,c);else if(c->used==sizeof(c->input)-1)response(c,"431 Request Header Fields Too Large","text/plain","Request too large",17);}
  }
  if(c->total){long n=sc3(4,c->fd,(long)(c->output+c->sent),c->total-c->sent);if(n>0)c->sent+=(unsigned)n;if(c->sent==c->total||(n<0&&n!=-35)){closefd(c->fd);c->fd=-1;}}
 }
}
void*bta_worker(void*arg){
 struct app a;memset(&a,0,sizeof(a));a.listener=(long)arg;a.bt=a.hdmi=a.shmfd=-1;a.muted=-1;a.wanted=0;for(int i=0;i<4;i++)a.clients[i].fd=-1;
 if(resolve("sceKernelUsleep",&a.sleep)||resolve("sceKernelGetProcessTime",&a.clock)||resolve("shm_open",&a.shm)||resolve("mmap",&a.map)||resolve("munmap",&a.unmap)||!a.sleep||!a.clock||!a.shm||!a.map||!a.unmap){closefd(a.listener);return 0;}
 sc3(136,(long)"/data/bta_audio",0777,0);sc3(15,(long)"/data/bta_audio",0777,0);sc3(10,(long)BTA_STOP,0,0);load_target(&a);load_volume(&a);a.tv=exists(TV_ON);a.hdmi=sc3(5,(long)"/dev/hdmi",2,0);
 message(&a,"Ready. Select your headphones and press Connect.");notify("Bluetooth Audio is ready and idle. Open the home-screen app, then press Connect. Web controls: http://127.0.0.1:8097/ (or your PS4 IP on port 8097).");
 while(!a.quit){u64 now=a.clock();
  if(now>=a.net_at){network_tick(&a,now);a.net_at=now+4000;}
  if(a.quit)break;
  if(now>=a.poll_at){bluetooth_tick(&a,now);a.poll_at=now+50000;}
  audio_tick(&a,now);
  /* Output meter: peak of the last ~80 ms, with a quick decay so a client that
   * polls a few times a second still sees movement instead of a random window. */
  if(now>=a.meter_at){unsigned p=a.meter_peak*100/32767;if(p>100)p=100;unsigned hold=(unsigned)a.meter*7/10;a.meter=(int)(p>hold?p:hold);a.meter_peak=0;a.meter_at=now+80000;}
  if(now>=a.heartbeat){
   char json[12000];unsigned n=status(&a,json,sizeof(json));put("/data/bta_audio/status.json",json,n);a.level=0;a.heartbeat=now+1000000;
   if(exists(BTA_STOP))break;a.tv=exists(TV_ON);
   /* Disconnect is detected by the audio send failing (see audio_tick), as the
    * proven bridge did. No per-second driver state poll while streaming. */
  }
  a.sleep(500);
 }
 end_scan(&a);stop_audio(&a);a.wanted=0;message(&a,"Bluetooth Audio is offline. TV sound restored.");char json[12000];unsigned n=status(&a,json,sizeof(json));put("/data/bta_audio/status.json",json,n);
 for(int i=0;i<4;i++)closefd(a.clients[i].fd);closefd(a.listener);closefd(a.bt);closefd(a.hdmi);
 return 0;
}
