#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>

/* Place your frametable data-structures here
 * You probably also want to write a frametable initialisation
 * function and call it from vm_bootstrap
 */

 struct frame_table_entry{
    int isUsed;
    int next_empty_entry;
 };

paddr_t helper(unsigned int npages);

struct frame_table_entry *frame_table = 0;
unsigned page_nums;
unsigned first_empty_entry;
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

/* Note that this function returns a VIRTUAL address, not a physical
 * address
 * WARNING: this function gets called very early, before
 * vm_bootstrap().  You may wish to modify main.c to call your
 * frame table initialisation function, or check to see if the
 * frame table has been initialised and call ram_stealmem() otherwise.
 */
void init_frametable(void){
      paddr_t top_of_ram = ram_getsize();
      paddr_t bottom_of_ram = ram_getfirstfree();
      paddr_t location = top_of_ram - (top_of_ram / PAGE_SIZE * sizeof(struct frame_table_entry));
      frame_table = (struct frame_table_entry *)PADDR_TO_KVADDR(location);
      page_nums = top_of_ram / PAGE_SIZE;
      unsigned bot = bottom_of_ram / PAGE_SIZE;
      unsigned top = location / PAGE_SIZE;

      spinlock_acquire(&stealmem_lock);
      for(unsigned i=0; i<page_nums; i++){
          if (i < bot || i > top ){
            frame_table[i].isUsed = 1;
          }
          else{
            frame_table[i].isUsed = 0;
            if (i != page_nums-1){
              frame_table[i].next_empty_entry = i+1;
            }
            else{
              frame_table[i].next_empty_entry = -1;
            }
          }
      }
      for(unsigned i=0; i<page_nums; i++){
        if (frame_table[i].isUsed == 0){
          first_empty_entry = i;
          break;
        }
      }
      spinlock_release(&stealmem_lock);
}

vaddr_t alloc_kpages(unsigned int npages)
{
        paddr_t addr;
        if (frame_table == 0){
          spinlock_acquire(&stealmem_lock);
          addr = ram_stealmem(npages);
          spinlock_release(&stealmem_lock);
        }
        else{
          addr = helper(npages);
        }

        if(addr == 0){
          return 0;
        }
        return PADDR_TO_KVADDR(addr);
}

paddr_t helper(unsigned int npages)
{
    paddr_t addr = 0;
    spinlock_acquire(&stealmem_lock);
    if(npages == 1)
    {
      if (first_empty_entry >= page_nums){
          spinlock_release(&stealmem_lock);
          kprintf("error frame number.\n");
          return 0;
      }
      addr = first_empty_entry * PAGE_SIZE;
      frame_table[first_empty_entry].isUsed = 1;
      first_empty_entry = frame_table[first_empty_entry].next_empty_entry;
    }
    spinlock_release(&stealmem_lock);
    return addr;
}

void free_kpages(vaddr_t addr)
{
      paddr_t paddr = KVADDR_TO_PADDR(addr);
      unsigned temp = paddr / PAGE_SIZE;
      if (temp > page_nums){
        return;
      }
      if (frame_table[temp].isUsed == 0){
        return;
      }
      free_kpage(addr);
      spinlock_acquire(&stealmem_lock);
      frame_table[temp].isUsed = 0;
      frame_table[temp].next_empty_entry = first_empty_entry;
      first_empty_entry = temp;
      spinlock_release(&stealmem_lock);
}
