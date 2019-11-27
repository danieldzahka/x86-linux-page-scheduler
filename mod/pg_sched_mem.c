#include <asm/pgalloc.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
/* #include <linux/badger_trap.h> */
#include <linux/syscalls.h>
#include <linux/hugetlb.h>
#include <linux/kernel.h>


/* This is a modified version of the Page Table walk from */
/* badger trap https://research.cs.wisc.edu/multifacet/BadgerTrap/ */
/*
 * This function walks the page table of the process being marked for badger trap
 * This helps in finding all the PTEs that are to be marked as reserved. This is 
 * espicially useful to start badger trap on the fly using (2) and (3). If we do not
 * call this function, when starting badger trap for any process, we may miss some TLB 
 * misses from being tracked which may not be desierable.
 *
 * Note: This function takes care of transparent hugepages and hugepages in general.
 */

/* pte_t *huge_pte_offset(struct mm_struct *mm, */
/* 		       unsigned long addr, unsigned long sz); */

int pg_sched_scan_pgtbl(struct mm_struct *mm)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	pte_t *page_table;
	spinlock_t *ptl;
	unsigned long address;
	unsigned long i,j,k,l;
	unsigned long user = 0;
	unsigned long mask = _PAGE_USER | _PAGE_PRESENT;
	struct vm_area_struct *vma;
	pgd_t *base = mm->pgd;
	for(i=0; i<PTRS_PER_PGD; i++)
	{
		pgd = base + i;
		if((pgd_flags(*pgd) & mask) != mask)
			continue;
		for(j=0; j<PTRS_PER_PUD; j++)
		{
			pud = (pud_t *)pgd_page_vaddr(*pgd) + j;
			if((pud_flags(*pud) & mask) != mask)
                        	continue;
			address = (i<<PGDIR_SHIFT) + (j<<PUD_SHIFT);
			if(vma && pud_huge(*pud) && is_vm_hugetlb_page(vma))
			{
				spin_lock(&mm->page_table_lock);
				/* page_table = huge_pte_offset(mm, address , vma_mmu_pagesize(vma)); */
				/* *page_table = pte_mkreserve(*page_table); */
				spin_unlock(&mm->page_table_lock);
				continue;
			}
			for(k=0; k<PTRS_PER_PMD; k++)
			{
				pmd = (pmd_t *)pud_page_vaddr(*pud) + k;
				if((pmd_flags(*pmd) & mask) != mask)
					continue;
				address = (i<<PGDIR_SHIFT) + (j<<PUD_SHIFT) + (k<<PMD_SHIFT);
				vma = find_vma(mm, address);
				if(vma && pmd_huge(*pmd) && (transparent_hugepage_enabled(vma)||is_vm_hugetlb_page(vma)))
				{
					spin_lock(&mm->page_table_lock);
					/* *pmd = pmd_mkreserve(*pmd); */
					spin_unlock(&mm->page_table_lock);
					continue;
				}
				for(l=0; l<PTRS_PER_PTE; l++)
				{
					pte = (pte_t *)pmd_page_vaddr(*pmd) + l;
					if((pte_flags(*pte) & mask) != mask)
						continue;
					address = (i<<PGDIR_SHIFT) + (j<<PUD_SHIFT) + (k<<PMD_SHIFT) + (l<<PAGE_SHIFT);
					vma = find_vma(mm, address);
					if(vma)
					{
						page_table = pte_offset_map_lock(mm, pmd, address, &ptl);
						/* *pte = pte_mkreserve(*pte); */
						pte_unmap_unlock(page_table, ptl);
					}
					user++;
				}
			}
		}
	}

	return user;
}
