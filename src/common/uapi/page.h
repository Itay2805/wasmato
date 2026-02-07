#pragma once

/**
 * Page size is 4kb
 */
#define PAGE_SHIFT  12
#define PAGE_MASK   ((1 << PAGE_SHIFT) - 1)
#define PAGE_SIZE   (1 << PAGE_SHIFT)

#define SIZE_TO_PAGES(size)   (((size) >> PAGE_SHIFT) + (((size) & PAGE_MASK) ? 1 : 0))
#define PAGES_TO_SIZE(pages)  ((pages) << PAGE_SHIFT)
