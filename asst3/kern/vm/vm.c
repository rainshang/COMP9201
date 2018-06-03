#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <spl.h>
#include <current.h>
#include <proc.h>
#include <synch.h>

static struct lock *page_table_lock;
static struct page_table_entry **page_table;
static size_t page_nums;


static void init_page_table()
{
	page_table = init_pagetable(&page_nums);
	if (!page_table)
	{
		panic("Initialize Page Table failed.\n");
	}

	for (size_t i = 0; i < page_nums; ++i)
	{
		page_table[i] = NULL;
	}
}

void vm_bootstrap(void)
{
	init_page_table();
	page_table_lock = lock_create("page_table_lock");
}

#define PAGE_BITS 12
static inline uint32_t hpt_hash(struct addrspace *as, vaddr_t vaddr)
{
	uint32_t hash = (((uint32_t)as) ^ (vaddr >> PAGE_BITS)) % page_nums;
	return hash;
}

static struct page_table_entry *lookup_pht(struct addrspace *as, vaddr_t vaddr, uint32_t hash)
{
	struct page_table_entry *cur = page_table[hash];
	while (cur != NULL){
		if (cur->pid == as && cur->page_vaddr == vaddr){
			return cur;
		}
		cur = cur->next;
	}
	return NULL;
}

static void update_tlb(vaddr_t faultvaddr, paddr_t frame_paddr, int dirty_mask)
{
	int spl = splhigh();
	uint32_t ehi = faultvaddr;
	uint32_t elo = frame_paddr;
	elo |= dirty_mask;
	tlb_random(ehi, elo);
	splx(spl);
}

static struct region *check_regions(struct addrspace *as, vaddr_t vaddr)
{
	struct region *cur_region = as->as_regions;
	while (cur_region)
	{
		if (vaddr >= cur_region->base_page_vaddr && vaddr < cur_region->base_page_vaddr + PAGE_SIZE * cur_region->page_nums)
		{
			return cur_region;
		}
		else
		{
			cur_region = cur_region->next_region;
		}
	}
	return NULL;
}


int vm_fault(int faulttype, vaddr_t faultvaddr)
{
	switch (faulttype)
	{
	case VM_FAULT_READONLY:
		return EFAULT;
	case VM_FAULT_READ:
	case VM_FAULT_WRITE:
		break;
	default:
		return EINVAL;
	}
	if (!curproc)
	{
		return EFAULT;
	}
	struct addrspace *as = proc_getas();
	if (!as)
	{
		return EFAULT;
	}
	faultvaddr &= PAGE_FRAME;
	uint32_t hash = hpt_hash(as, faultvaddr);
	lock_acquire(page_table_lock);
	struct page_table_entry *pte = lookup_pht(as, faultvaddr, hash);
	lock_release(page_table_lock);

	if (pte)
	{
		update_tlb(faultvaddr, pte->frame_paddr, as->dirty_mask);
		return 0;
	}
	else
	{
		struct region *region = check_regions(as, faultvaddr);
		if (!region)
		{
			return EFAULT;
		}
		vaddr_t vaddr = alloc_kpages(1);
		if (vaddr == 0){
        return ENOMEM;
    }
		bzero((void*) vaddr, PAGE_SIZE);
		struct page_table_entry *new_insert = kmalloc(sizeof(struct page_table_entry));
		if (!new_insert) {
        free_kpages(vaddr);
        return ENOMEM;
    }
		new_insert->pid = as;
		new_insert->page_vaddr = faultvaddr;
		new_insert->frame_paddr = KVADDR_TO_PADDR(vaddr) | TLBLO_VALID;
		if (region->permission & PERMISSION_WRITE){
				new_insert->frame_paddr |= TLBLO_DIRTY;
		}
		new_insert->next = NULL;

		lock_acquire(page_table_lock);
		struct page_table_entry *pe = page_table[hash];
		if (pe != NULL){
			while (pe->next != NULL) {
				pe = pe->next;
			}
			pe->next = new_insert;
		}
		else{
			page_table[hash] = new_insert;
		}
		lock_release(page_table_lock);
		update_tlb(faultvaddr, new_insert->frame_paddr, as->dirty_mask);
		return 0;
	}
}

int vm_copy(struct addrspace *old, struct addrspace *new)
{
	size_t i;
	lock_acquire(page_table_lock);
  for(i = 0; i < page_nums; i++){
		struct page_table_entry *cur = page_table[i];

				if(cur != NULL && cur->pid == old){
					struct page_table_entry *new_pte = kmalloc(sizeof(struct page_table_entry));
					if(!new_pte){
								lock_release(page_table_lock);
								return ENOMEM;
						}
						vaddr_t vaddr = alloc_kpages(1);
						memcpy((void *)(vaddr), (const void *)PADDR_TO_KVADDR(cur->frame_paddr & PAGE_FRAME), PAGE_SIZE);
						new_pte->frame_paddr = KVADDR_TO_PADDR(vaddr) | (cur->frame_paddr & ~PAGE_FRAME);
						new_pte->page_vaddr = cur->page_vaddr;
						new_pte->pid = new;
						new_pte->next = NULL;

						int hash = hpt_hash(new, new_pte->page_vaddr);
						struct page_table_entry *pe = page_table[hash];
						if (pe != NULL){
							while (pe->next != NULL) {
								pe = pe->next;
							}
							pe->next = new_pte;
						}
						else{
							page_table[hash] = new_pte;
						}
					}
	}
	lock_release(page_table_lock);
	return 0;
}

void vm_destroy(struct addrspace *as)
{
		size_t i;
		lock_acquire(page_table_lock);
		struct page_table_entry *c_pe, *n_pe, *t_pe;
		for (i = 0; i < page_nums; i++){
			n_pe = c_pe = page_table[i];
			while (c_pe != NULL) {
					if (c_pe->pid == as){
						if (page_table[i] == c_pe){
							t_pe = page_table[i] = n_pe = c_pe->next;
						}
						else{
							t_pe = n_pe->next = c_pe->next;
						}
						free_kpages(PADDR_TO_KVADDR(c_pe->frame_paddr));
            kfree(c_pe);
            c_pe = t_pe;
					}
					else{
						n_pe = c_pe;
            c_pe = c_pe->next;
					}
			}
		}
		lock_release(page_table_lock);
}

/*
 *
 * SMP-specific functions.  Unused in our configuration.
 */

void vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("vm tried to do tlb shootdown?!\n");
}
