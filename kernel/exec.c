#include <cdefs.h>
#include <param.h>
#include <memlayout.h>
#include <mmu.h>
#include <proc.h>
#include <defs.h>
#include <x86_64.h>
#include <elf.h>
#include <trap.h>

int
exec(char *path, char **argv)
{
  char *s, *last;
  uint64_t argc, rip, sz, sp, ustack[3+MAXARG+1];
  struct vspace newva, oldva;

  if(vspaceinit(&newva) < 0)
    goto badnofree;

  sz = vspaceloadcode(&newva, path, &rip);
  if (sz == 0)
    goto bad;

  sp = SZ_2G;
  vspaceinitstack(&newva, sp);

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~7;
    if(vspacewritetova(&newva, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*8;  // argv pointer

  myproc()->tf->rdi = argc;
  myproc()->tf->rsi = sp - (argc+1)*8;

  sp -= (3+argc+1) * 8;
  if(vspacewritetova(&newva, sp, (char *)ustack, (3+argc+1)*8) < 0)
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(myproc()->name, last, sizeof(myproc()->name));

  // Commit to the user image.
  oldva = myproc()->vspace;
  myproc()->vspace = newva;

  myproc()->vspace.regions[VR_HEAP].va_base = PGROUNDUP(sz);
  myproc()->vspace.regions[VR_HEAP].size = 0;

  myproc()->tf->rip = rip;  // main
  myproc()->tf->rsp = sp;

  vspaceinstall(myproc());
  vspacefree(&oldva);
  return 0;

 bad:
  vspacefree(&newva);
 badnofree:
  return -1;
}
