/* Screen drawing for the native app. Pure C with no console dependencies, so the
 * PC harness (native/host_ui.c) can run this exact code and diff it pixel-for-pixel
 * against the preview renderer. Included by main.c. */
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../app/protocol.h"
#include "ui_font.h"
#include "ui_icons.h"

#define WIDTH 1280
#define HEIGHT 720
#define UI_OFFLINE 6

#define BG_TOP 0x0e1624
#define BG_BOT 0x090e17
#define CARD   0x141e2d
#define CARD2  0x1a2639
#define CARDF  0x253850
#define LINE   0x26364b
#define TEXT   0xf1f5fb
#define DIM    0xaab8cc
#define FAINT  0x6c7d94
#define SUBTLE 0x8393ab
#define MINT   0x85e4c0
#define AMBER  0xffc56e
#define RED    0xff6b7b
#define BLUE   0x6cb4ff
#define GREY   0x8795ab
#define BTN_X  0x7ba7ff
#define BTN_SQ 0xff9ad5
#define BTN_TRI 0x5fe0a4
#define BTN_CI 0xff7a7a

static uint32_t back[WIDTH*HEIGHT]; /* cached off-screen target; the video buffers are slow to read back */
static struct bt_native state;
static int online,ext_ok,focus,top,meter_s;
static uint64_t g_now,testing_until,notice_until;
static char notice[160];

/* ---- drawing primitives. The PC previewer mirrors these exactly (integer math). ---- */
static int cx0,cy0,cx1=WIDTH,cy1=HEIGHT;
static void clip(int x0,int y0,int x1,int y1){cx0=x0<0?0:x0;cy0=y0<0?0:y0;cx1=x1>WIDTH?WIDTH:x1;cy1=y1>HEIGHT?HEIGHT:y1;}
static uint32_t mixc(uint32_t a,uint32_t b,unsigned t){
 return (((((a>>16)&255)*(256-t)+((b>>16)&255)*t)>>8)<<16)|(((((a>>8)&255)*(256-t)+((b>>8)&255)*t)>>8)<<8)|(((a&255)*(256-t)+(b&255)*t)>>8);
}
static void blend(int x,int y,uint32_t rgb,unsigned a){
 if(!a||x<cx0||x>=cx1||y<cy0||y>=cy1)return;
 uint32_t*p=&back[y*WIDTH+x];
 if(a>=255){*p=0xff000000u|rgb;return;}
 uint32_t bg=*p;unsigned ia=255-a;
 unsigned r=(((rgb>>16)&255)*a+((bg>>16)&255)*ia+127)/255;
 unsigned g=(((rgb>>8)&255)*a+((bg>>8)&255)*ia+127)/255;
 unsigned b=((rgb&255)*a+(bg&255)*ia+127)/255;
 *p=0xff000000u|(r<<16)|(g<<8)|b;
}
static void gradient(uint32_t top_c,uint32_t bot_c){
 for(int y=0;y<HEIGHT;y++){uint32_t v=0xff000000u|mixc(top_c,bot_c,(unsigned)((y*256)/(HEIGHT-1)));uint32_t*row=back+y*WIDTH;for(int x=0;x<WIDTH;x++)row[x]=v;}
}
static void fill(int x,int y,int w,int h,uint32_t rgb){
 int x0=x>cx0?x:cx0,y0=y>cy0?y:cy0,x1=x+w<cx1?x+w:cx1,y1=y+h<cy1?y+h:cy1;
 if(x1<=x0||y1<=y0)return;
 uint32_t v=0xff000000u|rgb;
 for(int j=y0;j<y1;j++){uint32_t*row=back+j*WIDTH;for(int i=x0;i<x1;i++)row[i]=v;}
}
/* Coverage (0..255) of pixel (cx,cy) in a top-left corner of radius r: 4x4 supersampling. */
static unsigned corner_cov(int r,int cx,int cy){
 int R8=r*8,n=0;
 for(int t=1;t<8;t+=2)for(int s=1;s<8;s+=2){int dx=8*cx+s-R8,dy=8*cy+t-R8;if(dx*dx+dy*dy<=R8*R8)n++;}
 return (unsigned)((n*255+8)/16);
}
static void rrect(int x,int y,int w,int h,int r,uint32_t rgb){
 if(w<=0||h<=0)return;
 if(r>w/2)r=w/2;if(r>h/2)r=h/2;
 if(r<=0){fill(x,y,w,h,rgb);return;}
 fill(x+r,y,w-2*r,h,rgb);fill(x,y+r,r,h-2*r,rgb);fill(x+w-r,y+r,r,h-2*r,rgb);
 for(int cy=0;cy<r;cy++)for(int cx=0;cx<r;cx++){
  unsigned a=corner_cov(r,cx,cy);if(!a)continue;
  blend(x+cx,y+cy,rgb,a);blend(x+w-1-cx,y+cy,rgb,a);blend(x+cx,y+h-1-cy,rgb,a);blend(x+w-1-cx,y+h-1-cy,rgb,a);
 }
}
static void disc(int cx,int cy,int r,uint32_t rgb,unsigned alpha){
 if(alpha>=255){rrect(cx-r,cy-r,2*r,2*r,r,rgb);return;}
 for(int j=0;j<2*r;j++)for(int i=0;i<2*r;i++){
  int qx=i<r?i:2*r-1-i,qy=j<r?j:2*r-1-j;
  blend(cx-r+i,cy-r+j,rgb,corner_cov(r,qx,qy)*alpha/255);
 }
}
static void icon(int id,int x,int y,uint32_t rgb,unsigned scale){
 const struct icon*ic=&ICONS[id];
 for(int j=0;j<ic->h;j++)for(int i=0;i<ic->w;i++){unsigned a=ic->a[j*ic->w+i];if(a)blend(x+i,y+j,rgb,a*scale/255);}
}
static void icon_c(int id,int cx,int cy,uint32_t rgb,unsigned scale){icon(id,cx-ICONS[id].w/2,cy-ICONS[id].h/2,rgb,scale);}
static void logo(int x,int y){
 for(int j=0;j<LOGO_H;j++)for(int i=0;i<LOGO_W;i++){unsigned p=logo_argb[j*LOGO_W+i];unsigned a=p>>24;if(a)blend(x+i,y+j,p&0xffffff,a);}
}
static void dim_frame(unsigned a){
 unsigned ia=255-a;
 for(int i=0;i<WIDTH*HEIGHT;i++){uint32_t v=back[i];back[i]=0xff000000u|((((v>>16)&255)*ia+127)/255<<16)|((((v>>8)&255)*ia+127)/255<<8)|(((v&255)*ia+127)/255);}
}

/* ---- text: proportional anti-aliased glyphs, 1/16 px advances ---- */
static int gl_index(unsigned char c){return(c<32||c>126)?63-32:c-32;}
static int text_w(int f,const char*s){
 const struct face*fc=&FACES[f];unsigned tot=0;
 for(;*s;s++)tot+=fc->g[gl_index((unsigned char)*s)].adv16;
 return(int)((tot+8)>>4);
}
static void text(int f,int x,int base,const char*s,uint32_t rgb){
 const struct face*fc=&FACES[f];int pen=x*16;
 for(;*s;s++){
  const struct glyph*g=&fc->g[gl_index((unsigned char)*s)];
  int gx=((pen+8)>>4)+g->bx,gy=base+g->by;
  for(int j=0;j<g->h;j++)for(int i=0;i<g->w;i++){unsigned a=fc->bits[g->off+j*g->w+i];if(a)blend(gx+i,gy+j,rgb,a);}
  pen+=g->adv16;
 }
}
#define TBUF 272
/* Returns s if it fits maxw, else a copy in buf shortened with "...". */
static const char*fit(int f,const char*s,int maxw,char*buf){
 if(text_w(f,s)<=maxw)return s;
 int dots=text_w(f,"...");size_t len=strlen(s);if(len>TBUF-8)len=TBUF-8;
 unsigned pref[TBUF];unsigned acc=0;pref[0]=0;
 for(size_t i=0;i<len;i++){acc+=FACES[f].g[gl_index((unsigned char)s[i])].adv16;pref[i+1]=acc;}
 size_t n=len;while(n>0&&(int)((pref[n]+8)>>4)+dots>maxw)n--;
 while(n>0&&s[n-1]==' ')n--;
 memcpy(buf,s,n);memcpy(buf+n,"...",4);return buf;
}
static void text_fit(int f,int x,int base,const char*s,uint32_t rgb,int maxw){char b[TBUF];text(f,x,base,fit(f,s,maxw,b),rgb);}
static void text_c(int f,int cx,int base,const char*s,uint32_t rgb,int maxw){char b[TBUF];s=fit(f,s,maxw,b);text(f,cx-text_w(f,s)/2,base,s,rgb);}
static void text_r(int f,int rx,int base,const char*s,uint32_t rgb){text(f,rx-text_w(f,s),base,s,rgb);}
/* Greedy wrap to at most two lines (second line ellipsised if text remains). */
static int wrap2(int f,const char*s,int maxw,char l1[TBUF],char l2[TBUF]){
 char lines[3][TBUF];int nl=0,more=0;char cur[TBUF];cur[0]=0;
 const char*p=s;
 while(*p){
  const char*q=p;while(*q&&*q!=' ')q++;
  size_t wl=(size_t)(q-p);if(wl>TBUF/2)wl=TBUF/2;
  char t[TBUF];size_t cl=strlen(cur);
  if(cl)snprintf(t,sizeof(t),"%s %.*s",cur,(int)wl,p);else snprintf(t,sizeof(t),"%.*s",(int)wl,p);
  if(text_w(f,t)<=maxw||!cl)memcpy(cur,t,strlen(t)+1);
  else{if(nl<3)memcpy(lines[nl++],cur,cl+1);snprintf(cur,sizeof(cur),"%.*s",(int)wl,p);if(nl>=3){more=1;break;}}
  p=*q?q+1:q;
 }
 if(!more){if(nl<3)memcpy(lines[nl++],cur,strlen(cur)+1);else more=1;}
 char b[TBUF];
 if(nl<=1&&!more){snprintf(l1,TBUF,"%s",fit(f,lines[0],maxw,b));l2[0]=0;return 1;}
 snprintf(l1,TBUF,"%s",fit(f,lines[0],maxw,b));
 if(nl>2||more){char t[TBUF];snprintf(t,sizeof(t),"%s ...",lines[1]);snprintf(l2,TBUF,"%s",fit(f,t,maxw,b));}
 else snprintf(l2,TBUF,"%s",fit(f,lines[1],maxw,b));
 return 2;
}
/* 8- and 10-dot spinner offsets (cos,sin * 1024), starting at 12 o'clock. */
static const short SPIN8[8][2]={{0,-1024},{724,-724},{1024,0},{724,724},{0,1024},{-724,724},{-1024,0},{-724,-724}};
static const short SPIN10[10][2]={{0,-1024},{602,-828},{974,-316},{974,316},{602,828},{0,1024},{-602,828},{-974,316},{-974,-316},{-602,-828}};
static void spinner(int cx,int cy,int r,uint32_t rgb,uint64_t t_ms,int dots,int dr){
 const short(*tab)[2]=dots==8?SPIN8:SPIN10;int head=(int)((t_ms/90)%(uint64_t)dots);
 for(int i=0;i<dots;i++){
  int age=(head-i+dots)%dots,a=255-age*28;if(a<40)a=40;
  disc(cx+((r*tab[i][0]+512)>>10),cy+((r*tab[i][1]+512)>>10),dr,rgb,(unsigned)a);
 }
}

static int ui_link(void){
 if(!online)return UI_OFFLINE;
 if(ext_ok&&state.link>=BT_LINK_IDLE&&state.link<=BT_LINK_SEARCHING)return state.link;
 return state.streaming?BT_LINK_STREAMING:(state.wanted?BT_LINK_CONNECTING:BT_LINK_IDLE);
}
/* ---- screen ---- */
static void dev_name(const struct bt_device*d,char*out,size_t cap){
 const char*src=d->name;size_t len=strlen(src),n=len;
 while(n>0&&(src[n-1]=='?'||src[n-1]==' '))n--;
 int q=0;for(size_t i=n;i<len;i++)if(src[i]=='?')q++;
 if(n<len&&q<2){n=len;while(n>0&&src[n-1]==' ')n--;}
 if(n==0){snprintf(out,cap,"Unnamed device %04lX",d->address&0xffff);return;}
 if(n>=cap)n=cap-1;memcpy(out,src,n);out[n]=0;
}
static uint32_t link_color(int l){
 switch(l){case BT_LINK_STREAMING:return MINT;case BT_LINK_CONNECTING:return AMBER;case BT_LINK_SEARCHING:return BLUE;
  case BT_LINK_LOST:case BT_LINK_FAILED:case UI_OFFLINE:return RED;default:return GREY;}
}
static int secs(int ms){return(ms+999)/1000;}
static void draw(int pinmode,const char*pin,int digit){
 uint64_t t=g_now/1000;int l=ui_link();uint32_t col=link_color(l);int n=state.count;
 int retry=ext_ok?state.retry_ms:0,attempts=ext_ok?state.attempts:0,quiet=ext_ok?state.quiet_ms:0;
 int testing=(ext_ok&&state.test)||g_now<testing_until;
 char buf[TBUF],buf2[TBUF];
 clip(0,0,WIDTH,HEIGHT);gradient(BG_TOP,BG_BOT);

 /* header */
 logo(56,34);
 text(F_TITLE,146,74,"Bluetooth Audio",TEXT);
 text(F_SMALL,148,103,"PS4 game audio on your wireless headphones",SUBTLE);
 const char*pill=l==BT_LINK_STREAMING?"Connected":l==BT_LINK_CONNECTING?"Connecting":l==BT_LINK_SEARCHING?"Searching":l==BT_LINK_LOST?"Disconnected":l==BT_LINK_FAILED?"Can't connect":l==BT_LINK_IDLE?"Not connected":"Service offline";
 int pw=text_w(F_SMALLB,pill)+20+24+20,px=1224-pw;
 rrect(px,48,pw,44,22,mixc(CARD,col,36));
 unsigned pulse=(l==BT_LINK_CONNECTING||l==BT_LINK_SEARCHING)?150+(unsigned)((t/6)%100):255;
 disc(px+26,70,6,col,pulse>255?255:pulse);
 text(F_SMALLB,px+44,77,pill,mixc(TEXT,col,90));

 /* status strip: the one place problems are spelled out */
 const char*msg=state.message;char full[TBUF];
 int has_banner=1;uint32_t bc=AMBER;int spin=0;const char*btext=0;
 if(notice[0]&&g_now<notice_until){bc=AMBER;btext=notice;}
 else if(l==UI_OFFLINE){bc=RED;btext="The audio service isn't running. Press X to start it, or jailbreak again with GoldHEN's BinLoader enabled.";}
 else if(l==BT_LINK_LOST||l==BT_LINK_FAILED){bc=RED;
  if(retry>0)snprintf(full,sizeof(full),"%s Next try in %ds.",msg,secs(retry));else snprintf(full,sizeof(full),"%s",msg);btext=full;}
 else if(l==BT_LINK_CONNECTING){bc=AMBER;spin=1;btext=msg;}
 else if(l==BT_LINK_SEARCHING){bc=BLUE;spin=1;btext=msg;}
 else if(l==BT_LINK_STREAMING&&quiet>4000){bc=AMBER;btext="Connected, waiting for sound from the PS4. Play something or press Options to test.";}
 else has_banner=0;
 if(has_banner){
  rrect(56,124,1168,72,16,mixc(CARD,bc,30));rrect(56,124,6,72,3,bc);
  if(spin)spinner(98,160,14,bc,t,8,3);else{disc(98,160,17,bc,255);icon_c(I_BANG,98,160,BG_TOP,255);}
  int nl=wrap2(F_BODY,btext,1168-100-28,buf,buf2);
  if(nl==1)text(F_BODY,130,168,buf,TEXT);else{text(F_BODY,130,154,buf,TEXT);text(F_BODY,130,182,buf2,TEXT);}
 }else{
  disc(70,160,5,col,255);
  text(F_BODY,90,168,l==BT_LINK_STREAMING?"Game audio is playing on your headphones.":"Choose your headphones, then press X to connect.",DIM);
 }

 /* hero card */
 rrect(56,212,392,384,22,CARD);
 int cx=252,cy=298;
 clip(56,212,448,596);icon_c(I_GLOW,cx,cy,col,l!=BT_LINK_IDLE?95:30);clip(0,0,WIDTH,HEIGHT);
 disc(cx,cy,64,mixc(CARD,col,60),255);disc(cx,cy,64,col,255);
 disc(cx,cy,61,l!=BT_LINK_IDLE?mixc(CARD,col,52):CARD2,255);
 icon_c(I_HERO,cx,cy-1,l!=BT_LINK_IDLE?TEXT:FAINT,255);
 if(l==BT_LINK_CONNECTING||l==BT_LINK_SEARCHING)spinner(cx,cy,80,col,t,10,4);
 int badge=l==BT_LINK_STREAMING?I_CHECK:l==BT_LINK_LOST?I_BANG:l==BT_LINK_FAILED?I_XMARK:l==UI_OFFLINE?I_BANG:-1;
 if(badge>=0){disc(cx+46,cy+46,20,CARD,255);disc(cx+46,cy+46,17,col,255);icon_c(badge,cx+46,cy+46,BG_TOP,255);}
 const struct bt_device*target=0;for(int i=0;i<n;i++)if(state.devices[i].address==state.selected)target=&state.devices[i];
 if(target&&l!=BT_LINK_IDLE&&l!=UI_OFFLINE){dev_name(target,buf,sizeof(buf));}else snprintf(buf,sizeof(buf),"No headphones connected");
 text_c(F_NAME,cx,406,buf,TEXT,352);
 char sl[64];
 switch(l){
  case BT_LINK_STREAMING:snprintf(sl,sizeof(sl),"Playing");break;
  case BT_LINK_CONNECTING:if(attempts>1)snprintf(sl,sizeof(sl),"Connecting  (attempt %d)",attempts);else snprintf(sl,sizeof(sl),"Connecting");break;
  case BT_LINK_SEARCHING:snprintf(sl,sizeof(sl),"Searching");break;
  case BT_LINK_LOST:snprintf(sl,sizeof(sl),"Connection lost");break;
  case BT_LINK_FAILED:snprintf(sl,sizeof(sl),"Can't connect");break;
  case BT_LINK_IDLE:snprintf(sl,sizeof(sl),"Not connected");break;
  default:snprintf(sl,sizeof(sl),"Service offline");break;
 }
 if(testing)snprintf(sl,sizeof(sl),"Playing test tone");
 text_c(F_BODY,cx,438,sl,l!=BT_LINK_IDLE?col:DIM,352);
 char detail[64];detail[0]=0;
 if(l==BT_LINK_STREAMING)snprintf(detail,sizeof(detail),state.tv?"TV sound is on":"TV sound is muted");
 else if(l==BT_LINK_LOST||l==BT_LINK_FAILED){if(retry>0)snprintf(detail,sizeof(detail),"Retrying in %ds",secs(retry));else snprintf(detail,sizeof(detail),"Retrying...");}
 else if(l==BT_LINK_IDLE)snprintf(detail,sizeof(detail),"Pick headphones on the right");
 if(detail[0])text_c(F_SMALL,cx,466,detail,FAINT,352);
 if(l==BT_LINK_STREAMING&&ext_ok){
  rrect(106,488,292,8,4,LINE);int fw=292*meter_s/100;if(fw>=8)rrect(106,488,fw,8,4,MINT);
 }
 icon_c(I_SPEAKER,98,548,DIM,255);
 rrect(124,544,236,8,4,LINE);int vw=236*state.volume/100;if(vw>=8)rrect(124,544,vw,8,4,MINT);
 disc(124+vw,548,9,TEXT,255);
 snprintf(buf,sizeof(buf),"%d%%",state.volume);text_r(F_BODY,424,556,buf,TEXT);

 /* headphone list card */
 rrect(472,212,752,384,22,CARD);
 text(F_NAME,500,252,"Your headphones",TEXT);
 if(state.scanning){spinner(1168,242,9,BLUE,t,8,2);text_r(F_SMALL,1148,249,"Scanning",BLUE);}
 if(n==0){
  icon_c(I_HERO,848,372,LINE,255);
  text_c(F_NAME,848,452,"No headphones found",DIM,640);
  text_c(F_SMALL,848,482,"Put them in pairing mode, then press Triangle to scan.",FAINT,640);
 }
 for(int row=0;row<5;row++){
  int i=top+row;if(i>=n)break;
  int y=268+row*64;const struct bt_device*d=&state.devices[i];
  int is_t=d->address==state.selected&&l!=BT_LINK_IDLE&&l!=UI_OFFLINE,sel=focus==i;
  rrect(488,y,720,60,14,sel?CARDF:CARD2);
  if(sel)rrect(488,y+11,5,38,2,MINT);
  uint32_t ic=is_t?col:FAINT;
  disc(528,y+30,22,mixc(sel?CARDF:CARD2,ic,40),255);
  icon_c(I_ROWHP,528,y+30,is_t?mixc(TEXT,ic,60):DIM,255);
  char sub[64];sub[0]=0;uint32_t subc=FAINT;
  if(is_t){
   switch(l){
    case BT_LINK_CONNECTING:if(attempts>1)snprintf(sub,sizeof(sub),"Connecting... attempt %d",attempts);else snprintf(sub,sizeof(sub),"Connecting...");break;
    case BT_LINK_SEARCHING:snprintf(sub,sizeof(sub),"Searching...");break;
    case BT_LINK_LOST:snprintf(sub,sizeof(sub),"Connection lost - reconnecting");break;
    case BT_LINK_FAILED:snprintf(sub,sizeof(sub),"Can't connect");break;
    default:break;
   }
   subc=col;
  }else if(d->address==state.selected&&l==BT_LINK_IDLE){snprintf(sub,sizeof(sub),"Last used");}
  dev_name(d,buf,sizeof(buf));
  text_fit(F_NAME,562,y+(sub[0]?27:39),buf,TEXT,440);
  if(sub[0])text_fit(F_SMALL,562,y+51,sub,subc,440);
  if(sel&&!is_t){int cw=text_w(F_SMALLB,"Connect");icon_c(I_BCROSS,1190-cw-18,y+30,BTN_X,255);text_r(F_SMALLB,1190,y+37,"Connect",TEXT);}
  else if(is_t){
   const char*label=l==BT_LINK_STREAMING?"PLAYING":l==BT_LINK_CONNECTING?"CONNECTING":l==BT_LINK_SEARCHING?"SEARCHING":l==BT_LINK_LOST?"LOST":"FAILED";
   int lw=text_w(F_SMALLB,label)+28;rrect(1190-lw,y+15,lw,30,15,mixc(CARD2,col,50));text(F_SMALLB,1190-lw+14,y+37,label,col);
  }
 }
 if(n>5){snprintf(buf,sizeof(buf),"%d-%d of %d",top+1,top+5<n?top+5:n,n);text_r(F_SMALL,state.scanning?1040:1196,250,buf,FAINT);}

 /* controls */
 const char*act="Connect";
 if(!online)act="Retry";
 else if(focus>=0&&focus<n){const struct bt_device*fd=&state.devices[focus];if(fd->address==state.selected&&l!=BT_LINK_IDLE)act=l==BT_LINK_STREAMING?"Disconnect":"Stop trying";}
 uint32_t testc=l==BT_LINK_STREAMING?DIM:FAINT;
 char tvlabel[32];snprintf(tvlabel,sizeof(tvlabel),"TV: %s",state.tv?"On":"Muted");
 const char*tri=!online||l==BT_LINK_IDLE?"Scan":l==BT_LINK_STREAMING?"Disconnect":"Stop trying";
 struct{int icon;uint32_t ic;const char*tag1,*tag2,*label;uint32_t lc;}items[6]={
  {I_BCROSS,BTN_X,0,0,act,DIM},{I_BSQUARE,BTN_SQ,0,0,tvlabel,DIM},{I_BTRI,BTN_TRI,0,0,tri,DIM},
  {-1,0,"OPTIONS",0,"Test sound",testc},{-1,0,"L1","R1","Volume",DIM},{I_BCIRCLE,BTN_CI,0,0,"Close",DIM}};
 int x=56;
 for(int k=0;k<6;k++){
  int w=text_w(F_SMALL,items[k].label)+30;int t1=0,t2=0;
  if(items[k].icon>=0)w+=34;
  if(items[k].tag1){t1=text_w(F_SMALLB,items[k].tag1)+18;w+=t1+8;if(items[k].tag2){t2=text_w(F_SMALLB,items[k].tag2)+18;w+=t2+6;}}
  rrect(x,620,w,46,23,CARD);
  int tx=x+16;
  if(items[k].icon>=0){disc(x+25,643,15,CARD2,255);icon_c(items[k].icon,x+25,643,items[k].ic,255);tx=x+48;}
  if(items[k].tag1){
   tx=x+12;rrect(tx,630,t1,26,8,CARD2);text(F_SMALLB,tx+9,650,items[k].tag1,items[k].lc);tx+=t1+6;
   if(items[k].tag2){rrect(tx,630,t2,26,8,CARD2);text(F_SMALLB,tx+9,650,items[k].tag2,items[k].lc);tx+=t2+6;}
   tx+=2;
  }
  text(F_SMALL,tx,650,items[k].label,items[k].lc);
  x+=w+12;
 }

 if(pinmode){
  dim_frame(150);
  rrect(340,180,600,360,24,0x1b2a40);rrect(340,180,600,6,3,MINT);
  text_c(F_TITLE,640,252,"Pairing PIN",TEXT,520);
  text_c(F_BODY,640,292,"These headphones ask for a PIN. Try 0000 or 1234.",DIM,540);
  for(int i=0;i<4;i++){
   int bx=418+i*112;rrect(bx,330,92,100,16,i==digit?MINT:CARD2);
   char tch[2]={pin[i],0};text_c(F_TITLE,bx+46,400,tch,i==digit?BG_TOP:TEXT,80);
  }
  text_c(F_SMALL,640,480,"Left/Right: digit    Up/Down: change    X: submit    O: cancel",FAINT,540);
 }
}
