#include <user.h>
#include <cdefs.h>
#include <syscall_message.h>
#include <guest_space.h>
#include <memlayout.h>

// TODO: When app process exits fix ppn bitmap in guest OS and in xkvisor. Use unmap.
//       When guest app exits we can send a cleanup message to guest OS to update these data
//       structures.
//       Also fix proc map in guest OS and xkvisor since the counts just increase.

// guest os copies of xkvisor bitmaps. xkvisor will upate these through system call return params
static struct app_va_segment proc_map[MAX_PROC];
static uint8_t page_map[MAX_PHYS_PAGES];   

// helpers
static int next_available_ppn(void);

// guest OS syscall handler
void guest_syscall(struct syscall_message syscall);

// guest syscalls
int guest_write(struct syscall_message syscall);
int guest_init_app(struct syscall_message syscall);

// guest OS syscall table
static int (*syscalls[])(struct syscall_message syscall) = {
    [MESSAGE_WRITE] = guest_write,       // do an xkvisor write
    [MESSAGE_INIT_APP] = guest_init_app, // create user process in guest_init_app
};

void guest_syscall(struct syscall_message syscall) {
  int num = syscall.syscall_index;
  if(num > 0 && num <= MAX_GUEST_SYSCALLS && syscalls[num]) {
    syscalls[num](syscall);
  } else {
    printf(STDOUT, "pid: %d, unknown syscall %d", syscall.pid, num);
    kill(syscall.pid);
  }
}

static int new_app_pid;

int main(int argc, char *argv[]) {
  printf(STDOUT, "Booting Guest OS...\n");

  int available_pages = gquery_user_pages(page_map); // copies kernel proc page map to stack
                                                    // returns number of available pages
  printf(STDOUT, "%d pages allocated for guest ppn reserve\n", available_pages);

  struct syscall_message syscall;

  // TODO: fix new_app_id since this is hacky. We want gresume to always resume
  // the application process even if syscall.pid is shell. new_app_pid is set
  // whenever we allocate a new proc.

  for(;;) {
    gnext_syscall(&syscall); // will put guest OS to sleep if no syscalls ready
    guest_syscall(syscall);
    // don't resume shell, only resume new app process. resume shell on error
    gresume(new_app_pid); 
  }
}

int guest_write(struct syscall_message syscall) {
  int fd = (syscall.args)[0].arg_val.i;
  char c = (syscall.args)[1].arg_val.c;
  int n = (syscall.args)[2].arg_val.i;

  int written;
  if ((written = write(fd, &c, n)) < 0) {
    // FIXME: move checking to user lib
    printf(STDOUT, "kill write failed...\n");
    kill(syscall.pid);
  }
  return written;
}

int guest_init_app(struct syscall_message syscall) {
  // get argv and argc
  int argc = syscall.num_args;
  char *argv[argc + 1];
  argv[argc] = '\0';

  for (int i = 0; i < argc; i++) {
    argv[i] = syscall.args[i].arg_val.string;
  }

  // set bounds for guest application
  uint64_t base = SZ_2G;
  uint64_t midpoint = SZ_3G;
  uint64_t bound = SZ_4G;
  printf(STDOUT, "VA Bounds Set At: 2G, 3G, 4G\n");

  // request a proc from xkvisor, set base, midpoint, bound as uint64_t in proc_map, see guest_space.h
  // parent of new proc is set currently to shell, shell cleans up memory.
  new_app_pid = grequest_proc(proc_map, base, midpoint, bound);

  // TODO: when app exits we need to zero app process array in guest os and kernel proc copy.
  printf(STDOUT, "Received New Process, PID: %d\n", new_app_pid);

  // load code of new program and set rip, sets code region to 0, sets heap start
  // and size of heap to 0. unmapped in guest os, mapped to 0 in process
  if (gload_program(new_app_pid, argv[0]) == -1) {
    printf(STDOUT, "exec failed on: %s\n", argv[0]);
    // TODO: add error handling, cleanup proc mem, reset bitmap for page and proc
    new_app_pid = SHELL_PID;
    return -1;
  }

  // set 10 pages on the stack
  for (int i = 1; i <= 10; i++) {
    int ppn = next_available_ppn();
    gaddmap(new_app_pid, ppn, midpoint - (i * PGSIZE), 1, 1);
    printf(STDOUT, "Mapping ppn %d at VA: 0x%x\n", ppn, midpoint - (i * PGSIZE));
  }

  // run program
  struct syscall_message param;
  struct arg app_argc;
  struct arg app_argv;

  // store arguments.
  param.pid = new_app_pid;
  param.num_args = 2;
  app_argc.arg_type = INT_TYPE;
  app_argc.arg_val.i = argc;
  app_argv.arg_type = CHAR_PTR_PTR_TYPE;
  app_argv.arg_val.char_ptr_ptr = argv;
  param.args[0] = app_argc;
  param.args[1] = app_argv;

  // setup program arguments and rsp etc.
  // TODO: Fix argument setup in gdeployprogram
  if (gdeploy_program(&param) == -1) {
    printf(STDOUT, "guest app deployment failed");
    new_app_pid = SHELL_PID;
    return -1;
  } 

  // only set runnable in next syscall gresume to prevent deadlock
  return 1;
}

// retrieves next available ppn from page pool. sets to 2 marking as in use.
// 1 is available, 0 is not owned.
static int next_available_ppn(void) 
{
  for (int i = 0; i < MAX_PHYS_PAGES; i++) {
    if (page_map[i] == 1) {
      page_map[i] = 2; // set page as in use by app
      return i;
    }
  }
  return -1;
}
