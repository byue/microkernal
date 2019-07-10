// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include <cdefs.h>
#include <defs.h>
#include <e820.h>
#include <memlayout.h>
#include <mmu.h>
#include <param.h>
#include <spinlock.h>

int npages = 0;
int pages_in_use;
int pages_in_swap;
int free_pages;

struct core_map_entry *core_map = NULL;

struct core_map_entry *pa2page(uint64_t pa) {
  if (PGNUM(pa) >= npages) {
    cprintf("%x\n", pa);
    panic("pa2page called with invalid pa");
  }
  return &core_map[PGNUM(pa)];
}

uint64_t page2pa(struct core_map_entry *pp) {
  return (pp - core_map) << PT_SHIFT;
}

// --------------------------------------------------------------
// Detect machine's physical memory setup.
// --------------------------------------------------------------

void detect_memory(void) {
  uint32_t i;
  struct e820_entry *e;
  size_t mem = 0, mem_max = -KERNBASE;

  e = e820_map.entries;
  for (i = 0; i != e820_map.nr; ++i, ++e) {
    if (e->addr >= mem_max)
      continue;
    mem = max(mem, (size_t)(e->addr + e->len));
  }

  // Limit memory to 256MB.
  mem = min(mem, mem_max);
  npages = mem / PGSIZE;
  cprintf("E820: physical memory %dMB\n", mem / 1024 / 1024);
}

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file

struct {
  struct spinlock lock;
  int use_lock;
} kmem;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void mem_init(void *vstart) {
  void *vend;

  core_map = vstart;
  memset(vstart, 0, PGROUNDUP(npages * sizeof(struct core_map_entry)));
  vstart += PGROUNDUP(npages * sizeof(struct core_map_entry));

  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;

  vend = (void *)P2V((uint64_t)(npages * PGSIZE));
  freerange(vstart, vend);
  free_pages = (vend - vstart) >> PT_SHIFT;
  pages_in_use = 0;
  pages_in_swap = 0;
  kmem.use_lock = 1;
}

void freerange(void *vstart, void *vend) {
  char *p;
  p = (char *)PGROUNDUP((uint64_t)vstart);
  for (; p + PGSIZE <= (char *)vend; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(char *v) {
  struct core_map_entry *r;

  if ((uint64_t)v % PGSIZE || v < _end || V2P(v) >= (uint64_t)(npages * PGSIZE))
    panic("kfree");

  if (kmem.use_lock)
    acquire(&kmem.lock);

  r = (struct core_map_entry *)pa2page(V2P(v));

  if (r->ref > 1) {
    r->ref--;
    if (kmem.use_lock)
      release(&kmem.lock);
    return;
  }

  pages_in_use--;
  free_pages++;

  // Fill with junk to catch dangling refs.
  memset(v, 2, PGSIZE);

  r->available = 1;
  r->user = 0;
  r->va = 0;
  r->ref = 0;
  if (kmem.use_lock)
    release(&kmem.lock);
}

void
kincref(uint64_t pa)
{
  struct core_map_entry *r = pa2page(pa);
  r->ref++;
}

void
mark_user_mem(uint64_t pa, uint64_t va)
{
  // for user mem, add an mapping to proc_info
  struct core_map_entry *r = pa2page(pa);

  r->user = 1;
  r->va = va;
}

void
mark_kernel_mem(uint64_t pa)
{
  // for user mem, add an mapping to proc_info
  struct core_map_entry *r = pa2page(pa);

  r->user = 0;
  r->va = 0;
}

char *kalloc(void) {
  pages_in_use++;
  free_pages--;

  int i;

  if (kmem.use_lock)
    acquire(&kmem.lock);

  for (i = 0; i < npages; i++) {
    if (core_map[i].available == 1) {
      core_map[i].available = 0;
      if (kmem.use_lock)
        release(&kmem.lock);

      core_map[i].ref = 1;
      return P2V(page2pa(&core_map[i]));
    }
  }

  if (kmem.use_lock)
    release(&kmem.lock);

  return 0;
}
