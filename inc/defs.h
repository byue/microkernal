#pragma once
#include <cdefs.h>

struct buf;
struct context;
struct extent;
struct inode;
struct proc;
struct rtcdate;
struct spinlock;
struct sleeplock;
struct stat;
struct superblock;
struct vpage_info;
struct vpi_page;
struct vregion;
struct vspace;
struct file;
struct pipe;
struct syscall_message;

extern int npages;
extern int pages_in_use;
extern int pages_in_swap;
extern int free_pages;
extern int num_page_faults;
extern int num_disk_reads;

extern int crashn_enable;
extern int crashn;

// http://www.decompile.com/cpp/faq/file_and_line_error_string.htm
#define _xk_str(x) #x
#define __xk_str(x) _xk_str(x)

#define assert(x)		\
	do { \
    if (!(x)) \
      panic("assertion failed: '" #x "' at " __FILE__ ":" __xk_str(__LINE__)); \
  } while (0)

#define assertm(x, msg)		\
	do { \
    if (!(x)) \
      panic("assertion failed: '" msg "' at " __FILE__ ":" __xk_str(__LINE__)); \
  } while (0)

// bio.c
void binit(void);
struct buf *bread(uint, uint);
void brelse(struct buf *);
void bwrite(struct buf *);

// console.c
void consoleinit(void);
void cprintf(char *, ...);
void consoleintr(int (*)(void));
noreturn void panic(char *);

// exec.c
int exec(char *, char **);

// file.c
void            fileinit(void);
struct file *   filealloc(void);
struct file *   filedup(struct file *);
void            fileclose(struct file *);
int             filestat(struct file *, struct stat *);
int             fileread(struct file *, char *, int);
int             filewrite(struct file *, char *, int);

// fs.c
void readsb(int dev, struct superblock *sb);
struct inode *dirlookup(struct inode *, char *, uint *);
struct inode *rootlookup(char *);
struct inode *idup(struct inode *);
void iinit(int dev);
void irelease(struct inode *);
int namecmp(const char *, const char *);
struct inode *namei(char *);
struct inode *nameiparent(char *, char *);
int readi(struct inode *, char *, uint, uint);
void stati(struct inode *, struct stat *);
int writei(struct inode *, char *, uint, uint);
void udiskinit(int);
void udiskcopy(int, int);
uint findBlocks(uint, uint);
void updateInodeFile(struct inode *);
void createInode(char *);

// guest.c
void insert_syscall(struct syscall_message*, int);

// ide.c
void ideinit(void);
void ideintr(void);
void iderw(struct buf *);

// ioapic.c
void ioapicenable(int irq, int cpu);
extern uchar ioapicid;
void ioapicinit(void);

// kalloc.c
struct core_map_entry *pa2page(uint64_t pa);
void detect_memory(void);
char *kalloc(void);
void kfree(char *);
void mem_init(void *);
void mark_user_mem(uint64_t, uint64_t);
void mark_kernel_mem(uint64_t);
void kincref(uint64_t);

// kbd.c
void kbdintr(void);

// lapic.c
void cmostime(struct rtcdate *r);
int cpunum(void);
extern volatile uint *lapic;
void lapiceoi(void);
void lapicinit(void);
void lapicstartap(uchar, uint);
void microdelay(int);

// mp.c
extern int ismp;
void mpinit(void);

// vspace.c
void                vspacebootinit(void);
int                 vspaceinit(struct vspace *);
void                vspaceinitcode(struct vspace *, char *, uint64_t);
int                 vspaceloadcode(struct vspace *, char *, uint64_t *);
void                vspaceinvalidate(struct vspace *);
void                vspaceinstall(struct proc *);
void                vspaceinstallkern(void);
void                vspacefree(struct vspace *);
struct vregion*     va2vregion(struct vspace *, uint64_t);
struct vpage_info*  va2vpage_info(struct vregion *, uint64_t);
int                 vregioncontains(struct vregion *, uint64_t, int);
int                 vspacecontains(struct vspace *, uint64_t, int);
int                 vspacecopy(struct vspace *, struct vspace *);
int                 vspaceinitstack(struct vspace *, uint64_t);
int                 vspacewritetova(struct vspace *, uint64_t, char *, int);
int                 vregionaddmap(struct vregion *, uint64_t, uint64_t, short, short);
int                 vregiondelmap(struct vregion *, uint64_t, uint64_t);

int                 cow_vspacecopy(struct vspace *, struct vspace *);

// picirq.c
void picenable(int);
void picinit(void);

// proc.c
void exit(void);
int fork(void);
int growproc(int);
int kill(int);
void pinit(void);
void procdump(void);
noreturn void scheduler(void);
void sched(void);
void sleep(void *, struct spinlock *);
void sleep_process(void *);
void sleep_process2(void *);
void userinit(void);
int wait(void);
void wakeup(void *);
void wakeup1(void *);
void yield(void);
void reboot(void);
int num_children(void);
struct proc *findproc(int pid);
void wakeup_children(void);
int fork_guest(int num_pages);
struct proc *allocproc(int cid);
struct proc *get_proc_arr(void);
void unlock_ptable(void);
void lock_ptable(void);
void wakeup_apps(void);

// pipe.c
int pipealloc(struct file**, struct file**);
void pipeclose(struct pipe*, int);
int piperead(struct pipe*, char*, int);
int pipewrite(struct pipe*, char*, int);

// swtch.S
void swtch(struct context **, struct context *);

// spinlock.c
void acquire(struct spinlock *);
void getcallerpcs(void *, uint64_t *);
int holding(struct spinlock *);
void initlock(struct spinlock *, char *);
void release(struct spinlock *);
void pushcli(void);
void popcli(void);

// sleeplock.c
void acquiresleep(struct sleeplock *);
void releasesleep(struct sleeplock *);
int holdingsleep(struct sleeplock *);
void initsleeplock(struct sleeplock *, char *);

// string.c
int memcmp(const void *, const void *, uint);
void *memmove(void *, const void *, uint);
void *memset(void *, int, uint);
char *safestrcpy(char *, const char *, int);
int strlen(const char *);
int strncmp(const char *, const char *, uint);
char *strncpy(char *, const char *, int);

// syscall.c
int argint(int, int *);
int argint64(int, int64_t *);
int argptr(int, char **, int);
int argstr(int, char **);
int fetchint(uint64_t, int *);
int fetchint64_t(uint64_t, int64_t *);
int fetchstr(uint64_t, char **);
uint64_t fetcharg(int n);
void syscall(void);

// trap.c
void idtinit(void);
extern uint ticks;
void tvinit(void);
extern struct spinlock tickslock;

// uart.c
void uartinit(void);
void uartintr(void);
void uartputc(int);
// number of elements in fixed-size array
#define NELEM(x) (sizeof(x) / sizeof((x)[0]))
