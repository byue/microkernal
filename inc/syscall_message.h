#pragma once

#include <sysinfo.h>
#include <stat.h>
#include <file.h>

#define SHELL_PID 3
#define GUEST_PID 2

// memory settings for guest os
#define MAX_PHYS_PAGES 5000
#define DEFAULT_USER_PAGES 2000

// memory macros
#define APP_HEAP 3
#define APP_STACK 4

#define MAX_PROC 64

#define MAX_GUEST_SYSCALLS 100
#define MESSAGE_WRITE 1 // index in guest os syscall table
#define MESSAGE_INIT_APP 2
#define MESSAGE_DESTROY_APP 3

#define MAX_ARGS 6  // including argc 
#define MAX_STRING_SIZE 64 // buggy when size is too big, leave as 64

// argument types in struct arg
#define CHAR_TYPE 1
#define INT_TYPE 2
#define DOUBLE_TYPE 3
#define FLOAT_TYPE 4
#define LONG_TYPE 5
#define VOID_PTR_TYPE 6
#define INT_PTR_TYPE 7
#define CHAR_PTR_TYPE 8
#define CHAR_PTR_PTR_TYPE 9
#define STRUCT_FILE_PTR_TYPE 10
#define STRUCT_STAT_PTR_TYPE 11
#define STRUCT_SYS_INFO_PTR_TYPE 12
#define STRING_TYPE 13

union val {
  // primitives
  char c;
  char string[MAX_STRING_SIZE];
  int i;
  double d;
  float fl;
  long l;
  // primitive pointers
  void *void_ptr;
  int *int_ptr;
  char *char_ptr;
  char **char_ptr_ptr;
  // struct pointers
  struct file *f;
  struct stat *stat_ptr;
  struct sys_info *sys_info_ptr;
};

struct arg {
  // arg type defined by macros
  int arg_type;
  union val arg_val;
};

struct syscall_message {
  int pid;
  int syscall_index;
  int num_args;
  struct arg args[MAX_ARGS];
  struct syscall_message *next_message;
  char buffer[512];  // buffer for holding char data.
};
