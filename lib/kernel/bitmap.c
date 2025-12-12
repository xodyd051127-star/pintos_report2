#include "bitmap.h"
#include <debug.h>
#include <limits.h>
#include <round.h>
#include <stdio.h>
#include "threads/malloc.h"
#ifdef FILESYS
#include "filesys/file.h"
#endif

/* Element type.
   This must be an unsigned integer type at least as wide as int.
   Each bit represents one bit in the bitmap.
   If bit 0 in an element represents bit K in the bitmap,
   then bit 1 in the element represents bit K+1 in the bitmap,
   and so on. */
typedef unsigned long elem_type;

/* Number of bits in an element. */
#define ELEM_BITS (sizeof (elem_type) * CHAR_BIT)

/* From the outside, a bitmap is an array of bits. From the
   inside, it's an array of elem_type (defined above) that
   simulates an array of bits. */
struct bitmap
{
    size_t bit_cnt;  /* Number of bits. */
    elem_type *bits; /* Elements that represent bits. */
};

/* Returns the index of the element that contains the bit
   numbered BIT_IDX. */
static inline size_t
elem_idx (size_t bit_idx)
{
    return bit_idx / ELEM_BITS;
}

/* Returns an elem_type where only the bit corresponding to
   BIT_IDX is turned on. */
static inline elem_type
bit_mask (size_t bit_idx)
{
    return (elem_type)1 << (bit_idx % ELEM_BITS);
}

/* Returns the number of elements required for BIT_CNT bits. */
static inline size_t
elem_cnt (size_t bit_cnt)
{
    return DIV_ROUND_UP (bit_cnt, ELEM_BITS);
}

/* Returns the number of bytes required for BIT_CNT bits. */
static inline size_t
byte_cnt (size_t bit_cnt)
{
    return sizeof (elem_type) * elem_cnt (bit_cnt);
}

/* Returns a bit mask in which the bits actually used in the last
   element of B's bits are set to 1 and the rest are set to 0. */
static inline elem_type
last_mask (const struct bitmap *b)
{
    int last_bits = b->bit_cnt % ELEM_BITS;
    return last_bits ? ((elem_type)1 << last_bits) - 1 : (elem_type)-1;
}

/* Creation and destruction. */

/* Initializes B to be a bitmap of BIT_CNT bits
   and sets all of its bits to false.
   Returns true if success, false if memory allocation
   failed. */
struct bitmap *
bitmap_create (size_t bit_cnt)
{
    struct bitmap *b = malloc (sizeof *b);
    if (b != NULL)
    {
        b->bit_cnt = bit_cnt;
        b->bits = malloc (byte_cnt (bit_cnt));
        if (b->bits != NULL || bit_cnt == 0)
        {
            bitmap_set_all (b, false);
            return b;
        }
        free (b);
    }
    return NULL;
}

/* Creates and returns a bitmap with BIT_CNT bits in the
   BLOCK_SIZE bytes of storage preallocated at BLOCK.
   BLOCK_SIZE must be at least bitmap_needed_bytes(BIT_CNT). */
struct bitmap *
bitmap_create_in_buf (size_t bit_cnt, void *block, size_t block_size UNUSED)
{
    struct bitmap *b = block;

    ASSERT (block_size >= bitmap_buf_size (bit_cnt));

    b->bit_cnt = bit_cnt;
    b->bits = (elem_type *)(b + 1);
    bitmap_set_all (b, false);
    return b;
}

/* Returns the number of bytes required to accomodate a bitmap
   with BIT_CNT bits (for use with bitmap_create_in_buf()). */
size_t
bitmap_buf_size (size_t bit_cnt)
{
    return sizeof (struct bitmap) + byte_cnt (bit_cnt);
}

/* Destroys bitmap B, freeing its storage.
   Not for use on bitmaps created by
   bitmap_create_preallocated(). */
void
bitmap_destroy (struct bitmap *b)
{
    if (b != NULL)
    {
        free (b->bits);
        free (b);
    }
}

/* Bitmap size. */

/* Returns the number of bits in B. */
size_t
bitmap_size (const struct bitmap *b)
{
    return b->bit_cnt;
}

/* Setting and testing single bits. */

/* Atomically sets the bit numbered IDX in B to VALUE. */
void
bitmap_set (struct bitmap *b, size_t idx, bool value)
{
    ASSERT (b != NULL);
    ASSERT (idx < b->bit_cnt);
    if (value)
        bitmap_mark (b, idx);
    else
        bitmap_reset (b, idx);
}

/* Atomically sets the bit numbered BIT_IDX in B to true. */
void
bitmap_mark (struct bitmap *b, size_t bit_idx)
{
    size_t idx = elem_idx (bit_idx);
    elem_type mask = bit_mask (bit_idx);

    /* This is equivalent to `b->bits[idx] |= mask' except that it
       is guaranteed to be atomic on a uniprocessor machine. See
       the description of the OR instruction in [IA32-v2b]. */
    asm ("orl %1, %0" : "=m"(b->bits[idx]) : "r"(mask) : "cc");
}

/* Atomically sets the bit numbered BIT_IDX in B to false. */
void
bitmap_reset (struct bitmap *b, size_t bit_idx)
{
    size_t idx = elem_idx (bit_idx);
    elem_type mask = bit_mask (bit_idx);

    /* This is equivalent to `b->bits[idx] &= ~mask' except that it
       is guaranteed to be atomic on a uniprocessor machine. See
       the description of the AND instruction in [IA32-v2a]. */
    asm ("andl %1, %0" : "=m"(b->bits[idx]) : "r"(~mask) : "cc");
}

/* Atomically toggles the bit numbered IDX in B;
   that is, if it is true, makes it false,
   and if it is false, makes it true. */
void
bitmap_flip (struct bitmap *b, size_t bit_idx)
{
    size_t idx = elem_idx (bit_idx);
    elem_type mask = bit_mask (bit_idx);

    /* This is equivalent to `b->bits[idx] ^= mask' except that it
       is guaranteed to be atomic on a uniprocessor machine. See
       the description of the XOR instruction in [IA32-v2b]. */
    asm ("xorl %1, %0" : "=m"(b->bits[idx]) : "r"(mask) : "cc");
}

/* Returns the value of the bit numbered IDX in B. */
bool
bitmap_test (const struct bitmap *b, size_t idx)
{
    ASSERT (b != NULL);
    ASSERT (idx < b->bit_cnt);
    return (b->bits[elem_idx (idx)] & bit_mask (idx)) != 0;
}

/* Setting and testing multiple bits. */

/* Sets all bits in B to VALUE. */
void
bitmap_set_all (struct bitmap *b, bool value)
{
    ASSERT (b != NULL);

    bitmap_set_multiple (b, 0, bitmap_size (b), value);
}

/* Sets the CNT bits starting at START in B to VALUE. */
void
bitmap_set_multiple (struct bitmap *b, size_t start, size_t cnt, bool value)
{
    size_t i;

    ASSERT (b != NULL);
    ASSERT (start <= b->bit_cnt);
    ASSERT (start + cnt <= b->bit_cnt);

    for (i = 0; i < cnt; i++)
        bitmap_set (b, start + i, value);
}

/* Returns the number of bits in B between START and START + CNT,
   exclusive, that are set to VALUE. */
size_t
bitmap_count (const struct bitmap *b, size_t start, size_t cnt, bool value)
{
    size_t i, value_cnt;

    ASSERT (b != NULL);
    ASSERT (start <= b->bit_cnt);
    ASSERT (start + cnt <= b->bit_cnt);

    value_cnt = 0;
    for (i = 0; i < cnt; i++)
        if (bitmap_test (b, start + i) == value)
            value_cnt++;
    return value_cnt;
}

/* Returns true if any bits in B between START and START + CNT,
   exclusive, are set to VALUE, and false otherwise. */
bool
bitmap_contains (const struct bitmap *b, size_t start, size_t cnt, bool value)
{
    size_t i;

    ASSERT (b != NULL);
    ASSERT (start <= b->bit_cnt);
    ASSERT (start + cnt <= b->bit_cnt);

    for (i = 0; i < cnt; i++)
        if (bitmap_test (b, start + i) == value)
            return true;
    return false;
}

/* Returns true if any bits in B between START and START + CNT,
   exclusive, are set to true, and false otherwise.*/
bool
bitmap_any (const struct bitmap *b, size_t start, size_t cnt)
{
    return bitmap_contains (b, start, cnt, true);
}

/* Returns true if no bits in B between START and START + CNT,
   exclusive, are set to true, and false otherwise.*/
bool
bitmap_none (const struct bitmap *b, size_t start, size_t cnt)
{
    return !bitmap_contains (b, start, cnt, true);
}

/* Returns true if every bit in B between START and START + CNT,
   exclusive, is set to true, and false otherwise. */
bool
bitmap_all (const struct bitmap *b, size_t start, size_t cnt)
{
    return !bitmap_contains (b, start, cnt, false);
}

/* Finding set or unset bits. */

/* Finds and returns the starting index of the first group of CNT
   consecutive bits in B at or after START that are all set to
   VALUE.
   If there is no such group, returns BITMAP_ERROR. */
size_t
bitmap_scan (const struct bitmap *b, size_t start, size_t cnt, bool value)
{
    ASSERT (b != NULL);
    ASSERT (start <= b->bit_cnt);

    if (cnt <= b->bit_cnt)
    {
        size_t last = b->bit_cnt - cnt;
        size_t i;
        for (i = start; i <= last; i++)
            if (!bitmap_contains (b, i, cnt, !value))
                return i;
    }
    return BITMAP_ERROR;
}

/* [프로젝트 2: 메모리 할당] Best Fit을 위한 함수 추가 시작 */

/* Finds and returns the starting index of the group of consecutive
   bits in B that:
   1. Is at or after START.
   2. All bits are set to VALUE (false for free space).
   3. Has a size >= CNT.
   4. Has the minimum size among all groups satisfying 1, 2, and 3 (Best Fit).
   
   If there is no such group, returns BITMAP_ERROR. */
size_t
bitmap_scan_best_fit (const struct bitmap *b, size_t start, size_t cnt, bool value)
{
    size_t min_size = SIZE_MAX; // 찾은 최소 크기 (초기값으로 최대 크기 사용)
    size_t best_idx = BITMAP_ERROR;  // 최소 크기를 가진 블록의 인덱스
    size_t i = start;

    ASSERT (b != NULL);
    ASSERT (start <= b->bit_cnt);

    if (cnt > b->bit_cnt)
        return BITMAP_ERROR;
    
    // last = b->bit_cnt - cnt; // 끝까지 검색해야 하므로, 마지막 유효한 인덱스는 b->bit_cnt까지
    size_t last = b->bit_cnt;

    while (i < last) 
    {
        // 1. VALUE가 아닌 비트를 찾기 (연속된 VALUE 블록의 시작)
        // 예를 들어, value=false(빈 공간)라면, true(사용 중)인 비트를 찾음
        i = bitmap_scan (b, i, 1, !value); 
        
        if (i == BITMAP_ERROR)
            break;

        // 2. 빈 공간의 시작을 찾았음 (i는 현재 빈 공간 블록의 첫 번째 인덱스)
        // 빈 공간의 끝을 찾기 (VALUE가 아닌 비트를 다시 찾음)
        size_t next_used = bitmap_scan (b, i, 1, value);
        
        // 블록의 끝 인덱스 (빈 공간이 끝까지 이어지는 경우)
        size_t block_end_idx = (next_used == BITMAP_ERROR) ? b->bit_cnt : next_used;
        
        size_t current_size = block_end_idx - i;

        // 3. 요청 크기(cnt)를 만족하는지 확인
        if (current_size >= cnt)
        {
            // 4. Best Fit 조건 확인: 현재 크기가 min_size보다 작은가?
            if (current_size < min_size)
            {
                min_size = current_size;
                best_idx = i;
            }
        }
        
        // 다음 검색 시작점 설정 (현재 블록의 다음 비트부터)
        i = block_end_idx;
    }

    return best_idx;
}

/* [프로젝트 2: 메모리 할당] Best Fit을 위한 함수 추가 끝 */

/* Finds the first group of CNT consecutive bits in B at or after
   START that are all set to VALUE, flips them all to !VALUE,
   and returns the index of the first bit in the group.
   If there is no such group, returns BITMAP_ERROR.
   If CNT is zero, returns 0.
   Bits are set atomically, but testing bits is not atomic with
   setting them. */
size_t
bitmap_scan_and_flip (struct bitmap *b, size_t start, size_t cnt, bool value)
{
    size_t idx = bitmap_scan (b, start, cnt, value);
    if (idx != BITMAP_ERROR)
        bitmap_set_multiple (b, idx, cnt, !value);
    return idx;
}

/* File input and output. */

#ifdef FILESYS
/* Returns the number of bytes needed to store B in a file. */
size_t
bitmap_file_size (const struct bitmap *b)
{
    return byte_cnt (b->bit_cnt);
}

/* Reads B from FILE. Returns true if successful, false
   otherwise. */
bool
bitmap_read (struct bitmap *b, struct file *file)
{
    bool success = true;
    if (b->bit_cnt > 0)
    {
        off_t size = byte_cnt (b->bit_cnt);
        success = file_read_at (file, b->bits, size, 0) == size;
        b->bits[elem_cnt (b->bit_cnt) - 1] &= last_mask (b);
    }
    return success;
}

/* Writes B to FILE. Return true if successful, false
   otherwise. */
bool
bitmap_write (const struct bitmap *b, struct file *file)
{
    off_t size = byte_cnt (b->bit_cnt);
    return file_write_at (file, b->bits, size, 0) == size;
}
#endif /* FILESYS */

/* Debugging. */

/* Dumps the contents of B to the console as hexadecimal. */
void
bitmap_dump (const struct bitmap *b)
{
    hex_dump (0, b->bits, byte_cnt (b->bit_cnt), false);
}
