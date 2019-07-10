#include <syscall.h>
#include <trap.h>

#define SYSCALL(name)                                                          \
  .globl name;                                                                 \
  name:                                                                        \
  movl $SYS_##name, % eax;                                                     \
  int $TRAP_SYSCALL;                                                           \
  ret

SYSCALL(fork)
SYSCALL(exit)
SYSCALL(wait)
SYSCALL(pipe)
SYSCALL(read)
SYSCALL(write)
SYSCALL(close)
SYSCALL(kill)
SYSCALL(exec)
SYSCALL(open)
SYSCALL(mknod)
SYSCALL(unlink)
SYSCALL(fstat)
SYSCALL(link)
SYSCALL(mkdir)
SYSCALL(chdir)
SYSCALL(dup)
SYSCALL(getpid)
SYSCALL(sbrk)
SYSCALL(sleep)
SYSCALL(uptime)
SYSCALL(sysinfo)
SYSCALL(crashn)
SYSCALL(app_syscall)
SYSCALL(fork_guest)

SYSCALL(gnum_children)
SYSCALL(gnext_syscall)
SYSCALL(gresume)
SYSCALL(gquery_user_pages)
SYSCALL(grequest_proc)
SYSCALL(gload_program)
SYSCALL(gaddmap)
SYSCALL(gremovemap)
SYSCALL(gupdate_flags)
SYSCALL(gdeploy_program)