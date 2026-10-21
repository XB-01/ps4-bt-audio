/* PC harness: renders one scene with the app's real drawing code (ui.h) and writes a
 * PPM, so the screen can be checked without the console.
 *   cl /nologo /O2 /TC /I build\native /Fe:host_ui.exe native\host_ui.c
 *   host_ui scene.txt out.ppm
 * scene.txt is key=value lines (see apply()). Addresses are truncated to the host's
 * 32-bit `unsigned long`; scenes use addresses that stay distinct after that. */
#include "ui.h"

static int pinmode,pin_digit;
static char pin_text[5]="0000";

static void apply(const char*k,const char*v){
 if(!strcmp(k,"online"))online=atoi(v);
 else if(!strcmp(k,"ext"))ext_ok=atoi(v);
 else if(!strcmp(k,"link"))state.link=atoi(v);
 else if(!strcmp(k,"wanted"))state.wanted=atoi(v);
 else if(!strcmp(k,"streaming"))state.streaming=atoi(v);
 else if(!strcmp(k,"tv"))state.tv=atoi(v);
 else if(!strcmp(k,"muted"))state.muted=atoi(v);
 else if(!strcmp(k,"scanning"))state.scanning=atoi(v);
 else if(!strcmp(k,"volume"))state.volume=atoi(v);
 else if(!strcmp(k,"selected"))state.selected=(unsigned long)strtoull(v,0,16);
 else if(!strcmp(k,"message"))snprintf(state.message,sizeof(state.message),"%s",v);
 else if(!strcmp(k,"attempts"))state.attempts=atoi(v);
 else if(!strcmp(k,"retry_ms"))state.retry_ms=atoi(v);
 else if(!strcmp(k,"meter"))state.meter=atoi(v);
 else if(!strcmp(k,"quiet_ms"))state.quiet_ms=atoi(v);
 else if(!strcmp(k,"test"))state.test=atoi(v);
 else if(!strcmp(k,"focus"))focus=atoi(v);
 else if(!strcmp(k,"top"))top=atoi(v);
 else if(!strcmp(k,"t"))g_now=(uint64_t)atoll(v)*1000u;
 else if(!strcmp(k,"meter_s"))meter_s=atoi(v);
 else if(!strcmp(k,"notice")){snprintf(notice,sizeof(notice),"%s",v);}
 else if(!strcmp(k,"pin"))pinmode=atoi(v);
 else if(!strcmp(k,"pin_text"))snprintf(pin_text,sizeof(pin_text),"%s",v);
 else if(!strcmp(k,"pin_digit"))pin_digit=atoi(v);
 else if(!strcmp(k,"dev")){
  struct bt_device*d=&state.devices[state.count];const char*bar=strrchr(v,'|');if(!bar||state.count>=BT_DEVICE_LIMIT)return;
  size_t n=(size_t)(bar-v);if(n>95)n=95;memcpy(d->name,v,n);d->name[n]=0;
  d->address=(unsigned long)strtoull(bar+1,0,16);state.count++;
 }
}
int main(int argc,char**argv){
 if(argc<3){fprintf(stderr,"usage: host_ui scene.txt out.ppm\n");return 2;}
 FILE*f=fopen(argv[1],"r");if(!f){fprintf(stderr,"cannot open scene\n");return 2;}
 char line[512];
 while(fgets(line,sizeof(line),f)){
  size_t n=strlen(line);while(n&&(line[n-1]=='\n'||line[n-1]=='\r'))line[--n]=0;
  char*eq=strchr(line,'=');if(!eq)continue;*eq=0;apply(line,eq+1);
 }
 fclose(f);
 notice_until=notice[0]?g_now+1000000u:0;
 draw(pinmode,pin_text,pin_digit);
 FILE*o=fopen(argv[2],"wb");if(!o)return 3;
 fprintf(o,"P6\n%d %d\n255\n",WIDTH,HEIGHT);
 for(int i=0;i<WIDTH*HEIGHT;i++){unsigned char px[3];px[0]=(unsigned char)(back[i]>>16);px[1]=(unsigned char)(back[i]>>8);px[2]=(unsigned char)back[i];fwrite(px,1,3,o);}
 fclose(o);return 0;
}
