#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>

#define PAGE_BITS 12
static struct page_table_entry *hashed_page_table;
static unsigned hpt_size;

void init_page_table()
{
	paddr_t mm_size = ram_getsize();
	hpt_size = mm_size / PAGE_SIZE * 2;
	unsigned n_pages_hpt = sizeof(struct page_table_entry) * hpt_size / PAGE_SIZE;
	hashed_page_table = (struct page_table_entry *)PADDR_TO_KVADDR(ram_stealmem(n_pages_hpt));

	if (hashed_page_table == NULL)
	{
		panic("Initialize Page Table failed.\n")
	}

	for (size_t i = 0; i < hpt_size; ++i)
	{
		struct page_table_entry pte = hashed_page_table[i];

		pte.frame_addr = 0;
		pte.page_addr = 0;
		pte.pid = NULL;
		pte.next_hash_index = 0;
	}
}

void vm_bootstrap(void)
{
	init_page_table();
}

uint32_t hpt_hash(struct addrspace *as, vaddr_t faultvaddr)
{
	uint32_t index;

	index = (((uint32_t)as) ^ (faultvaddr >> PAGE_BITS)) % hpt_size;
	return index;
}

vaddr_t get_page_addr(vaddr_t faultvaddr)
{
	return faultvaddr - faultvaddr % PAGE_SIZE;
}

void update_tlb(vaddr_t faultvaddr, paddr_t frame_addr)
{
	int spl = splhigh();
	uint32_t ehi = faultvaddr & TLBHI_VPAGE;
	uint32_t elo = frame_addr | TLBLO_DIRTY | TLBLO_VALID;

	tlb_random(ehi, elo);
	splx(spl);
}

int lookup_pht(struct addrspace *as, vaddr_t faultvaddr, struct page_table_entry pte)
{
	if (pte.pid == NULL)
	{
		return -1;
	}
	else
	{
		if (pte.pid == as && pte.page_addr == get_page_addr(faultvaddr)) // hit
		{
			update_tlb(faultvaddr, pte.frame_addr);
			retunr 0;
		}
		else if (pte.next_hash_index != 0) // hash collision solution
		{
			struct page_table_entry next_pte = hashed_page_table[pte.next_hash_index];
			return lookup_pht(as, faultvaddr, next_pte);
		}
		else // miss
		{
			return -1;
		}
	}
}

int check_regions(struct addrspace *as, vaddr_t faultvaddr)
{
	struct region *cur_region = as->as_regions;

	while (cur_region != NULL)
	{
		if (faultvaddr >= cur_region->start_page && faultvaddr <= cur_region->start_page + PAGE_SIZE * cur_region->count_page)
		{
			return 0;
		}
		else
		{
			cur_region = cur_region->next_region;
		}
	}

	return -1;
}

int alloc_frame(struct page_table_entry pte, struct addrspace *as, vaddr_t faultvaddr)
{
	vaddr_t new_frame_vaddr = alloc_kpages(1);
	if (vaddr == 0)
	{
		return ENOMEM;
	}
	bzero(vaddr, PAGE_SIZE);
	pte.frame_addr = KVADDR_TO_PADDR(new_frame_vaddr);
	pte.page_addr = get_page_addr(faultvaddr);
	pte.pid = as;
	return 0;
}

int insert_pht(struct addrspace *as, vaddr_t faultvaddr, uint32_t index)
{
	struct page_table_entry pte = hashed_page_table[index];
	int err = 0;
	if (pte.pid == NULL)
	{
		err = alloc_frame(pte, as, faultvaddr);
		if (err)
		{
			return err;
		}
		update_tlb(faultvaddr, pte.frame_addr);
		return 0;
	}
	else
	{
		uint32_t di = 1;
		while (di < hpt_size)
		{
			uint32_t new_index = (index + di) % hpt_size;
			struct page_table_entry next_pte = hashed_page_table[new_index];
			if (next_pte.pid == NULL)
			{
				err = alloc_frame(next_pte, as, faultvaddr);
				if (err)
				{
					return err;
				}
				pte.next_hash_index = new_index;
				update_tlb(faultvaddr, next_pte.frame_addr);
				return 0;
			}
			else
			{
				++di;
			}
		}
		return ENOMEM;
	}
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
	if (curproc == NULL)
	{
		return EFAULT;
	}
	struct addrspace *as = proc_getas();
	if (as == NULL)
	{
		return EFAULT;
	}

	struct spinlock *spinlock = &SPINLOCK_INITIALIZER;
	spinlock_acquire(spinlock);

	uint32_t index = hpt_hash(as, faultvaddr);
	struct page_table_entry pte = hashed_page_table[index];

	int err = lookup_pht(as, faultvaddr, pte);

	spinlock_release(spinlock);
	if (err)
	{
		err = check_regions(as, faultvaddr);
		if (err)
		{
			return EFAULT;
		}
		else
		{
			spinlock_acquire(spinlock);
			err = insert_pht(as, faultvaddr, index);
			spinlock_release(spinlock);
			return err;
		}
	}
	return 0;
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
