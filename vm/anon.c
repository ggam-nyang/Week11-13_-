/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/vaddr.h"
#include "bitmap.h"
#include "threads/mmu.h"

#define CEILING(x, y) (((x) + (y) - 1) / (y))
#define SECTORS_PER_PAGE CEILING(PGSIZE, DISK_SECTOR_SIZE)

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

static const struct page_operations anon_stack_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON | VM_MARKER_0,
};

static struct bitmap *swap_table;

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get (1, 1);

	disk_sector_t num_sector = disk_size (swap_disk);
	size_t max_slot = num_sector / SECTORS_PER_PAGE;

	swap_table = bitmap_create (max_slot);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	/* 3-2 에서 했으나, 의미는 확실치 않음 WHY!! */
	page->operations = &anon_ops;
	if (type & VM_MARKER_0)
		page->operations = &anon_stack_ops;
	
	struct anon_page *anon_page = &page->anon;
	anon_page->owner = thread_current ();
	anon_page->swap_slot_idx = INVALID_SLOT_IDX;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	if (anon_page->swap_slot_idx == INVALID_SLOT_IDX) return false;

	disk_sector_t sec_no;

	for (int i = 0; i < SECTORS_PER_PAGE; i++)
	{
		sec_no = (disk_sector_t) (anon_page->swap_slot_idx * SECTORS_PER_PAGE) + i;
		off_t ofs = i * DISK_SECTOR_SIZE;
		disk_read (swap_disk, sec_no, kva + ofs);
	}
	// clear
	bitmap_set (swap_table, anon_page->swap_slot_idx, false);

	anon_page->swap_slot_idx = INVALID_SLOT_IDX;

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	// swap table에서 slot index 가져오기
	size_t swap_slot_idx = bitmap_scan_and_flip (swap_table, 0, 1, false);
	if (swap_slot_idx == BITMAP_ERROR)
		PANIC("there is no more free slot in the disk");
	
	// 예외처리 (page, frame, kva가 없을 때)
	if (page == NULL || page->frame == NULL || page->frame->kva == NULL)
		return false;

	disk_sector_t sec_no;
	// disk에 옮겨 적기
	for (int i = 0; i < SECTORS_PER_PAGE; i++)
	{
		sec_no = (disk_sector_t) (swap_slot_idx * SECTORS_PER_PAGE) + i;
		off_t ofs = i * DISK_SECTOR_SIZE;
		disk_write (swap_disk, sec_no, page->frame->kva + ofs);
	}
	anon_page->swap_slot_idx = swap_slot_idx;

	pml4_clear_page (anon_page->owner->pml4, page->va);
	pml4_set_dirty (anon_page->owner->pml4, page->va, false);
	page->frame = NULL;

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	/* WHY!! 의미를 잘 모르겠음 */
	if (page->frame != NULL) {
		list_remove (&page->frame->elem);
		free(page->frame);
	}

	else {
		/* swap이 된 경우인데, 아직은 고려하지 않는다. */
		ASSERT (anon_page->swap_slot_idx != INVALID_SLOT_IDX);

		bitmap_set (swap_table, anon_page->swap_slot_idx, false);
	}
}
