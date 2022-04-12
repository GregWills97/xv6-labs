// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);
void kfree_cpu(void *pa, int cpu_id);
void take_pages(int cpu_dest, int cpu_src, int n);
int  has_pages(int cpu, int n);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  char lock_name[8];
  //initialize all locks for CPU
  for(int i = 0; i < NCPU; i++) {
    snprintf(lock_name, 8, "kmem%d", i); 
    initlock(&kmem[i].lock, "kmem");
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  int i;
  char *p;

  i = 0;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    // Free initial pages balanced between all CPUs
    kfree_cpu(p, (i%NCPU));
    i++;
  }
}

//Version of kfree that takes a cpu id
void
kfree_cpu(void *pa, int cpu_id)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree_cpu");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem[cpu_id].lock);
  r->next = kmem[cpu_id].freelist;
  kmem[cpu_id].freelist = r;
  release(&kmem[cpu_id].lock);
}


// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  int cpu_id;
  struct run *r;

  //Turn off interrrupts get id
  push_off();
  cpu_id = cpuid();

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem[cpu_id].lock);
  r->next = kmem[cpu_id].freelist;
  kmem[cpu_id].freelist = r;
  release(&kmem[cpu_id].lock);

  //Turn interrupts back on 
  pop_off();
}

// Returns 1 if the CPU has n pages
int has_pages(int cpu, int n) {
  int count;
  struct run *r;

  acquire(&kmem[cpu].lock);
  r = kmem[cpu].freelist;

  count = 0;

  while(r) {
    r = r->next;
    count++;
  }
  release(&kmem[cpu].lock);

  return (count == n) ? 1 : 0;
}

// Steal n pages from cpu_src to cpu_dst
void steal_pages(int cpu_dest, int cpu_src, int n) {
  struct run *r;

  if(cpu_dest == cpu_src)
    return;

  // Acquire locks in order
  acquire(&kmem[cpu_dest].lock);
  acquire(&kmem[cpu_src].lock);

  //steal n pages
  while(n > 0) {
    //get top of src freelist
    r = kmem[cpu_src].freelist;

    //Remove src page from list
    kmem[cpu_src].freelist = r->next;

    //Put src free page on top of dest free list
    r->next = kmem[cpu_dest].freelist;
    kmem[cpu_dest].freelist = r;
    n--;
  }

  //Release in order
  release(&kmem[cpu_src].lock);
  release(&kmem[cpu_dest].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  int cpu_id;
  struct run *r;

  //Turn off interrrupts get id
  push_off();
  cpu_id = cpuid();

  acquire(&kmem[cpu_id].lock);
  r = kmem[cpu_id].freelist;
  
  //Free page available
  if(r)
    kmem[cpu_id].freelist = r->next;
  release(&kmem[cpu_id].lock);

  //Page not found look in another CPUs pool
  if(!r) {
    for(int i = 0; i < NCPU; i++) {
      int next_neighbor = (cpu_id + i + 1) % NCPU;


      /* Multiple pages at a time */
      ///*
      if(has_pages(next_neighbor, 1)) {
        steal_pages(cpu_id, next_neighbor, 1);
      }
      
      //Pages should be in freelist now
      acquire(&kmem[cpu_id].lock);
      r = kmem[cpu_id].freelist;
      if(r)
        kmem[cpu_id].freelist = r->next;
      release(&kmem[cpu_id].lock);
      //*/

      
      /* Single Page taken Logic */
      /*
      acquire(&kmem[next_neighbor].lock);
      r = kmem[next_neighbor].freelist;
      if(r)
        kmem[next_neighbor].freelist = r->next;
      release(&kmem[next_neighbor].lock);
      */
      
      if(r)
        break; //stop searching
    }
  }

  if(r)
      memset((char*)r, 5, PGSIZE); //fill with junk
  
  //turn back on interrupts
  pop_off();

  return (void*)r;
}
