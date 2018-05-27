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

#define PAGE_BITS 12
static struct page_table_entry *hashed_page_table;
static unsigned hpt_size;

static void init_page_table()
{
	paddr_t mm_size = ram_getsize();
	hpt_size = mm_size / PAGE_SIZE * 2;
	unsigned n_pages_hpt = sizeof(struct page_table_entry) * hpt_size / PAGE_SIZE;
	hashed_page_table = (struct page_table_entry *)PADDR_TO_KVADDR(ram_stealmem(n_pages_hpt));

	if (hashed_page_table == NULL)
	{
		panic("Initialize Page Table failed.\n");
	}

	for (size_t i = 0; i < hpt_size; ++i)
	{
		struct page_table_entry *pte = &hashed_page_table[i];

		pte->frame_addr = 0;
		pte->page_addr = 0;
		pte->pid = NULL;
		pte->next_hash_index = 0;
	}
}

void vm_bootstrap(void)
{
	init_page_table();
	init_frametable();
}

static uint32_t hpt_hash(struct addrspace *as, vaddr_t faultvaddr)
{
	uint32_t index;

	index = (((uint32_t)as) ^ (faultvaddr >> PAGE_BITS)) % hpt_size;
	return index;
}

static vaddr_t get_page_addr(vaddr_t faultvaddr)
{
	return faultvaddr & PAGE_FRAME;
}

static void update_tlb(vaddr_t faultvaddr, paddr_t frame_addr)
{
	int spl = splhigh();
	uint32_t ehi = faultvaddr & TLBHI_VPAGE;
	uint32_t elo = frame_addr | TLBLO_DIRTY | TLBLO_VALID;

	tlb_random(ehi, elo);
	splx(spl);
}

static int lookup_pht_update_tlb(struct addrspace *as, vaddr_t faultvaddr, struct page_table_entry pte)
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
			return 0;
		}
		else if (pte.next_hash_index != 0) // hash collision solution
		{
			struct page_table_entry next_pte = hashed_page_table[pte.next_hash_index];
			return lookup_pht_update_tlb(as, faultvaddr, next_pte);
		}
		else // miss
		{
			return -1;
		}
	}
}

static int check_regions(struct addrspace *as, vaddr_t faultvaddr)
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

static int alloc_frame(struct page_table_entry *pte, struct addrspace *as, vaddr_t faultvaddr, unsigned char is_copy_or_new)
{
	vaddr_t new_frame_vaddr = alloc_kpages(1);
	if (new_frame_vaddr == 0)
	{
		return ENOMEM;
	}
	if (is_copy_or_new)
	{
		memcpy((void *)new_frame_vaddr, (const void *)faultvaddr, PAGE_SIZE);
	}
	else
	{
		bzero((void *)new_frame_vaddr, PAGE_SIZE);
	}
	pte->frame_addr = KVADDR_TO_PADDR(new_frame_vaddr);
	pte->page_addr = get_page_addr(faultvaddr);
	pte->pid = as;
	return 0;
}

static int insert_pht(struct addrspace *as, vaddr_t faultvaddr, uint32_t index, unsigned char is_copy_or_new)
{
	struct page_table_entry *pte = &hashed_page_table[index];
	int err = 0;
	if (pte->pid == NULL)
	{
		err = alloc_frame(pte, as, faultvaddr, is_copy_or_new);
		if (err)
		{
			return err;
		}
		if (!is_copy_or_new)
		{
			update_tlb(faultvaddr, pte->frame_addr);
		}
		return 0;
	}
	else
	{
		uint32_t di = 1;
		while (di < hpt_size)
		{
			uint32_t new_index = (index + di) % hpt_size;
			struct page_table_entry *next_pte = &hashed_page_table[new_index];
			if (next_pte->pid == NULL)
			{
				err = alloc_frame(next_pte, as, faultvaddr, is_copy_or_new);
				if (err)
				{
					return err;
				}
				pte->next_hash_index = new_index;
				if (!is_copy_or_new)
				{
					update_tlb(faultvaddr, next_pte->frame_addr);
				}
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

	struct spinlock spinlock = SPINLOCK_INITIALIZER;
	spinlock_acquire(&spinlock);

	uint32_t index = hpt_hash(as, faultvaddr);
	struct page_table_entry pte = hashed_page_table[index];

	int err = lookup_pht_update_tlb(as, faultvaddr, pte);

	spinlock_release(&spinlock);
	if (err)
	{
		err = check_regions(as, faultvaddr);
		if (err)
		{
			return EFAULT;
		}
		else
		{
			spinlock_acquire(&spinlock);
			err = insert_pht(as, faultvaddr, index, 0);
			spinlock_release(&spinlock);
			return err;
		}
	}
	return 0;
}

static void lookup_pht_delete_page(vaddr_t page_vaddr, struct page_table_entry *pte)
{
	if (pte->pid != NULL)
	{
		if (pte->next_hash_index != 0)
		{
			struct page_table_entry *next_pte = &hashed_page_table[pte->next_hash_index];
			lookup_pht_delete_page(page_vaddr, next_pte);
		}
		if (pte->page_addr == page_vaddr) // hit
		{
			pte->frame_addr = 0;
			pte->page_addr = 0;
			pte->pid = NULL;
			pte->next_hash_index = 0;
		}
	}
}

void free_kpage(vaddr_t page_vaddr)
{
	if (curproc == NULL)
	{
		return;
	}
	struct addrspace *as = proc_getas();
	if (as == NULL)
	{
		return;
	}

	struct spinlock spinlock = SPINLOCK_INITIALIZER;
	spinlock_acquire(&spinlock);

	uint32_t index = hpt_hash(as, page_vaddr);
	struct page_table_entry *pte = &hashed_page_table[index];

	lookup_pht_delete_page(page_vaddr, pte);

	spinlock_release(&spinlock);
}

static int pte_copy(struct addrspace *old, struct addrspace *new, vaddr_t old_page_vaddr, struct page_table_entry old_pte)
{
	if (old_pte.pid == old && old_pte.page_addr == old_page_vaddr) // hit
	{
		uint32_t index = hpt_hash(new, old_page_vaddr);
		return insert_pht(new, old_page_vaddr, index, 1);
	}
	else if (old_pte.next_hash_index != 0) // hash collision solution
	{
		struct page_table_entry next_old_pte = hashed_page_table[old_pte.next_hash_index];
		return pte_copy(old, new, old_page_vaddr, next_old_pte);
	}
	else
	{
		return 0;
	}
}

int vm_copy(struct addrspace *old, struct addrspace *new)
{
	struct spinlock spinlock = SPINLOCK_INITIALIZER;
	spinlock_acquire(&spinlock);

	struct region *old_region = old->as_regions;
	while (old_region)
	{
		vaddr_t page_vaddr = old_region->start_page;
		for (unsigned i = 0; i < old_region->count_page; ++i)
		{
			page_vaddr += PAGE_SIZE * i;
			uint32_t index = hpt_hash(old, page_vaddr);
			struct page_table_entry pte = hashed_page_table[index];
			int err = pte_copy(old, new, page_vaddr, pte);
			if (err)
			{
				spinlock_release(&spinlock);
				return err;
			}
		}
		old_region = old_region->next_region;
	}

	spinlock_release(&spinlock);
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
