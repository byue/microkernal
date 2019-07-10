#include <user.h>
// guest_test <number_prints> <sleep_param>
int main(int argc, char* argv[]) {
  if (argc != 3) {
    aprintf(STDOUT, "Usage: guest_test <number> <sleep_param>\n");
    exit();
    return 0;
  }
  int num_prints = atoi(argv[1]);
  int sleep_param = atoi(argv[2]);
  printf(STDOUT, "Starting IPC syscall test...\n");
  for (int i = 1; i <= num_prints; i++) {
    sleep(sleep_param);
    aprintf(STDOUT, "%d Hello World!\n", i);
  }
  printf(STDOUT, "IPC syscall test successful\n");
  exit();
  return 0;
}
