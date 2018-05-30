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

static struct spinlock hpt_lock = SPINLOCK_INITIALIZER;
static struct page_table_entry *hashed_page_table;
static size_t page_nums;

static void init_pte(struct page_table_entry *pte)
{
	pte->pid = NULL;
	pte->page_vaddr = 0;
	pte->frame_paddr = 0;
	pte->next_hash = 0;
}

static void init_page_table()
{
	hashed_page_table = init_pagetable(&page_nums);
	if (!hashed_page_table)
	{
		panic("Initialize Page Table failed.\n");
	}

	for (size_t i = 0; i < page_nums; ++i)
	{
		struct page_table_entry *pte = &hashed_page_table[i];
		init_pte(pte);
	}
}

void vm_bootstrap(void)
{
	init_page_table();
}

#define PAGE_BITS 12
static inline uint32_t hpt_hash(struct addrspace *as, vaddr_t vaddr)
{
	uint32_t hash = (((uint32_t)as) ^ (vaddr >> PAGE_BITS)) % page_nums;
	return hash;
}

static inline vaddr_t get_page_vaddr(vaddr_t vaddr)
{
	return vaddr & PAGE_FRAME;
}

static struct page_table_entry *lookup_pht(struct addrspace *as, vaddr_t vaddr)
{
	uint32_t hash = hpt_hash(as, vaddr);
	struct page_table_entry *pte = &hashed_page_table[hash];

	while (pte->pid)
	{
		if (pte->pid == as && pte->page_vaddr == get_page_vaddr(vaddr)) // hit
		{
			return pte;
		}
		else if (pte->next_hash) // hash collision solution
		{
			pte = &hashed_page_table[pte->next_hash];
		}
		else
		{
			return NULL;
		}
	}

	return NULL;
}

static void update_tlb(vaddr_t faultvaddr, paddr_t *frame_paddr, struct region *region)
{
	int spl = splhigh();
	uint32_t ehi = faultvaddr & TLBHI_VPAGE;
	uint32_t elo = *frame_paddr;
	if (region)
	{
		elo |= TLBLO_VALID;
		if (region->permission & PERMISSION_WRITE)
		{
			elo |= TLBLO_DIRTY;
		}
		*frame_paddr = elo;
	}
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

static int insert_pht(struct addrspace *as, vaddr_t vaddr, struct page_table_entry **ret_pte)
{
	uint32_t hash = hpt_hash(as, vaddr);
	struct page_table_entry *pte = &hashed_page_table[hash];

	uint32_t di = 1;
	while (pte->pid && di < page_nums)
	{
		pte = &hashed_page_table[(hash + di) % page_nums];
		++di;
	}

	if (pte->pid)
	{
		return ENOMEM;
	}
	else
	{
		vaddr_t new_frame_vaddr = alloc_kpages(1);
		if (!new_frame_vaddr)
		{
			return ENOMEM;
		}
		pte->pid = as;
		pte->page_vaddr = get_page_vaddr(vaddr);
		pte->frame_paddr = KVADDR_TO_PADDR(new_frame_vaddr);
		*ret_pte = pte;
		return 0;
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

	spinlock_acquire(&hpt_lock);
	struct page_table_entry *pte = lookup_pht(as, faultvaddr);
	spinlock_release(&hpt_lock);

	if (pte)
	{
		update_tlb(faultvaddr, &pte->frame_paddr, NULL);
		return 0;
	}
	else
	{
		struct region *region = check_regions(as, faultvaddr);
		if (!region)
		{
			return EFAULT;
		}
		spinlock_acquire(&hpt_lock);
		int err = insert_pht(as, faultvaddr, &pte);
		spinlock_release(&hpt_lock);
		if (err)
		{
			return err;
		}
		bzero((void *)PADDR_TO_KVADDR(pte->frame_paddr), PAGE_SIZE);
		update_tlb(faultvaddr, &pte->frame_paddr, region);
		return 0;
	}
}

int vm_copy(struct addrspace *old, struct addrspace *new)
{
	spinlock_acquire(&hpt_lock);

	struct region *old_region = old->as_regions;
	while (old_region)
	{
		vaddr_t page_vaddr = old_region->base_page_vaddr;
		for (size_t i = 0; i < old_region->page_nums; ++i)
		{
			page_vaddr += PAGE_SIZE * i;

			struct page_table_entry *old_pte = lookup_pht(old, page_vaddr);
			if (old_pte)
			{
				struct page_table_entry *new_pte;
				int err = insert_pht(new, page_vaddr, &new_pte);
				if (err)
				{
					spinlock_release(&hpt_lock);
					return err;
				}
				memcpy((void *)PADDR_TO_KVADDR(new_pte->frame_paddr), (const void *)PADDR_TO_KVADDR(old_pte->frame_paddr & PAGE_FRAME), PAGE_SIZE);
				new_pte->frame_paddr |= (old_pte->frame_paddr & ~PAGE_FRAME);
			}
		}
		old_region = old_region->next_region;
	}

	spinlock_release(&hpt_lock);
	return 0;
}

void vm_destroy(struct addrspace *as)
{
	spinlock_acquire(&hpt_lock);

	for (size_t i = 0; i < page_nums; ++i)
	{
		struct page_table_entry *pte = &hashed_page_table[i];
		if (pte->pid == as)
		{
			if (pte->next_hash)
			{
				struct page_table_entry *next_pte = &hashed_page_table[pte->next_hash];
				if (next_pte->pid == as)
				{
					init_pte(pte);
				}
				else
				{
					pte->pid = next_pte->pid;
					pte->page_vaddr = next_pte->page_vaddr;
					pte->frame_paddr = next_pte->frame_paddr;
					pte->next_hash = next_pte->next_hash;

					init_pte(next_pte);
				}
			}
			else
			{
				init_pte(pte);
			}
		}
	}

	spinlock_release(&hpt_lock);
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
