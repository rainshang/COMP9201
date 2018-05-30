#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>

struct frame_table_entry
{
	unsigned char is_used;
	size_t next_empty_frame_index;
};

static struct spinlock ft_lock = SPINLOCK_INITIALIZER;
static struct frame_table_entry *frame_table = NULL;
static size_t frame_nums;

static size_t current_empty_frame_index;

static inline paddr_t get_frame_paddr(size_t index)
{
	return index * PAGE_SIZE;
}

struct page_table_entry *init_pagetable(size_t *page_nums)
{
	paddr_t top_of_ram = ram_getsize();
	paddr_t ft_base = ram_getfirstfree();

	// allocate space for frame table
	frame_table = (struct frame_table_entry *)PADDR_TO_KVADDR(ft_base);
	frame_nums = top_of_ram / PAGE_SIZE;

	// calculate available frame after creating frame table
	size_t ft_size = sizeof(struct frame_table_entry) * frame_nums;
	current_empty_frame_index = (ft_base + ft_size + PAGE_SIZE - 1) / PAGE_SIZE;

	// allocate space for page table
	struct page_table_entry *page_table = (struct page_table_entry *)PADDR_TO_KVADDR(get_frame_paddr(current_empty_frame_index));

	// calculate available frame after creating page table
	*page_nums = frame_nums * 2;
	size_t pt_size = sizeof(struct page_table_entry) * *page_nums;
	current_empty_frame_index += (pt_size + PAGE_SIZE - 1) / PAGE_SIZE;
	if (current_empty_frame_index >= frame_nums)
	{
		return NULL;
	}

	for (size_t i = 0; i < frame_nums; ++i)
	{
		struct frame_table_entry *fte = &frame_table[i];

		if (i < current_empty_frame_index)
		{
			fte->is_used = 1;
		}
		else
		{
			fte->is_used = 0;
			fte->next_empty_frame_index = i + 1;
		}
	}

	return page_table;
}

/* Note that this function returns a VIRTUAL address, not a physical
 * address
 * WARNING: this function gets called very early, before
 * vm_bootstrap().  You may wish to modify main.c to call your
 * frame table initialisation function, or check to see if the
 * frame table has been initialised and call ram_stealmem() otherwise.
 */
vaddr_t alloc_kpages(unsigned int npages)
{
	paddr_t addr = 0;
	spinlock_acquire(&ft_lock);
	if (frame_table)
	{
		if (npages == 1)
		{
			if (current_empty_frame_index >= frame_nums)
			{
				spinlock_release(&ft_lock);
				kprintf("error frame number.\n");
				return 0;
			}
			addr = get_frame_paddr(current_empty_frame_index);

			struct frame_table_entry *fte = &frame_table[current_empty_frame_index];
			fte->is_used = 1;
			current_empty_frame_index = fte->next_empty_frame_index;
		}
	}
	else
	{
		addr = ram_stealmem(npages);
	}
	spinlock_release(&ft_lock);

	if (addr)
	{
		return PADDR_TO_KVADDR(addr);
	}
	else
	{
		return 0;
	}
}

void free_kpages(vaddr_t vaddr)
{
	paddr_t paddr = KVADDR_TO_PADDR(vaddr);
	size_t frame_index = paddr / PAGE_SIZE;
	if (frame_index >= frame_nums)
	{
		return;
	}
	spinlock_acquire(&ft_lock);
	struct frame_table_entry *fte = &frame_table[frame_index];
	if (fte->is_used)
	{
		fte->is_used = 0;
		fte->next_empty_frame_index = current_empty_frame_index;
		current_empty_frame_index = frame_index;
	}
	spinlock_release(&ft_lock);
}