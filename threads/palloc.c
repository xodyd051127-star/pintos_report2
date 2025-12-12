#include "threads/palloc.h"
#include <bitmap.h>
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/loader.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/thread.h" // list 사용을 위해 추가

/* Page allocator. Hands out memory in page-size (or
   page-multiple) chunks. See malloc.h for an allocator that
   hands out smaller chunks.

   System memory is divided into two "pools" called the kernel
   and user pools. The user pool is for user (virtual) memory
   pages, the kernel pool for everything else. */

/* A memory pool. */
struct pool
{
    struct lock lock;         /* Mutual exclusion. */
    struct bitmap *used_map;  /* Bitmap of free pages. */
    uint8_t *base;            /* Base of pool. */
};

/* Two pools: one for kernel data, one for user pages. */
static struct pool kernel_pool, user_pool;

// [프로젝트 2: 메모리 할당] 추가 시작 -------------------------------------

/* 현재 메모리 할당 모드 (palloc.h에 정의됨) */
static enum palloc_mode current_palloc_mode = PAL_FIRST_FIT;

/* Next Fit을 위한 마지막 검색 위치 (페이지 인덱스) */
static size_t next_fit_idx_k = 0; // Kernel Pool의 Next Fit 인덱스
static size_t next_fit_idx_u = 0; // User Pool의 Next Fit 인덱스

/* Buddy System을 위한 자료 구조 */
// Pintos RAM 크기를 고려하여 최대 크기를 10 (2^10 = 1024 페이지, 약 4MB)로 가정
#define BUDDY_MAX_ORDER 10 
static struct list buddy_free_list[BUDDY_MAX_ORDER];

static bool buddy_initialized = false;
// TODO: Buddy System에 필요한 다른 구조체 (예: 블록 관리를 위한 배열) 추가 필요

// [프로젝트 2: 메모리 할당] 추가 끝 -------------------------------------

static void init_pool (struct pool *, void *base, size_t page_cnt,
                       const char *name);
static bool page_from_pool (const struct pool *, void *page);

// [프로젝트 2: 메모리 할당] 추가: Best Fit 보조 함수 (palloc.c 내부에 정의)
// Best Fit은 bitmap_scan_best_fit()을 사용하도록 가정하고, 여기서는 선언 생략.

// [프로젝트 2: 메모리 할당] 추가: Buddy System 초기화/할당/해제 함수 선언
static void buddy_init (void);
static void *buddy_allocate (struct pool *pool, size_t page_cnt);
static void buddy_free (struct pool *pool, void *pages, size_t page_cnt);


/* Initializes the page allocator. At most USER_PAGE_LIMIT
   pages are put into the user pool. */
void
palloc_init (size_t user_page_limit)
{
    /* Free memory starts at 1 MB and runs to the end of RAM. */
    uint8_t *free_start = ptov (1024 * 1024);
    uint8_t *free_end = ptov (init_ram_pages * PGSIZE);
    size_t free_pages = (free_end - free_start) / PGSIZE;
    size_t user_pages = free_pages / 2;
    size_t kernel_pages;
    if (user_pages > user_page_limit)
        user_pages = user_page_limit;
    kernel_pages = free_pages - user_pages;

    /* Give half of memory to kernel, half to user. */
    init_pool (&kernel_pool, free_start, kernel_pages, "kernel pool");
    init_pool (&user_pool, free_start + kernel_pages * PGSIZE,
               user_pages, "user pool");

    // [프로젝트 2: 메모리 할당] Buddy System 리스트 초기화
    for (int i = 0; i < BUDDY_MAX_ORDER; i++) {
        list_init(&buddy_free_list[i]);
    }
}

// [프로젝트 2: 메모리 할당] 추가 시작 -------------------------------------

/* 메모리 할당 모드를 설정합니다. (palloc.h에 선언됨) */
void
palloc_set_mode (enum palloc_mode mode)
{
    current_palloc_mode = mode;
    
    // Buddy System은 동적으로 k값을 계산하고 초기화해야 합니다.
    if (mode == PAL_BUDDY && !buddy_initialized) {
        // TODO: buddy_init() 함수 호출 및 구현 필요
        // buddy_init(&kernel_pool); 
        // buddy_init(&user_pool);
        // buddy_initialized = true;
    }
}

// TODO: Buddy System 초기화 함수 구현 (palloc_init에서 사용 가능한 페이지를 Buddy 시스템에 맞게 재구성)
static void 
buddy_init(void) 
{ 
    // 구현 필요
}

// TODO: Buddy System 할당 함수 구현 (buddy_free_list를 사용하여 블록 분할/할당)
static void *
buddy_allocate (struct pool *pool UNUSED, size_t page_cnt UNUSED) 
{ 
    // 구현 필요
    return NULL;
}

// TODO: Buddy System 해제 함수 구현 (buddy_free_list를 사용하여 블록 병합/해제)
static void
buddy_free (struct pool *pool UNUSED, void *pages UNUSED, size_t page_cnt UNUSED) 
{ 
    // 구현 필요
}


// [프로젝트 2: 메모리 할당] 추가 끝 -------------------------------------


/* Obtains and returns a group of PAGE_CNT contiguous free pages.
   If PAL_USER is set, the pages are obtained from the user pool,
   otherwise from the kernel pool. If PAL_ZERO is set in FLAGS,
   then the pages are filled with zeros. If too few pages are
   available, returns a null pointer, unless PAL_ASSERT is set in
   FLAGS, in which case the kernel panics. */
void *
palloc_get_multiple (enum palloc_flags flags, size_t page_cnt)
{
    struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;
    void *pages;
    size_t page_idx = BITMAP_ERROR; 
    size_t bitmap_size = bitmap_size (pool->used_map);

    if (page_cnt == 0)
        return NULL;

    lock_acquire (&pool->lock);

    // [수정 시작] 할당 모드에 따른 분기 로직
    if (current_palloc_mode == PAL_FIRST_FIT) {
        // 1. First Fit (기존 동작)
        page_idx = bitmap_scan_and_flip (pool->used_map, 0, page_cnt, false);
    }
    else if (current_palloc_mode == PAL_NEXT_FIT) {
        // 2. Next Fit
        size_t start_idx = (pool == &kernel_pool) ? next_fit_idx_k : next_fit_idx_u;
        
        // A. 현재 Next Fit 인덱스부터 끝까지 검색
        page_idx = bitmap_scan (pool->used_map, start_idx, page_cnt, false);

        if (page_idx == BITMAP_ERROR) {
            // B. 끝까지 못 찾았으면 처음(0)부터 Next Fit 인덱스 직전까지 검색 (Wrap-around)
            page_idx = bitmap_scan (pool->used_map, 0, page_cnt, false);
            
            // 검색된 인덱스가 기존 Next Fit 인덱스보다 커야 함 (이미 검색한 공간은 제외)
            if (page_idx != BITMAP_ERROR && page_idx >= start_idx)
            {
                // 재검색된 인덱스가 start_idx 이후에 있다면, 찾지 못한 것으로 처리
                page_idx = BITMAP_ERROR;
            }
        }
        
        // C. 할당 성공 시 Next Fit 인덱스 업데이트 및 비트맵 플립
        if (page_idx != BITMAP_ERROR) {
            // Next Fit 인덱스 업데이트 (다음 검색 시작 위치)
            size_t next_idx = page_idx + page_cnt;
            if (next_idx >= bitmap_size) next_idx = 0; // Wrap around
            
            if (pool == &kernel_pool) next_fit_idx_k = next_idx;
            else next_fit_idx_u = next_idx;
            
            // 비트맵 플립
            bitmap_set_multiple(pool->used_map, page_idx, page_cnt, true);
        }
    }
    else if (current_palloc_mode == PAL_BEST_FIT) {
        // 3. Best Fit
        // bitmap_scan_best_fit 함수는 bitmap.c에 추가했다고 가정
        page_idx = bitmap_scan_best_fit(pool->used_map, 0, page_cnt, false); 
        if (page_idx != BITMAP_ERROR) {
            // 최적의 공간을 찾았으므로 할당 표시
            bitmap_set_multiple(pool->used_map, page_idx, page_cnt, true);
        }
    }
    else if (current_palloc_mode == PAL_BUDDY) {
        // 4. Buddy System
        // TODO: buddy_allocate()를 호출하도록 변경
        // pages = buddy_allocate(pool, page_cnt);
        // page_idx는 Buddy 시스템에서는 사용되지 않음 (pages에 직접 주소가 할당됨)
        // Buddy System은 bitmap을 직접 사용하지 않으므로, 이 분기 내에서 pages를 설정해야 함
        
        pages = NULL; // 임시
    }
    // [수정 종료]

    // Buddy System을 제외한 Bitmap 기반 할당 방식
    if (current_palloc_mode != PAL_BUDDY)
    {
        if (page_idx != BITMAP_ERROR)
            pages = pool->base + PGSIZE * page_idx;
        else
            pages = NULL;
    }


    lock_release (&pool->lock);

    if (pages != NULL)
    {
        if (flags & PAL_ZERO)
            memset (pages, 0, PGSIZE * page_cnt);
    }
    else
    {
        if (flags & PAL_ASSERT)
            PANIC ("palloc_get: out of pages");
    }

    return pages;
}

/* Obtains a single free page and returns its kernel virtual
   address.
   If PAL_USER is set, the page is obtained from the user pool,
   otherwise from the kernel pool. If PAL_ZERO is set in FLAGS,
   then the page is filled with zeros. If no pages are
   available, returns a null pointer, unless PAL_ASSERT is set in
   FLAGS, in which case the kernel panics. */
void *
palloc_get_page (enum palloc_flags flags)
{
    // Buddy System의 경우 palloc_get_multiple(flags, 1)이 Buddy System을 직접 처리해야 함
    return palloc_get_multiple (flags, 1);
}

/* Frees the PAGE_CNT pages starting at PAGES. */
void
palloc_free_multiple (void *pages, size_t page_cnt)
{
    struct pool *pool;
    size_t page_idx;

    ASSERT (pg_ofs (pages) == 0);
    if (pages == NULL || page_cnt == 0)
        return;

    if (page_from_pool (&kernel_pool, pages))
        pool = &kernel_pool;
    else if (page_from_pool (&user_pool, pages))
        pool = &user_pool;
    else
        NOT_REACHED ();

    // [수정 시작] Buddy System 해제 분기
    if (current_palloc_mode == PAL_BUDDY) {
        // TODO: Buddy System 해제 로직 실행
        // buddy_free(pool, pages, page_cnt);
        return;
    }
    // [수정 종료]

    // First Fit, Next Fit, Best Fit (bitmap 방식)의 해제 로직
    page_idx = pg_no (pages) - pg_no (pool->base);

#ifndef NDEBUG
    memset (pages, 0xcc, PGSIZE * page_cnt);
#endif

    ASSERT (bitmap_all (pool->used_map, page_idx, page_cnt));
    bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
}

/* Frees the page at PAGE. */
void
palloc_free_page (void *page)
{
    palloc_free_multiple (page, 1);
}

/* Initializes pool P as starting at START and ending at END,
   naming it NAME for debugging purposes. */
static void
init_pool (struct pool *p, void *base, size_t page_cnt, const char *name)
{
    /* We'll put the pool's used_map at its base.
       Calculate the space needed for the bitmap
       and subtract it from the pool's size. */
    size_t bm_pages = DIV_ROUND_UP (bitmap_buf_size (page_cnt), PGSIZE);
    if (bm_pages > page_cnt)
        PANIC ("Not enough memory in %s for bitmap.", name);
    page_cnt -= bm_pages;

    printf ("%zu pages available in %s.\n", page_cnt, name);

    /* Initialize the pool. */
    lock_init (&p->lock);
    p->used_map = bitmap_create_in_buf (page_cnt, base, bm_pages * PGSIZE);
    p->base = base + bm_pages * PGSIZE;
}

/* Returns true if PAGE was allocated from POOL,
   false otherwise. */
static bool
page_from_pool (const struct pool *pool, void *page)
{
    size_t page_no = pg_no (page);
    size_t start_page = pg_no (pool->base);
    size_t end_page = start_page + bitmap_size (pool->used_map);

    return page_no >= start_page && page_no < end_page;
}
