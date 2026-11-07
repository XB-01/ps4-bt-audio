#ifndef BTA_FIRMWARE_H
#define BTA_FIRMWARE_H
/* Kernel-relative addresses. 11.50 recovered from the test console's live
 * kernel; see docs/firmware-11.50.md for validation and build instructions. */
#ifdef BTA_FW_1150
#define BTA_SW_VERSION 0x11500001u
#define BTA_SW_VERSION_MASK 0xffff0000u
#define BTA_FW_LABEL "11.50"
#define BTA_ACCEPT_SITE 0x5a0844
#define BTA_COPYOUT 0x2bd4a0
#define BTA_CACHE_LOOKUP 0x5b6a80
#define BTA_HCI_COMMAND 0x5ad8c0
#define BTA_BT_LOCK 0x5a6900
#define BTA_BT_UNLOCK 0x5a6980
#define BTA_BT_CONTEXT 0x2612518
#define BTA_HCI_FORMAT 0xad4082
#else
#define BTA_SW_VERSION 0x13520001u
#define BTA_SW_VERSION_MASK 0xffffffffu
#define BTA_FW_LABEL "13.520.001"
#define BTA_ACCEPT_SITE 0x5a1334
#define BTA_COPYOUT 0x2bd6a0
#define BTA_CACHE_LOOKUP 0x5b7570
#define BTA_HCI_COMMAND 0x5ae3b0
#define BTA_BT_LOCK 0x5a73f0
#define BTA_BT_UNLOCK 0x5a7470
#define BTA_BT_CONTEXT 0x2612550
#define BTA_HCI_FORMAT 0xad7562
#endif
#define BTA_SW_MATCHES(v) (((v) & BTA_SW_VERSION_MASK) == (BTA_SW_VERSION & BTA_SW_VERSION_MASK))

static inline int bta_kernel_matches(unsigned long base) {
    if ((base >> 48) != 0xffff || (base & 0x3fff)) return 0;
    if (*(const unsigned long *)(base + BTA_CACHE_LOOKUP) != 0x56415741e5894855UL ||
        *(const unsigned long *)(base + BTA_HCI_COMMAND) != 0x56415741e5894855UL)
        return 0;
#ifdef BTA_FW_1150
    /* Include cache bounds/stride, lock targets, command body and copyout.
     * Fingerprints are checked before any kernel call or patch write. */
    if (*(const unsigned long *)(base + BTA_BT_LOCK) != 0xdd3d8d48e5894855UL ||
        *(const unsigned long *)(base + BTA_BT_LOCK + 8) != 0x00000010e80206b4UL ||
        *(const unsigned long *)(base + BTA_BT_UNLOCK) != 0xfb895053e5894855UL ||
        *(const unsigned long *)(base + BTA_COPYOUT) != 0x048b4865e5894855UL ||
        *(const unsigned long *)(base + BTA_CACHE_LOOKUP + 10) != 0x4800018da8878d48UL ||
        *(const unsigned long *)(base + BTA_CACHE_LOOKUP + 0x39) != 0x4800000408c38148UL ||
        *(const unsigned long *)(base + BTA_HCI_COMMAND + 8) != 0xec83485354415541UL ||
        *(const unsigned short *)(base + BTA_HCI_FORMAT) != 0x3232 ||
        *(const unsigned char *)(base + BTA_HCI_FORMAT + 2) != 0)
        return 0;
#endif
    return 1;
}
#endif
