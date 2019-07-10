#include <cdefs.h>
#include <fcntl.h>
#include <stat.h>
#include <stdarg.h>
#include <user.h>

// Will print an error message and loop
#define error(msg, ...)                                                        \
  do {                                                                         \
    printf(stdout, "ERROR (line %d): ", __LINE__);                             \
    printf(stdout, msg, ##__VA_ARGS__);                                        \
    printf(stdout, "\n");                                                      \
    while (1)                                                                  \
      ;                                                                        \
  } while (0)

// After error functionality is tested, this is just a convenience
#define assert(a)                                                              \
  do {                                                                         \
    if (!(a)) {                                                                \
      printf(stdout, "Assertion failed (line %d): %s\n", __LINE__, #a);        \
      while (1)                                                                \
        ;                                                                      \
    }                                                                          \
  } while (0)

int stdout = 1;

void testinvalidargs(void);
void smallfilereadtest(void);
void duptest(void);
void createtest(void);
void smallfilewritetest(void);
void filetest(void);

void forktest(void);

int main() {
  int pid, wpid;
  if(open("console", O_RDWR) < 0){
    return -1;
  }
  dup(0);     // stdout
  dup(0);     // stderr

  pid = fork();
  if (pid < 0) {
    printf(1, "fork failed\n");
    exit();
  }

  if (pid == 0) {

  printf(1, "hello world\n");
  filetest();
  forktest();

  printf(stdout, "file-system tests passed!\n");

//  while (1)
//    ;
  }

  while ((wpid = wait()) >= 0 && wpid != pid)
    printf(1, "zombie!\n");

  exit();
  return 0;
}

void testinvalidargs(void) {
  int fd, i;
  char buf[11];

  // open

  if (open("/other.txt", O_RDONLY) != -1)
    error("opened a file that doesn't exist");

  printf(stdout, "passed argument checking for open\n");

  // read
  if (read(15, buf, 11) != -1)
    error("read on a non existent file descriptor");

  fd = open("/small.txt", O_RDONLY);

  if ((i = read(fd, buf, -100)) != -1)
    error("negative n didn't return error, return value was '%d'", i);

  if (read(fd, (char *)0xffffff00, 10) != -1)
    error("able to read to a buffer not in my memory region");

  printf(stdout, "passed argument checking for read\n");

  // write
  if (write(15, buf, 11) != -1)
    error("write on a non existent file descriptor");

  printf(stdout, "passed argument checking for write\n");

  // dup
  if (dup(15) != -1)
    error("able to duplicated a non open file");

  printf(stdout, "passed argument checking for dup\n");

  // close
  if (close(15) != -1)
    error("able to close non open file");

  if (close(fd) > 0 && close(fd) != -1)
    error("able to close same file twice");

  printf(stdout, "passed argument checking for close\n");
}

void createtest(void) {
  int fd;

  fd = open("other.txt", O_CREATE);
  if (fd < 0)
    error("unable to create other file");

  if(close(fd) != 0)
      error("unable to close newly created file");

  fd = open("/other.txt", O_RDONLY);
  if (fd < 0)
    error("unable to reopen created file");

  assert(close(fd) == 0);

  printf(stdout, "create other file succeeded; ok\n");
}

void smallfilereadtest(void) {
  int fd, i;
  char buf[11];

  printf(stdout, "small file test\n");
  // Test read only funcionality
  fd = open("/small.txt", O_RDONLY);
  if (fd < 0)
    error("unable to open small file");

  printf(stdout, "open small file succeeded; ok\n");

  if ((i = read(fd, buf, 10)) != 10)
    error("read of first 10 bytes unsucessful was %s bytes", "6");

  buf[10] = 0;
  if (strcmp(buf, "aaaaaaaaaa") != 0)
    error("buf was not 10 a's, was: '%s'", buf);
  printf(stdout, "read of first 10 bytes sucessful\n");

  if ((i = read(fd, buf, 10)) != 10)
    error("read of second 10 bytes unsucessful was %d bytes", i);

  buf[10] = 0;
  if (strcmp(buf, "bbbbbbbbbb") != 0)
    error("buf was not 10 b's, was: '%s'", buf);
  printf(stdout, "read of second 10 bytes sucessful\n");

  // only 25 byte file
  if ((i = read(fd, buf, 10)) != 6)
    error("read of last 6 bytes unsucessful was %d bytes", i);

  buf[6] = 0;
  if (strcmp(buf, "ccccc\n") != 0)
    error("buf was not 5 c's (and a newline), was: '%s'", buf);

  printf(stdout, "read of last 5 bytes sucessful\n");

  if (read(fd, buf, 10) != 0)
    error("read more bytes than should be possible");

  if (close(fd) != 0)
    error("error closing fd");

  printf(stdout, "small file test ok\n");
}

void duptest(void) {
  int fd1, fd2, stdout_cpy;
  char buf[100];

  printf(stdout, "dup test\n");
  // Test read only funcionality
  fd1 = open("/small.txt", O_RDONLY);
  if (fd1 < 0)
    error("unable to open small file");

  if ((fd2 = dup(fd1)) != fd1 + 1)
    error("returned fd from dup was not the smallest free fd, was '%d'", fd2);

  // test offsets are respected in dupped files
  assert(read(fd1, buf, 10) == 10);
  buf[10] = 0;

  if (strcmp(buf, "aaaaaaaaaa") != 0)
    error("couldn't read from original fd after dup");

  if (read(fd2, buf, 10) != 10)
    error("coudn't read from the dupped fd");
  buf[10] = 0;

  if (strcmp(buf, "aaaaaaaaaa") == 0)
    error("the duped fd didn't respect the read offset from the other file.");

  if (strcmp(buf, "bbbbbbbbbb") != 0)
    error(
        "the duped fd didn't read the correct 10 bytes at the 10 byte offset");

  if (close(fd1) != 0)
    error("closing the original file");

  if (read(fd2, buf, 5) != 5)
    error("wasn't able to read from the duped file after the original file was "
          "closed");

  buf[5] = 0;
  assert(strcmp(buf, "ccccc") == 0);

  printf(stdout, "dup read offsets ok\n");

  if (close(fd2) != 0)
    error("closing the duped file");

  // test duping of stdout
  // should be fd1, because that is the first file I opened (and closed) earlier
  if ((stdout_cpy = dup(stdout)) != fd1)
    error("returned fd from dup that was not the smallest free fd, was '%d'",
          stdout_cpy);

  char *consolestr = "print to console directly from write\n";
  strcpy(buf, consolestr);

  if (write(stdout_cpy, consolestr, strlen(consolestr)) != strlen(consolestr))
    error("couldn't write to console from duped fd");

  assert(close(stdout_cpy) == 0);

  printf(stdout, "dup test ok\n");
}

void smallfilewritetest(void) {
  int fd, i;
  char buf[8192];

  printf(stdout, "small file write test starting\n");

  strcpy(buf, "stdout\n");

  if ((i = write(stdout, buf, 7)) != 7)
    error("wasn't able to write to stdout, return value was '%d'", i);

  printf(stdout, "write to stdout directly ok\n");

  strcpy(buf, "lab5 is 451's last lab.\n");
  fd = open("small.txt", O_RDWR);
  write(fd, buf, 50);
  close(fd);

  fd = open("small.txt", O_RDONLY);
  read(fd, buf, 50);

  if (strcmp(buf, "lab5 is 451's last lab.\n") != 0)
    error("file content was not lab5 is 451's last lab., was: '%s'", buf);

  close(fd);

  printf(stdout, "write to small file ok\n");

  printf(stdout, "small file write test ok!\n");
}

void filetest() {
  testinvalidargs();
  smallfilereadtest();
  duptest();
  createtest();
  smallfilewritetest();
}


void forktest(void) {
  int n, pid;
  int nproc = 4;

  printf(1, "forktest\n");

  for (n = 0; n < nproc; n++) {
    pid = fork();
    if (pid < 0)
      break;
    if (pid == 0) {
      exit();
      error("forktest: exit failed to destroy this process");
    }
  }

  if (n != nproc) {
    error("forktest: fork claimed to work %d times! but only %d\n", nproc, n);
  }

  for (; n > 0; n--) {
    if (wait() < 0) {
      error("forktest: wait stopped early\n");
    }
  }

  if (wait() != -1) {
    error("forktest: wait got too many\n");
  }

  printf(1, "forktest: fork test OK\n");
}

