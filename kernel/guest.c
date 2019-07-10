#include <cdefs.h>
#include <defs.h>
#include <param.h>
#include <proc.h>
#include <fcntl.h>
#include <syscall_message.h>
#include <trap.h>
#include <memlayout.h>
#include <x86_64vm.h>

extern int nextcid;

// for initproc to startup guest os
int
sys_fork_guest(void)
{
  int num_pages;
  if (argint(0, &num_pages) < 0)
    return -1;
  return fork_guest(num_pages);
}

// sys_guestcall forwards syscall to guest_os process
int 
sys_app_syscall(void)
{
  // FIXME: eventually want to not have to copy a fixed-sized char* buffer over;
  // instead, kernel should translate a char* into a new address space
  // entering syscall from guest user process
  int sys_num;
  struct arg *args;
  struct syscall_message *new_message;

  // get syscall number
  if(argint(0, &sys_num) < 0)
    return -1;

  // get args
  if(argptr(1, (void*)&args, sizeof(struct arg)*MAX_ARGS) < 0)
    return -1;

  new_message = (struct syscall_message *) kalloc();

  new_message->pid = myproc()->pid;
  new_message->syscall_index = sys_num;
  new_message->num_args = args[0].arg_val.i;

  // copy args into new message (first arg is argc)
  for(int i = 0; i < args[0].arg_val.i; i++) {
    (new_message->args)[i].arg_type = args[i+1].arg_type;
    (new_message->args)[i].arg_val = args[i+1].arg_val;
  }
 
  insert_syscall(new_message, GUEST_PID);

  // WORKFLOW: switch to guest OS from guest user process, put guest user
  // process to sleep, guest os calls write, then wake up guest user process
  // when that finishes
  lock_ptable();
  wakeup1(findproc(GUEST_PID));
  sleep_process2(myproc());
  unlock_ptable();
  return 0;
}

// Inserts the given syscall message (created by a guest user syscall) into the
// syscall message buffer of the guest os with the given guest id (pid).
void
insert_syscall(struct syscall_message* new_message, int gid) 
{
  new_message->next_message = NULL;

  // insert new syscall message at end of parent syscall buffer
  struct syscall_message *parent_syscall_buffer = findproc(gid)->syscall_buffer;
  if (parent_syscall_buffer == NULL) {
    // no messages in buffer; add at front
    findproc(gid)->syscall_buffer = new_message;
  } else {
    // stop at end of buffer and add new message
    while (parent_syscall_buffer != NULL && 
           parent_syscall_buffer->next_message != NULL) {
      parent_syscall_buffer = parent_syscall_buffer->next_message;
    }
    parent_syscall_buffer->next_message = new_message;
  }
}

// Priveleged system calls (for a guest OS)

// Returns the number of children of this guest OS.
int 
sys_gnum_children(void) 
{
  return num_children(); // defined in proc.c
}

// Puts the next syscall into the given syscall_message or puts the current
// process (a guest OS) to sleep if there are no syscalls waiting for it. 
int
sys_gnext_syscall(void)
{
  struct syscall_message *s;
  if (argptr(0, (void *) &s, sizeof(struct syscall_message)) < 0) {
    // pointer failed to load
    return -1;
  }

  struct syscall_message* curr_s = myproc()->syscall_buffer;
  while (myproc()->syscall_buffer == NULL) {
    // buffer was empty; put guest to sleep for now
    sleep_process(myproc());
    curr_s = myproc()->syscall_buffer;
  }
  *s = *curr_s;

  // update buffer to next item and free syscall
  myproc()->syscall_buffer = curr_s->next_message;
  kfree((char *) curr_s);
  return 0;
}

// Wakes up the guest user process with the given pid.
// do not wakeup shell, only app user processes!
int
sys_gresume(void)
{
  int pid;
  if (argint(0, &pid) < 0)
    return -1;
  lock_ptable();
  findproc(pid)->state = RUNNABLE;
  sleep_process2(myproc());
  unlock_ptable();
  return 0;
}

// Copies the user's physical memory "bitmap" from the proc struct
// to the given array. To avoid messy bit shifting, we are using a
// full uint8_t byte for each boolean value in the bitmap.
int
sys_gquery_user_pages(void)
{
  uint8_t *page_map;
  if(argptr(0, (void *) &page_map, sizeof(uint8_t) * MAX_PHYS_PAGES) < 0)
    return -1;
  int num_free_pages = 0;
  for (int i = 0; i < MAX_PHYS_PAGES; i++) {
    uint8_t owned = myproc()->user_pages[i];
    if (owned == 1) {
      num_free_pages++;
    }
    page_map[i] = owned;
  }
  return num_free_pages;
}

int
sys_gload_program(void)
{
  int pid;
  char *path;

  if(argint(0, &pid) < 0)
    goto bad;
  if (myproc()->app_processes[pid].owned != 1)
    goto bad;
  if(argstr(1, &path) < 0)
    goto bad;
  struct proc *new_proc = findproc(pid);

  // save heap and base since vspaceloadcode has side effect of setting if after code
  uint64_t heap_base = new_proc->vspace.regions[VR_HEAP].va_base;
  uint64_t heap_size = new_proc->vspace.regions[VR_HEAP].size;

  uint64_t rip;
  // Side effect: heap region is set to right above code start, set back to 0 and change the start later
  if (vspaceloadcode(&new_proc->vspace, path, &rip) == 0) // load code at 0, not mapped in guest os
    goto bad;

  new_proc->vspace.regions[VR_HEAP].va_base = heap_base;
  new_proc->vspace.regions[VR_HEAP].size = heap_size;
  
  // set rip
  new_proc->tf->rip = rip;  // set program counter for new program
  vspaceinvalidate(&new_proc->vspace);
  return 0;

  bad:
        // Close all open files.
    for(int fd = 0; fd < NOFILE; fd++){
      if(new_proc->ofile[fd]){
        fileclose(new_proc->ofile[fd]);
        new_proc->ofile[fd] = 0;
      }
    }
    new_proc->parent = 0;

    kfree((char *) new_proc->kstack);
    kfree((char *) new_proc->vspace.pgtbl);

    lock_ptable();
    // Parent might be sleeping in wait().
    wakeup1(findproc(SHELL_PID));
    new_proc->state = UNUSED;
    unlock_ptable();
    return -1;
}

// allocates a proc, sets base, midpoint, and bound for va boundaries
// va of guest os and app process mapped the same
// start of stack and heap are set to midpoint
// returns pid, -1 on failure
int
sys_grequest_proc(void)
{
  // get app_va_segment to store guest os copy of base, midpoint, bound
  struct app_va_segment* proc_map;
  if(argptr(0, (void *) &proc_map, sizeof(struct app_va_segment) * MAX_PROC) < 0) {
    return -1;
  }

  // get base, midpoint, bound arguments
  uint64_t base = fetcharg(1);
  uint64_t midpoint = fetcharg(2);
  uint64_t bound = fetcharg(3);
  
  // check boundaries
  if (bound > KERNBASE || base >= midpoint || midpoint >= bound) {
    return -1;
  }

  // allocate space for a new proc
  struct proc *new_proc = allocproc(nextcid++);

  // set parent as shell since shell waits on children to exit
  new_proc->parent = findproc(SHELL_PID);
  
  new_proc->is_guest_os = 0;

  int pid = new_proc->pid;

  // get ptable proc array
  struct proc *proc_arr = get_proc_arr();

  // update guest os proc array if any children are unused or zombies
  for (int i = 0 ; i < MAX_PROC; i++) {
    int pid = proc_arr[i].pid;
    if (proc_map[pid].owned == 1 && (proc_arr[i].state == UNUSED || proc_arr[i].state == ZOMBIE)) {
      proc_map[pid].owned = 0;
    }
  }

  // copy file descriptors to app proc
  for(int i = 0; i < NOFILE; i++)
    if(findproc(SHELL_PID)->ofile[i])
      new_proc->ofile[i] = filedup(myproc()->ofile[i]);

  udiskcopy(myproc()->cid, new_proc->cid);

  // update guest os copy of boundaries
  proc_map[pid].owned = 1;
  proc_map[pid].base = base;
  proc_map[pid].midpoint = midpoint;
  proc_map[pid].bound = bound;

  // update kernel copy of boundaries
  myproc()->app_processes[pid].owned = 1;
  myproc()->app_processes[pid].base = base;
  myproc()->app_processes[pid].midpoint = midpoint;
  myproc()->app_processes[pid].bound = bound;

  // setup pml4 page table and region directions
  vspaceinit(&new_proc->vspace);

  // reset trap frame and set trap frame registers (based on initproc settings)
  memset(new_proc->tf, 0, sizeof(*new_proc->tf));
  new_proc->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  new_proc->tf->ss = (SEG_UDATA << 3) | DPL_USER;
  new_proc->tf->rflags = FLAGS_IF;

  // update start and size of guest_os VR_APP_HEAP, VR_APP_USTACK
  // base, size, dir
  myproc()->vspace.regions[VR_APP_HEAP].va_base = midpoint;
  myproc()->vspace.regions[VR_APP_HEAP].size = 0;
  myproc()->vspace.regions[VR_APP_HEAP].dir = VRDIR_UP;
  myproc()->vspace.regions[VR_APP_USTACK].va_base = midpoint;
  myproc()->vspace.regions[VR_APP_USTACK].size = 0;
  myproc()->vspace.regions[VR_APP_USTACK].dir = VRDIR_DOWN;

  // update application region fields
  new_proc->vspace.regions[VR_HEAP].va_base = midpoint;
  new_proc->vspace.regions[VR_HEAP].size = 0;
  new_proc->vspace.regions[VR_HEAP].dir = VRDIR_UP;
  new_proc->vspace.regions[VR_USTACK].va_base = midpoint;
  new_proc->vspace.regions[VR_USTACK].size = 0;
  new_proc->vspace.regions[VR_USTACK].dir = VRDIR_DOWN;

  // flush new mappings from vspace to pml4
  vspaceinvalidate(&new_proc->vspace);
  vspaceinstall(myproc());
  return pid;
}

// 0 on success, -1 on failure
int 
sys_gaddmap(void) {
  int pid;
  int ppn;
  uint64_t va;
  int app_present;
  int app_writeable;

  // check if guest os owns proc
  if(argint(0, &pid) < 0 || myproc()->app_processes[pid].owned == 0)
    return -1;

  // check if guest os owns physical page
  if (argint(1, &ppn) < 0 || myproc()->user_pages[ppn] == 0)
    return -1;

  va = fetcharg(2);
  // check if va is within base and bounds
  if (va < myproc()->app_processes[pid].base || va > myproc()->app_processes[pid].bound)
    return -1;

  if(argint(3, &app_present) < 0)
    return -1;

  if(argint(4, &app_writeable) < 0)
    return -1;

  // set application entry
  struct proc *app_proc = findproc(pid);
  pte_t *app_entry = walkpml4(app_proc->vspace.pgtbl, (void *) va, 1);
  int app_perm = PTE_U;

  if (app_present)
    app_perm |= PTE_P;

  if (app_writeable)
    app_perm |= PTE_W;

  *app_entry = PTE(ppn << PT_SHIFT, app_perm);

  // set guest entry
  pte_t *guest_entry = walkpml4(myproc()->vspace.pgtbl, (void *) va, 1);
  *guest_entry = PTE(ppn << PT_SHIFT, PTE_P | PTE_U | PTE_W);

  if (va < myproc()->app_processes[pid].midpoint) {
    app_proc->vspace.regions[VR_USTACK].size += PGSIZE;
    myproc()->vspace.regions[VR_APP_USTACK].size += PGSIZE;
  } else {
    app_proc->vspace.regions[VR_HEAP].size += PGSIZE;
    myproc()->vspace.regions[VR_APP_HEAP].size += PGSIZE;
  }

  // flush pml4
  vspaceinstall(myproc());
  myproc()->user_pages[ppn] = 2;
  return 0;
} 

// returns ppn of unmapped page
int 
sys_gremovemap(void) {
  int pid;
  if(argint(0, &pid) < 0 || myproc()->app_processes[pid].owned == 0)
    return -1;

  uint64_t va = fetcharg(1);
  if (va > myproc()->app_processes[pid].bound || va < myproc()->app_processes[pid].base)
    return -1;

  pte_t *pte_guest = walkpml4(myproc()->vspace.pgtbl, (void *) va, 0);
  int ppn = PTE_ADDR(*pte_guest) >> PT_SHIFT;
  if (pte_guest == 0 || myproc()->user_pages[ppn] == 0)
    return -1;

  struct proc *app_proc = findproc(pid);
  pte_t *pte_app = walkpml4(app_proc->vspace.pgtbl, (void *) va, 0);
  int ppn2 = PTE_ADDR(*pte_app) >> PT_SHIFT;
  if (pte_app == 0 || ppn != ppn2)
    return -1;

  // clear page table entries
  *pte_guest = 0;
  *pte_app = 0;
  myproc()->user_pages[ppn] = 1;

  // flush pml4
  vspaceinstall(myproc());
  // return ppn of page guest can reallocate for apps
  return ppn;
}

int 
sys_gupdate_flags(void) {
  int app_pid;
  uint64_t va;
  int present;
  int writeable;

  if(argint(0, &app_pid) < 0 || myproc()->app_processes[app_pid].owned == 0)
    return -1;

  va = fetcharg(1);
  
  if (va > myproc()->app_processes[app_pid].bound || va < myproc()->app_processes[app_pid].base)
    return -1;
  
  if (argint(1, &present) < 0 || argint(1, &writeable) < 0)
    return -1;

  pte_t *pte_guest = walkpml4(myproc()->vspace.pgtbl, (void *) va, 0);
  pte_t *pte_app = walkpml4(findproc(app_pid)->vspace.pgtbl, (void *) va, 0);
  
  if (pte_guest == 0 || pte_app == 0 || (PTE_ADDR(*pte_guest) >> PT_SHIFT) != (PTE_ADDR(*pte_app) >> PT_SHIFT))
    return -1;

  // set flags
  if (present == 1) {
    *pte_app |= PTE_P;
  } else {
    *pte_app &= ~PTE_P;
  }

  if (writeable == 1) {
    *pte_app |= PTE_W;
  } else {
    *pte_app &= ~PTE_W;
  }

  return 0;
}

// Helper for exec
// Round size of arg upwards to the nearest multiple of 8
int
multipleOfEight(char * arg) {
  int size = strlen(arg) + 1;   
  size /= 8;
  size++;  
  size *= 8;
  return size;
}

int
sys_gdeploy_program(void) {
  struct syscall_message *message;
  if (argptr(0, (void *) &message, sizeof(struct syscall_message)) < 0)
    goto bad;
  
  int app_pid = message->pid;
  if(myproc()->app_processes[app_pid].owned == 0)
    goto bad;

  struct proc *app_proc = findproc(app_pid);
  int argc = 0;
  char **argv = message->args[1].arg_val.char_ptr_ptr;
  char *topOfStack = myproc()->app_processes[app_pid].midpoint;
  char * pointers[MAXARG];

  while (*argv != '\0') {
    topOfStack -= multipleOfEight(*argv);
    pointers[argc] = topOfStack;
    copyout(app_proc->vspace.pgtbl, (uint64_t) topOfStack, (void *) *argv, strlen(*argv) + 1);
    argc++;
    argv++;
  }

  pointers[argc] = '\0';

  topOfStack -= (8 * (argc + 1));

  copyout(app_proc->vspace.pgtbl, (uint64_t) topOfStack, (void *) pointers, sizeof(uint64_t) * (argc + 1));

  app_proc->tf->rdi = argc;
  app_proc->tf->rsi = (uint64_t) topOfStack;

  topOfStack -= 8;

  app_proc->tf->rsp = (uint64_t) topOfStack;

  return 0;

  // cleanup proc
  bad:
    // Close all open files.
    for(int fd = 0; fd < NOFILE; fd++){
      if(app_proc->ofile[fd]){
        fileclose(app_proc->ofile[fd]);
        app_proc->ofile[fd] = 0;
      }
    }

    kfree((char *) app_proc->kstack);
    kfree((char *) app_proc->vspace.pgtbl);

    lock_ptable();
    // Parent might be sleeping in wait().
    wakeup1(findproc(SHELL_PID));
    app_proc->state = UNUSED;
    app_proc->parent = 0;
    unlock_ptable();
    return -1;
}