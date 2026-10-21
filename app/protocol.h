#ifndef BT_AUDIO_PROTOCOL_H
#define BT_AUDIO_PROTOCOL_H
#define BT_DEVICE_LIMIT 32
#define BT_NATIVE_MAGIC 0x32415442u
struct bt_device { unsigned long address; unsigned cod,services,flags,reserved; char name[96]; };
struct bt_devices { unsigned count,reserved; struct bt_device items[BT_DEVICE_LIMIT]; };
/* What the headphone link is really doing. A UI must show this, not guess it
 * from "selected" or "wanted": a selected device may be offline or retrying. */
#define BT_LINK_IDLE 0       /* nothing requested */
#define BT_LINK_CONNECTING 1 /* attempt in progress */
#define BT_LINK_STREAMING 2  /* audio is flowing to the headphones */
#define BT_LINK_LOST 3       /* was streaming, link dropped, reconnecting */
#define BT_LINK_FAILED 4     /* last attempt failed, waiting to retry */
#define BT_LINK_SEARCHING 5  /* scanning for the selected headphones */
struct bt_native { unsigned magic,version; int streaming,wanted,tv,muted,scanning,count,pin_pending,volume; unsigned long selected; char message[128]; struct bt_device devices[BT_DEVICE_LIMIT];
 /* Appended fields (service 2.5+). Apps built before them read only the prefix
  * and ignore the rest; apps built with them must tolerate a shorter reply. */
 int link;          /* BT_LINK_* */
 int attempts;      /* connection attempts since the last successful stream */
 int retry_ms;      /* until the next automatic attempt; 0 = none pending */
 int meter;         /* recent output peak, 0..100 */
 int quiet_ms;      /* while streaming: ms since a packet was last sent */
 int test;          /* test tone playing */
 int connect_rc,audio_rc; /* last driver results; 0 = fine */
};
static inline int bt_is_audio(const struct bt_device*d){return (d->services&8)||(((d->cod>>8)&31)==4&&!(d->services&0x40));}
int bt_list(struct bt_devices*);
int bt_prepare(unsigned long address);
#endif
