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
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (!as)
	{
		return NULL;
	}
	as->as_regions = NULL;
	return as;
}

int as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new = as_create();
	if (!new)
	{
		return ENOMEM;
	}

	struct region *old_region = old->as_regions;

	while (old_region)
	{
		as_define_region(new,
						 old_region->base_page_vaddr,
						 PAGE_SIZE * old_region->page_nums,
						 old_region->permission & PERMISSION_READ,
						 old_region->permission & PERMISSION_WRITE,
						 old_region->permission & PERMISSION_EXECUTE);
		old_region = old_region->next_region;
	}
	*ret = new;
	int err = vm_copy(old, new);
	if (err)
	{
		as_destroy(new);
	}
	return err;
}

void as_destroy(struct addrspace *as)
{
	vm_destroy(as);
	struct region *region = as->as_regions;
	while (region)
	{
		struct region *tmp = region;
		region = region->next_region;
		kfree(tmp);
	}

	kfree(as);
}

void as_activate(void)
{
	struct addrspace *as = proc_getas();
	if (!as)
	{
		return;
	}
	/* Disable interrupts on this CPU while frobbing the TLB. */
	int spl = splhigh();

	for (int i = 0; i < NUM_TLB; i++)
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

	new_region->base_page_vaddr = vaddr & PAGE_FRAME;
	new_region->page_nums = (memsize + vaddr % PAGE_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
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
	struct region *region = as->as_regions;
	while (region)
	{
		region->permission = region->permission | PERMISSION_WRITE;
		region = region->next_region;
	}
	return 0;
}

int as_complete_load(struct addrspace *as)
{
	struct region *region = as->as_regions;
	while (region)
	{
		region->permission = region->permission | PERMISSION_READ;
		region = region->next_region;
	}
	return 0;
}

#define USER_STACKPAGES 16

int as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	*stackptr = USERSTACK;
	return as_define_region(as, USERSTACK - PAGE_SIZE * USER_STACKPAGES, PAGE_SIZE * USER_STACKPAGES, PERMISSION_READ, PERMISSION_WRITE, 0);
}
