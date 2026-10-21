/* Temporary 13.52 Bluetooth service-acceptance patch, with byte fingerprint,
 * atomic replacement, readback, and a separately buildable rollback.
 * Changes no flash or firmware files. Requires the exact live code inspected. */
typedef unsigned long u64;
__attribute__((noinline)) static long sc3(long n,long a,long b,long c) {
    long out;
    __asm__ volatile("syscall; jnc 1f; neg %%rax; 1:" : "=a"(out),"+d"(c),"+D"(a),"+S"(b) : "a"(n) : "rcx","r8","r9","r10","r11","cc","memory");
    return out;
}
struct sw { u64 size; char text[28]; unsigned version; };
struct result {u64 before, after;};
struct args { void *fn; struct result *out; };
static int patch(void *td,struct args *a) {
    (void)td;unsigned lo,hi;
    __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(0xc0000082));
    u64 base=(((u64)hi<<32)|lo)-0x1c0;
    if ((base>>48)!=0xffff || (base&0x3fff)) return 22;
    volatile u64 *site=(void*)(base+0x5a1334);
    /* cmp dword [rax+0x20],0x40; jne reject; first 2 bytes of next insn.
     * -> test byte [rax+0x20],0x48; je reject. 0x40 = HID, 0x08 = audio.
     * All other allowlist paths and connection-limit checks stay in place. */
    u64 original=0x8b492a7540207883UL, replacement=0x8b492a74482040f6UL;
#ifdef ROLLBACK
    u64 swap=original; original=replacement; replacement=swap;
#endif
    struct result out={*site,*site};
    int rc=0;
    if(out.before==original) {
        u64 flags,cr0,expected=original;
        __asm__ volatile("pushfq; popq %0; cli; mov %%cr0,%1":"=r"(flags),"=r"(cr0)::"memory");
        u64 writable=cr0&~0x10000UL;
        __asm__ volatile("mov %0,%%cr0"::"r"(writable):"memory");
        __asm__ volatile("lock cmpxchgq %2,(%1)":"+a"(expected):"r"(site),"r"(replacement):"cc","memory");
        __asm__ volatile("mov %0,%%cr0; pushq %1; popfq"::"r"(cr0),"r"(flags):"cc","memory");
        out.after=*site;
        if(expected!=original || out.after!=replacement) rc=5;
    } else if(out.before!=replacement) rc=22;
    int(*copyout)(const void*,void*,u64)=(void*)(base+0x2bd6a0);
    int copied=copyout(&out,a->out,sizeof(out));
    return copied?copied:rc;
}
__attribute__((section(".text.start"))) int _start(void) {
    struct sw v={.size=sizeof(v)};int(*getver)(void*)=0;
    if(sc3(591,0x2001,(long)"sceKernelGetSystemSwVersion",(long)&getver)||!getver||getver(&v)||v.version!=0x13520001)return 1;
    struct {long status;struct result result;} r={0};
    r.status=sc3(11,(long)patch,(long)&r.result,0);
    long fd=sc3(5,(long)"/data/bt_accept_patch_result.bin",0x601,0644);
    if(fd>=0){sc3(4,fd,(long)&r,sizeof(r));sc3(6,fd,0,0);}
    return (int)r.status;
}
