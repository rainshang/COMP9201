/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *        The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 *
 * UNSW: If you use ASST3 config as required, then this file forms
 * part of the VM subsystem.
 *
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL)
	{
		return NULL;
	}
	as->as_regions = kmalloc(sizeof(struct addrspace));
	if (as->as_regions == NULL)
	{
		kfree(as);
		return NULL;
	}
	as->as_regions->start_page = 0;
	as->as_regions->count_page = 0;
	as->as_regions->permission = 0;
	as->as_regions->next_region = NULL;

	return as;
}

int as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas == NULL)
	{
		return ENOMEM;
	}

  struct region *old_temp = old->as_regions;

  while (old_temp != NULL){
      as_define_region(newas, old_temp->start_page, old_temp->count_page,
        old_temp->permission & PERMISSION_READ, old_temp->permission & PERMISSION_WRITE,
        old_temp->permission & PERMISSION_EXECUTE);

      old_temp = old_temp->next_region;
      
  }



	(void)old;

	*ret = newas;
	return 0;
}

void as_destroy(struct addrspace *as)
{
	/*
         * Clean up as needed.
         */

	kfree(as);
}

void as_activate(void)
{
	int i, spl;
	struct addrspace *as;
	as = proc_getas();
	if (as == NULL)
	{
		return;
	}
	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i = 0; i < NUM_TLB; i++)
	{
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void as_deactivate(void)
{
	as_activate();
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
					 int readable, int writeable, int executable)
{
	struct region *new_region = kmalloc(sizeof(struct region));
	if (!new_region)
	{
		return ENOMEM;
	}

	new_region->start_page = vaddr & PAGE_FRAME;
	new_region->count_page = (memsize + vaddr % PAGE_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
	new_region->permission = readable | writeable | executable;
	new_region->next_region = NULL;

	if (as->as_regions)
	{
		struct region *iterator_region = as->as_regions;
		while (iterator_region->next_region)
		{
			iterator_region = iterator_region->next_region;
		}
		iterator_region->next_region = new_region;
	}
	else
	{
		as->as_regions = new_region;
	}
	return 0;
}

int as_prepare_load(struct addrspace *as)
{
	struct region *regions = as->as_regions;
	while (regions != NULL)
	{
		regions->permission = regions->permission | PERMISSION_WRITE;
		regions = regions->next_region;
	}
	return 0;
}

int as_complete_load(struct addrspace *as)
{
	struct region *regions = as->as_regions;
	while (regions != NULL)
	{
		regions->permission = regions->permission | PERMISSION_READ;
		regions = regions->next_region;
	}
	return 0;
}

#define USER_STACKPAGES 16

int as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	*stackptr = USERSTACK;
	return as_define_region(as, USERSTACK - PAGE_SIZE * USER_STACKPAGES, PAGE_SIZE * USER_STACKPAGES, PERMISSION_READ, PERMISSION_WRITE, 0);
}
