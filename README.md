Virtual Memory V3

Relevant Files:
+ guest os is user/guest_os.c
+ guest os application is user/guest_test.c
+ user/aprintf.c utilizes app_syscall to do writes
+ inc/guest_space.h has structs for keeping track of VA segments
+ inc/syscall_message.h has structs for holding syscall information and arguments
+ kernel/guest.c has implementation of priveleged syscalls for guest os to use

Current Execution flow:
+ initproc starts
+ guest_os starts as a background process, parent is initproc
+ shell process starts, parent is initproc
+ type in guest_test to console, shell sends create message to guest_os
+ guest_os sets up new process/memory for guest_test, guest_test runs
+ guest_test finishes, cleaned up by shell (parent of guest_test is set to shell)

Completed:
+ guest_test works with syscall redirection/guest os memory management. Prints hello world successfully.
+ Designed inc/syscall_message.h interface for passing system call arguments/index for arbitrary argument types
+ Implemented simple interface for creating processes
+ Implemented simple interface for managing application memory
+ Implemented simple interface for receving syscalls and putting syscalls into buffer (guest_os and app take turns sleeping)

TODO:
+ Implement trap redirection (Can use syscall buffer or make a new one) to handle page faults etc.
+ Have guest OS clean up app instead of shell
+ Run shell from guest os instead of initproc (and don't have shell as parent of guest_test, andon't hardcode PIDs)
+ start guest OS with assembly file similar to initproc instead of using guest_fork in init.c
+ incorporate vspace into implementation (Currently using raw pml4 since vspaceinvalidate was not working) -- see branch v2 for a start on vspace implementation
+ Create page tables in guest os (use shadow page tables possibly; currently we are just interfacing with page tables in kernel)
+ Improve proc interface (currently we can only lease pids from xkvisor to guest_os) -- ideally implement a proc at user-level and have privileged system calls for scheduling so that guest_os is not using xkvisor's proc.
+ Integrate with file system, multiprocessor, and user thread groups
+ cleanup ppn/proc maps (see static arrays in guest_os.c and kernel copies in proc.h) when guest_test finishes in guest_os, update both kernel and guest os copies. Currently
  all the stack pages for the guest_test are just being leaked.
+ Instead of using ppn maps, consider calling mapping directly into the page table of guest_os.

To add a new syscall change the following files:
+ syscall.c (add extern declaration, update sycalls table)
+ guest.c (implement syscall)
+ usys.S (add symbol for syscall)
+ user.h (add syscall declaration for app to use)
+ syscall.h (add macro with syscall number)

To add a new user program binary to shell:
+ open the kernel Makefrag, change section at line 72 to your new .c and .o files
