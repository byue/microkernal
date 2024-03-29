// init: The initial user-level program

#include <cdefs.h>
#include <fcntl.h>
#include <stat.h>
#include <user.h>
#include <syscall_message.h>

char *argv[] = {"sh", 0};

int main(void) {
  int pid, wpid;

  if (open("console", O_RDWR) < 0) {
    mknod("console", 1, 1);
    open("console", O_RDWR);
  }
  dup(0); // stdout
  dup(0); // stderr

  // startup guest OS
  char *argv2[] = {"guest_os", 0};
  if (fork_guest(DEFAULT_USER_PAGES) == 0) {
    exec("guest_os", argv2);
  } else {
    for (;;) {
      printf(1, "init: starting sh\n");
      pid = fork();
      if (pid < 0) {
        printf(1, "init: fork failed\n");
        exit();
      }
      if (pid == 0) {
        exec("sh", argv);
        printf(1, "init: exec sh failed\n");
        exit();
      }
      while ((wpid = wait()) >= 0 && wpid != pid)
        printf(1, "zombie!\n");
    }
  }
  return 0;
}
