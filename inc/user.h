#pragma once
#include <cdefs.h>
#include <syscall_message.h>
#include <guest_space.h>

#define STDOUT 1

struct stat;
struct rtcdate;
struct sys_info;
struct syscall_message;
struct app_va_segment;

// system calls
int fork(void);
noreturn int exit(void);
int wait(void);
int pipe(int *);
int write(int, void *, int);
int read(int, void *, int);
int close(int);
int kill(int);
int exec(char *, char **);
int open(char *, int);
int mknod(char *, short, short);
int unlink(char *);
int fstat(int fd, struct stat *);
int link(char *, char *);
int mkdir(char *);
int chdir(char *);
int dup(int);
int getpid(void);
char *sbrk(int);
int sleep(int);
int uptime(void);
int sysinfo(struct sys_info *);
int crashn(int);

// general syscall for user libraries to use
int app_syscall(int, struct arg*);

// privileged system calls
int gnum_children(void);
int gnext_syscall(struct syscall_message *);
int gresume(int);
int gquery_user_pages(uint8_t *);
int grequest_proc(struct app_va_segment *, uint64_t, uint64_t, uint64_t);
int gload_program(int, char *);
int gdeploy_program(struct syscall_message *);
int gaddmap(int app_pid, int host_ppn, uint64_t va, int app_present, int app_writeable);
int gremovemap(int app_pid, uint64_t va);
int gupdate_flags(int app_pid, uint64_t va, int app_present, int app_writeable);

// for starting guest os from shell
int fork_guest(int);

// ulib.c
int stat(char *, struct stat *);
char *strcpy(char *, char *);
void *memmove(void *, void *, int);
char *strchr(const char *, char c);
int strcmp(const char *, const char *);
void printf(int, char *, ...);
void aprintf(int, char *, ...);
char *gets(char *, int max);
uint strlen(char *);
void *memset(void *, int, uint);
void *malloc(uint);
void free(void *);
int atoi(const char *);
