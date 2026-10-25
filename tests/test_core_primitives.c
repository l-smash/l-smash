/*****************************************************************************
 * test_core_primitives.c
 *****************************************************************************
 * Copyright (c) 2026 Yusuke Nakamura
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
 * THIS SOFTWARE.
 *****************************************************************************/

/* This file is available under an ISC license. */

#include "common/internal.h"

#include "test_common.h"

/*---------------------------------------------------------------------------
 * Test 1: Byte & Bitstream Primitives (le64, uvlc, leb128, su, ns)
 *---------------------------------------------------------------------------*/
int test_bitstream_primitives( void )
{
    /* 1a. Verify lsmash_bs_get_le64 (regression test for << 16 vs << 32 bug) */
    {
        uint8_t raw_le64[8] = { 0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01 };
        lsmash_bs_t bs = { 0 };
        TEST_ASSERT( lsmash_bs_set_empty_stream( &bs, raw_le64, sizeof(raw_le64) ) == 0, "init le64 bs" );
        uint64_t val = lsmash_bs_get_le64( &bs );
        TEST_ASSERT( val == 0x0123456789ABCDEFULL, "lsmash_bs_get_le64 64-bit shift" );
    }

    /* 1b. Verify av1_get_uvlc across standard, boundary, and error cases */
    {
        lsmash_bs_t *bs = lsmash_bs_create();
        TEST_ASSERT( bs != NULL, "create bs for uvlc" );
        lsmash_bits_t bits = { 0 };
        lsmash_bits_init( &bits, bs );

        put_uvlc( &bits, 0 );
        put_uvlc( &bits, 1 );
        put_uvlc( &bits, 2 );
        put_uvlc( &bits, 3 );
        put_uvlc( &bits, 6 );
        put_uvlc( &bits, 7 );
        put_uvlc( &bits, 42 );
        /* Max valid 32-bit uvlc value: 31 zeros + '1' + 31 ones -> 0xFFFFFFFE */
        for( int i = 0; i < 31; i++ )
            lsmash_bits_put( &bits, 1, 0 );
        lsmash_bits_put( &bits, 1, 1 );
        lsmash_bits_put( &bits, 31, 0x7FFFFFFFULL );
        /* leadingZeros >= 32 -> returns UINT32_MAX */
        for( int i = 0; i < 32; i++ )
            lsmash_bits_put( &bits, 1, 0 );
        lsmash_bits_put( &bits, 1, 1 );
        lsmash_bits_put_align( &bits );

        uint32_t buf_len = 0;
        uint8_t *buf = lsmash_bs_export_data( bs, &buf_len );
        lsmash_bs_cleanup( bs );
        TEST_ASSERT( buf != NULL && buf_len > 0, "export uvlc bitstream" );

        lsmash_bs_t rbs = { 0 };
        lsmash_bs_set_empty_stream( &rbs, buf, buf_len );
        lsmash_bits_t rbits = { 0 };
        lsmash_bits_init( &rbits, &rbs );

        TEST_ASSERT( lsm_av1_get_uvlc( &rbits ) == 0, "uvlc(0)" );
        TEST_ASSERT( lsm_av1_get_uvlc( &rbits ) == 1, "uvlc(1)" );
        TEST_ASSERT( lsm_av1_get_uvlc( &rbits ) == 2, "uvlc(2)" );
        TEST_ASSERT( lsm_av1_get_uvlc( &rbits ) == 3, "uvlc(3)" );
        TEST_ASSERT( lsm_av1_get_uvlc( &rbits ) == 6, "uvlc(6)" );
        TEST_ASSERT( lsm_av1_get_uvlc( &rbits ) == 7, "uvlc(7)" );
        TEST_ASSERT( lsm_av1_get_uvlc( &rbits ) == 42, "uvlc(42)" );
        TEST_ASSERT( lsm_av1_get_uvlc( &rbits ) == 0xFFFFFFFEU, "uvlc(0xFFFFFFFE max valid)" );
        TEST_ASSERT( lsm_av1_get_uvlc( &rbits ) == UINT32_MAX, "uvlc(leadingZeros >= 32)" );
        lsmash_free( buf );

        /* Truncated all-zero stream at EOF should not hang and must return UINT32_MAX */
        uint8_t zero_byte[1] = { 0x00 };
        lsmash_bs_set_empty_stream( &rbs, zero_byte, 1 );
        lsmash_bits_init( &rbits, &rbs );
        TEST_ASSERT( lsm_av1_get_uvlc( &rbits ) == UINT32_MAX, "uvlc truncated zero stream" );
    }

    /* 1c. Verify lsm_av1_get_leb128 and lsm_av1_show_leb128 */
    {
        /* 1-byte: 0 and 127 */
        uint8_t b1[2] = { 0x00, 0x7F };
        lsmash_bs_t bs = { 0 };
        uint8_t nbytes = 0;
        lsmash_bs_set_empty_stream( &bs, b1, sizeof(b1) );
        TEST_ASSERT( lsm_av1_show_leb128( &bs, 0, &nbytes ) == 0 && nbytes == 1, "show_leb128(0)" );
        TEST_ASSERT( lsm_av1_show_leb128( &bs, 1, &nbytes ) == 127 && nbytes == 1, "show_leb128(127)" );
        TEST_ASSERT( lsm_av1_get_leb128( &bs, &nbytes ) == 0 && nbytes == 1, "get_leb128(0)" );
        TEST_ASSERT( lsm_av1_get_leb128( &bs, &nbytes ) == 127 && nbytes == 1, "get_leb128(127)" );

        /* 5-byte: 0xFFFFFFFF (tests i = 4 shift into bits 28..31 without 32-bit signed UB) */
        uint8_t b_max32[5] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x0F };
        lsmash_bs_set_empty_stream( &bs, b_max32, sizeof(b_max32) );
        TEST_ASSERT( lsm_av1_show_leb128( &bs, 0, &nbytes ) == 0xFFFFFFFFULL && nbytes == 5, "show_leb128(UINT32_MAX)" );
        TEST_ASSERT( lsm_av1_get_leb128( &bs, &nbytes ) == 0xFFFFFFFFULL && nbytes == 5, "get_leb128(UINT32_MAX)" );

        /* 8-byte padded representation of 0xFFFFFFFF (legal 8-byte LEB128 <= UINT32_MAX) */
        uint8_t b_pad8[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x8F, 0x80, 0x80, 0x00 };
        lsmash_bs_set_empty_stream( &bs, b_pad8, sizeof(b_pad8) );
        TEST_ASSERT( lsm_av1_show_leb128( &bs, 0, &nbytes ) == 0xFFFFFFFFULL && nbytes == 8, "show_leb128 8-byte padded" );
        TEST_ASSERT( lsm_av1_get_leb128( &bs, &nbytes ) == 0xFFFFFFFFULL && nbytes == 8, "get_leb128 8-byte padded" );

        /* Overflow > UINT32_MAX: 0x100000000 (1ULL << 32) -> must fail with UINT64_MAX and nbytes == 0 */
        uint8_t b_over32[5] = { 0x80, 0x80, 0x80, 0x80, 0x10 };
        lsmash_bs_set_empty_stream( &bs, b_over32, sizeof(b_over32) );
        TEST_ASSERT( lsm_av1_show_leb128( &bs, 0, &nbytes ) == UINT64_MAX && nbytes == 0, "show_leb128 > UINT32_MAX rejected" );
        TEST_ASSERT( lsm_av1_get_leb128( &bs, &nbytes ) == UINT64_MAX && nbytes == 0, "get_leb128 > UINT32_MAX rejected" );

        /* > 8 bytes (8th byte still has MSB 0x80 set) -> must fail */
        uint8_t b_9bytes[9] = { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00 };
        lsmash_bs_set_empty_stream( &bs, b_9bytes, sizeof(b_9bytes) );
        TEST_ASSERT( lsm_av1_show_leb128( &bs, 0, &nbytes ) == UINT64_MAX && nbytes == 0, "show_leb128 > 8 bytes rejected" );
        TEST_ASSERT( lsm_av1_get_leb128( &bs, &nbytes ) == UINT64_MAX && nbytes == 0, "get_leb128 > 8 bytes rejected" );

        /* Truncated LEB128 at EOF -> must fail */
        uint8_t b_trunc[2] = { 0x81, 0x82 };
        lsmash_bs_set_empty_stream( &bs, b_trunc, sizeof(b_trunc) );
        TEST_ASSERT( lsm_av1_show_leb128( &bs, 0, &nbytes ) == UINT64_MAX && nbytes == 0, "show_leb128 truncated rejected" );
        TEST_ASSERT( lsm_av1_get_leb128( &bs, &nbytes ) == UINT64_MAX && nbytes == 0, "get_leb128 truncated rejected" );
    }

    printf( "[PASS] test_bitstream_primitives\n" );
    return 0;
}

/*---------------------------------------------------------------------------
 * Test 5: Self-Contained Memory Audit Allocator Verification
 *---------------------------------------------------------------------------*/
int test_memory_audit_allocator( void )
{
#ifdef LSMASH_DEBUG_ALLOC
    TEST_ASSERT( lsm_alloc_get_active_count() == 0, "initial active block count == 0" );
    TEST_ASSERT( lsm_alloc_get_active_bytes() == 0, "initial active byte count == 0" );

    uint8_t *buf1 = (uint8_t *)lsmash_malloc( 128 );
    TEST_ASSERT( buf1 != NULL, "lsmash_malloc(128) succeeds" );
    TEST_ASSERT( ((uintptr_t)buf1 & 0xF) == 0, "lsmash_malloc returns 16-byte aligned pointer" );
    TEST_ASSERT( buf1[0] == 0xCD && buf1[127] == 0xCD, "lsmash_malloc poisons uninitialized memory with 0xCD" );
    TEST_ASSERT( lsm_alloc_get_active_count() == 1, "active block count == 1" );
    TEST_ASSERT( lsm_alloc_get_active_bytes() == 128, "active byte count == 128" );

    uint8_t *buf2 = (uint8_t *)lsmash_malloc_zero( 64 );
    TEST_ASSERT( buf2 != NULL, "lsmash_malloc_zero(64) succeeds" );
    for( int i = 0; i < 64; i++ )
        if( buf2[i] != 0 )
            TEST_ASSERT( 0, "lsmash_malloc_zero zeroes all bytes" );
    TEST_ASSERT( lsm_alloc_get_active_count() == 2, "active block count == 2" );
    TEST_ASSERT( lsm_alloc_get_active_bytes() == 192, "active byte count == 192" );

    for( int i = 0; i < 64; i++ )
        buf2[i] = (uint8_t)(i + 1);
    uint8_t *buf3 = (uint8_t *)lsmash_memdup( buf2, 64 );
    TEST_ASSERT( buf3 != NULL, "lsmash_memdup(buf2, 64) succeeds" );
    TEST_ASSERT( memcmp( buf2, buf3, 64 ) == 0, "lsmash_memdup copies payload" );
    TEST_ASSERT( lsm_alloc_get_active_count() == 3, "active block count == 3" );
    TEST_ASSERT( lsm_alloc_get_active_bytes() == 256, "active byte count == 256" );

    buf1 = (uint8_t *)lsmash_realloc( buf1, 256 );
    TEST_ASSERT( buf1 != NULL, "lsmash_realloc(buf1, 256) succeeds" );
    TEST_ASSERT( lsm_alloc_get_active_count() == 3, "active block count == 3 after realloc" );
    TEST_ASSERT( lsm_alloc_get_active_bytes() == 384, "active byte count == 384 after realloc" );

    lsmash_free( buf1 );
    TEST_ASSERT( lsm_alloc_get_active_count() == 2, "active block count == 2 after lsmash_free" );
    TEST_ASSERT( lsm_alloc_get_active_bytes() == 128, "active byte count == 128 after lsmash_free" );

    lsmash_freep( &buf2 );
    TEST_ASSERT( buf2 == NULL, "lsmash_freep sets pointer to NULL" );
    TEST_ASSERT( lsm_alloc_get_active_count() == 1, "active block count == 1 after lsmash_freep" );
    TEST_ASSERT( lsm_alloc_get_active_bytes() == 64, "active byte count == 64 after lsmash_freep" );

    lsmash_freep( &buf3 );
    TEST_ASSERT( buf3 == NULL, "lsmash_freep(buf3) sets pointer to NULL" );
    TEST_ASSERT( lsm_alloc_get_active_count() == 0, "active block count == 0 after all frees" );
    TEST_ASSERT( lsm_alloc_get_active_bytes() == 0, "active byte count == 0 after all frees" );
    TEST_ASSERT( lsm_alloc_verify_no_leaks() == 0, "lsm_alloc_verify_no_leaks() == 0" );
#endif
    printf( "[PASS] test_memory_audit_allocator\n" );
    return 0;
}

/*---------------------------------------------------------------------------
 * Test 10: Paged Block Array (lsm_list_t) & Unified Accessor / Iterator
 *---------------------------------------------------------------------------*/
typedef struct
{
    uint64_t id;
    uint32_t tag;
    uint32_t flags;
    void    *owned_buf;
} test_paged_elem_t;

static uint32_t g_test_eliminator_calls = 0;

static void test_paged_elem_eliminator( void *data )
{
    if( !data )
        return;
    test_paged_elem_t *elem = (test_paged_elem_t *)data;
    if( elem->owned_buf )
    {
        lsmash_free( elem->owned_buf );
        elem->owned_buf = NULL;
    }
    ++g_test_eliminator_calls;
}

int test_paged_block_array( void )
{
    /* 10a. Zero-Initialization & NULL Container Safety */
    {
        lsm_list_t zero_list = { 0 };
        TEST_ASSERT( lsm_list_count( NULL ) == 0, "lsm_list_count(NULL) == 0" );
        TEST_ASSERT( lsm_list_count( &zero_list ) == 0, "lsm_list_count(&zero_list) == 0" );
        TEST_ASSERT( lsm_list_at( NULL, 0 ) == NULL, "lsm_list_at(NULL, 0) == NULL" );
        TEST_ASSERT( lsm_list_at( &zero_list, 0 ) == NULL, "lsm_list_at(&zero_list, 0) == NULL" );
        TEST_ASSERT( lsm_list_get( NULL, 1 ) == NULL, "lsm_list_get(NULL, 1) == NULL" );
        TEST_ASSERT( lsm_list_get( &zero_list, 0 ) == NULL, "lsm_list_get(&zero_list, 0) == NULL" );
        TEST_ASSERT( lsm_list_get( &zero_list, 1 ) == NULL, "lsm_list_get(&zero_list, 1) == NULL" );
        TEST_ASSERT( lsm_list_head( NULL ) == NULL, "lsm_list_head(NULL) == NULL" );
        TEST_ASSERT( lsm_list_head( &zero_list ) == NULL, "lsm_list_head(&zero_list) == NULL" );
        TEST_ASSERT( lsm_list_tail( NULL ) == NULL, "lsm_list_tail(NULL) == NULL" );
        TEST_ASSERT( lsm_list_tail( &zero_list ) == NULL, "lsm_list_tail(&zero_list) == NULL" );
        TEST_ASSERT( lsm_list_remove_tail( &zero_list ) < 0, "remove_tail on empty fails safely" );
        TEST_ASSERT( lsm_list_remove( &zero_list, 1 ) < 0, "remove on empty fails safely" );

        uint32_t iter_count = 0;
        test_paged_elem_t *it = NULL;
        LSM_LIST_FOR_EACH( it, &zero_list )
        {
            ++iter_count;
        }
        TEST_ASSERT( iter_count == 0 && it == NULL, "LSM_LIST_FOR_EACH on zero-init list executes 0 times" );
        LSM_LIST_FOR_EACH( it, (lsm_list_t *)NULL )
        {
            ++iter_count;
        }
        TEST_ASSERT( iter_count == 0 && it == NULL, "LSM_LIST_FOR_EACH on NULL list executes 0 times" );

        lsmash_entry_list_t zero_elist = { 0 };
        void *eit = NULL;
        LSM_ENTRY_LIST_FOR_EACH( eit, &zero_elist )
        {
            ++iter_count;
        }
        TEST_ASSERT( iter_count == 0 && eit == NULL, "LSM_ENTRY_LIST_FOR_EACH on zero-init list executes 0 times" );

        lsm_list_clear( &zero_list );
        lsm_list_clear( NULL );
        lsm_list_destroy( NULL );
    }

    /* 10b. Multi-Page Append, O(1) Random Access & 100% Pointer Stability Across 40+ Pages */
    {
        const uint32_t N = 10500; /* crosses >41 pages of 256 elements */
        lsm_list_t list = { 0 };
        lsm_list_init_simple( &list, sizeof(test_paged_elem_t) );

        test_paged_elem_t **saved_ptrs = (test_paged_elem_t **)lsmash_malloc( N * sizeof(test_paged_elem_t *) );
        TEST_ASSERT( saved_ptrs != NULL, "alloc saved_ptrs" );

        for( uint32_t i = 0; i < N; i++ )
        {
            test_paged_elem_t *slot = (test_paged_elem_t *)lsm_list_append( &list );
            TEST_ASSERT( slot != NULL, "lsm_list_append succeeds" );
            TEST_ASSERT( slot->id == 0 && slot->tag == 0 && slot->flags == 0 && slot->owned_buf == NULL,
                         "newly appended slot is zero-initialized" );
            slot->id    = (uint64_t)i * 1000003ULL + 7ULL;
            slot->tag   = i ^ 0xA5A55A5AU;
            slot->flags = i + 1U;
            saved_ptrs[i] = slot;

            /* Verify page boundary transition stability right after crossing a 256-element boundary */
            if( (i & LSM_LIST_PAGE_MASK) == 0 && i > 0 )
            {
                TEST_ASSERT( lsm_list_at( &list, 0 ) == saved_ptrs[0],
                             "page 0 element 0 pointer never moves when new page is allocated" );
                TEST_ASSERT( lsm_list_at( &list, i - 1 ) == saved_ptrs[i - 1],
                             "previous page tail pointer never moves when new page is allocated" );
            }
        }

        TEST_ASSERT( lsm_list_count( &list ) == N, "entry_count == 10500" );
        TEST_ASSERT( list.page_count == ((N + LSM_LIST_PAGE_MASK) >> LSM_LIST_PAGE_SHIFT),
                     "exact number of 256-element pages allocated" );
        TEST_ASSERT( lsm_list_head( &list ) == saved_ptrs[0], "head matches element 0" );
        TEST_ASSERT( lsm_list_tail( &list ) == saved_ptrs[N - 1], "tail matches element N-1" );

        /* Verify 100% pointer stability and O(1) random access for all 10,500 elements */
        for( uint32_t i = 0; i < N; i++ )
        {
            uint32_t perm = (i * 4099U + 123U) % N; /* pseudo-random index */
            test_paged_elem_t *by_at  = (test_paged_elem_t *)lsm_list_at( &list, perm );
            test_paged_elem_t *by_get = (test_paged_elem_t *)lsm_list_get( &list, perm + 1 );
            TEST_ASSERT( by_at == saved_ptrs[perm], "pointer stability 100% preserved for all elements" );
            TEST_ASSERT( by_get == by_at, "1-based lsm_list_get matches 0-based lsm_list_at" );
            TEST_ASSERT( by_at->id == (uint64_t)perm * 1000003ULL + 7ULL && by_at->tag == (perm ^ 0xA5A55A5AU),
                         "element payload intact across 42 pages" );
        }

        /* Verify LSM_LIST_FOR_EACH with continue and break */
        uint32_t visited = 0;
        test_paged_elem_t *elem = NULL;
        LSM_LIST_FOR_EACH( elem, &list )
        {
            if( elem->flags == 10 )
            {
                ++visited;
                continue;
            }
            if( elem->flags == 600 )
                break;
            ++visited;
        }
        TEST_ASSERT( visited == 599 && elem != NULL && elem->flags == 600,
                     "LSM_LIST_FOR_EACH supports single-loop continue and break" );

        /* Verify lockstep cursor forward & backward across page boundaries */
        lsm_list_cursor_t cur;
        lsm_list_cursor_init_at( &cur, &list, 254 );
        TEST_ASSERT( cur.data == saved_ptrs[254], "cursor init at 254" );
        TEST_ASSERT( lsm_list_cursor_next( &cur ) == saved_ptrs[255], "cursor next to 255 (page 0 end)" );
        TEST_ASSERT( lsm_list_cursor_next( &cur ) == saved_ptrs[256], "cursor next to 256 (page 1 start)" );
        TEST_ASSERT( lsm_list_cursor_prev( &cur ) == saved_ptrs[255], "cursor prev back to 255" );

        lsmash_free( saved_ptrs );
        lsm_list_clear( &list );
        TEST_ASSERT( lsm_list_count( &list ) == 0 && list.pages == NULL, "lsm_list_clear frees pages" );
    }

    /* 10c. Tail Removal, Mid-List Removal Across Pages, O(1) Move & Custom Eliminator Leak Check */
    {
        g_test_eliminator_calls = 0;
        lsm_list_t *src = lsm_list_create( sizeof(test_paged_elem_t), test_paged_elem_eliminator );
        TEST_ASSERT( src != NULL, "lsm_list_create succeeds" );

        const uint32_t M = 600; /* 3 pages (0..255, 256..511, 512..599) */
        for( uint32_t i = 0; i < M; i++ )
        {
            test_paged_elem_t item;
            item.id        = i + 100;
            item.tag       = i;
            item.flags     = 1;
            item.owned_buf = lsmash_malloc( 16 );
            TEST_ASSERT( item.owned_buf != NULL, "alloc owned_buf" );
            TEST_ASSERT( lsm_list_add( src, &item ) == 0, "lsm_list_add succeeds" );
        }
        TEST_ASSERT( lsm_list_count( src ) == M, "count == 600" );

        /* Pop 10 elements from tail */
        for( uint32_t i = 0; i < 10; i++ )
            TEST_ASSERT( lsm_list_remove_tail( src ) == 0, "remove_tail succeeds" );
        TEST_ASSERT( lsm_list_count( src ) == 590, "count == 590 after 10 tail pops" );
        TEST_ASSERT( g_test_eliminator_calls == 10, "eliminator called 10 times for tail pops" );

        /* Remove element at 1-based entry_number = 250 (in page 0, shifting elements from page 1 and page 2!) */
        TEST_ASSERT( lsm_list_remove( src, 250 ) == 0, "mid-list remove at entry 250 across pages" );
        TEST_ASSERT( lsm_list_count( src ) == 589, "count == 589 after mid-list removal" );
        TEST_ASSERT( g_test_eliminator_calls == 11, "eliminator called on removed mid-list element" );

        for( uint32_t i = 0; i < 589; i++ )
        {
            uint32_t expected_tag = (i < 249) ? i : (i + 1);
            test_paged_elem_t *e = (test_paged_elem_t *)lsm_list_at( src, i );
            TEST_ASSERT( e != NULL && e->tag == expected_tag && e->owned_buf != NULL,
                         "cross-page shift after mid-list removal preserved exact order and pointers" );
        }

        /* O(1) move from src to dst */
        lsm_list_t dst = { 0 };
        lsm_list_init( &dst, sizeof(test_paged_elem_t), test_paged_elem_eliminator );
        lsm_list_move( &dst, src );
        TEST_ASSERT( lsm_list_count( src ) == 0 && src->pages == NULL, "src is empty after lsm_list_move" );
        TEST_ASSERT( lsm_list_count( &dst ) == 589, "dst has all 589 elements after lsm_list_move" );

        lsm_list_destroy( src );
        lsm_list_clear( &dst );
        TEST_ASSERT( g_test_eliminator_calls == M, "eliminator called for all 600 elements with 0 leaks" );
    }

    printf( "[PASS] test_paged_block_array\n" );
    return 0;
}

/*---------------------------------------------------------------------------
 * Test 11: Large-Scale Paged Sample Tables, Roll Dedup Hazard & Fragmented Stress
 *---------------------------------------------------------------------------*/
int test_large_scale_paged_tables_and_roll_dedup( void )
{
    /* 11a. 10,500-Sample VFR Movie + Roll Group Mid-Table Deduplication + Timeline Pointer Stability */
    {
        const char *mp4_path  = "tests/tmp_test_vfr_10500.mp4";
        const char *dump_path = "tests/tmp_test_vfr_10500_dump.txt";
        const uint32_t N = 10500; /* >41 pages of 256 elements */

        lsmash_root_t *mux_root = lsmash_create_root();
        TEST_ASSERT( mux_root != NULL, "11a: create mux root" );
        lsmash_file_parameters_t mux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( mp4_path, 0, &mux_param ) == 0, "11a: open output mp4" );
        lsmash_brand_type brands[5] = {
            ISOM_BRAND_TYPE_ISOM,
            ISOM_BRAND_TYPE_ISO6,
            ISOM_BRAND_TYPE_AVC1,
            ISOM_BRAND_TYPE_AV01,
            ISOM_BRAND_TYPE_MP42
        };
        mux_param.major_brand   = ISOM_BRAND_TYPE_MP42;
        mux_param.brands        = brands;
        mux_param.brand_count   = 5;
        mux_param.minor_version = 0;
        TEST_ASSERT( lsmash_set_file( mux_root, &mux_param ) != NULL, "11a: set output file" );

        lsmash_movie_parameters_t movie_param;
        lsmash_initialize_movie_parameters( &movie_param );
        movie_param.timescale = 1000;
        TEST_ASSERT( lsmash_set_movie_parameters( mux_root, &movie_param ) == 0, "11a: set movie params" );

        uint32_t tid = lsmash_create_track( mux_root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        TEST_ASSERT( tid == 1, "11a: create video track" );

        lsmash_track_parameters_t track_param;
        lsmash_initialize_track_parameters( &track_param );
        track_param.mode           = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE | ISOM_TRACK_IN_PREVIEW;
        track_param.display_width  = 64 << 16;
        track_param.display_height = 64 << 16;
        TEST_ASSERT( lsmash_set_track_parameters( mux_root, tid, &track_param ) == 0, "11a: set track params" );

        lsmash_media_parameters_t media_param;
        lsmash_initialize_media_parameters( &media_param );
        media_param.timescale     = 90000;
        media_param.roll_grouping = 1;
        media_param.rap_grouping  = 1;
        TEST_ASSERT( lsmash_set_media_parameters( mux_root, tid, &media_param ) == 0, "11a: set media params" );

        lsmash_video_summary_t *summary = create_synth_av01_summary( 64, 64 );
        TEST_ASSERT( summary != NULL, "11a: create synth av01 summary" );
        TEST_ASSERT( lsmash_add_sample_entry( mux_root, tid, summary ) == 1, "11a: add sample entry" );
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );

        uint64_t cur_dts = 0;
        for( uint32_t i = 0; i < N; i++ )
        {
            uint32_t sample_num = i + 1;
            uint32_t sz         = 4 + (i % 13);
            uint32_t delta      = 100 + (i % 5); /* every sample changes delta -> N stts entries */
            uint32_t cts_off    = (i + 1 < N) ? ((i % 3) * 10) : 0; /* every sample changes offset -> N ctts entries */

            lsmash_sample_t *s = lsmash_create_sample( sz );
            TEST_ASSERT( s != NULL, "11a: create sample" );
            for( uint32_t b = 0; b < sz; b++ )
                s->data[b] = (uint8_t)((i * 17 + b) & 0xFF);
            s->dts   = cur_dts;
            s->cts   = cur_dts + cts_off;
            s->index = 1;
            s->prop.leading     = ISOM_SAMPLE_IS_NOT_LEADING;
            s->prop.independent = (i % 2 == 0) ? ISOM_SAMPLE_IS_INDEPENDENT : ISOM_SAMPLE_IS_NOT_INDEPENDENT;
            s->prop.disposable  = (i % 4 == 0) ? ISOM_SAMPLE_IS_DISPOSABLE  : ISOM_SAMPLE_IS_NOT_DISPOSABLE;
            s->prop.redundant   = ISOM_SAMPLE_HAS_NO_REDUNDANCY;
            s->prop.post_roll.identifier = sample_num;

            /* Exercise SYNC, OPEN_RAP, and consecutive POST_ROLL_START groups across page boundaries */
            if( i % 300 == 0 )
            {
                s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
            }
            else if( i % 300 == 150 )
            {
                s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_OPEN_RAP;
            }
            else if( (i % 300) >= 10 && (i % 300) < 40 && i + 2 < N )
            {
                /* 30 consecutive post-roll start samples with identical roll_distance = 2!
                 * Each creates an sbgp entry and a pool entry; as recovery completes while later
                 * entries are still in cache->roll.pool, isom_deduplicate_roll_group merges them
                 * via mid-table lsm_list_remove(sbgp->list, current_group_number)! */
                s->prop.ra_flags           = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_POST_ROLL_START;
                s->prop.post_roll.complete = sample_num + 2;
            }
            else
            {
                s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE;
            }

            TEST_ASSERT( lsmash_append_sample( mux_root, tid, s ) == 0, "11a: append VFR sample" );
            cur_dts += delta;
        }

        uint32_t last_delta = 100 + (N % 5);
        TEST_ASSERT( lsmash_flush_pooled_samples( mux_root, tid, last_delta ) == 0, "11a: flush pooled samples" );
        TEST_ASSERT( lsmash_update_track_duration( mux_root, tid, last_delta ) == 0, "11a: update track duration" );

        /* Add 2 explicit edit list entries */
        lsmash_edit_t edit1 = { .duration = 5000, .start_time = 0,   .rate = ISOM_EDIT_MODE_NORMAL };
        lsmash_edit_t edit2 = { .duration = 6000, .start_time = 100, .rate = ISOM_EDIT_MODE_NORMAL };
        TEST_ASSERT( lsmash_create_explicit_timeline_map( mux_root, tid, edit1 ) == 0, "11a: add edit 1" );
        TEST_ASSERT( lsmash_create_explicit_timeline_map( mux_root, tid, edit2 ) == 0, "11a: add edit 2" );

        TEST_ASSERT( lsmash_finish_movie( mux_root, NULL ) == 0, "11a: finish 10,500-sample movie" );
        lsmash_close_file( &mux_param );
        lsmash_destroy_root( mux_root );

        /* Demux, verify multi-page stbl tables, construct timeline, and verify pointer stability */
        lsmash_root_t *demux_root = lsmash_create_root();
        TEST_ASSERT( demux_root != NULL, "11a: create demux root" );
        lsmash_file_parameters_t demux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( mp4_path, 1, &demux_param ) == 0, "11a: open mp4 for demux" );
        demux_param.mode |= LSMASH_FILE_MODE_DUMP;
        lsmash_file_t *demux_file = lsmash_set_file( demux_root, &demux_param );
        TEST_ASSERT( demux_file != NULL, "11a: set demux file" );
        TEST_ASSERT( lsmash_read_file( demux_file, &demux_param ) >= 0, "11a: read 10,500-sample mp4" );
        TEST_ASSERT( lsmash_print_movie( demux_root, dump_path ) == 0, "11a: print movie dump" );
        remove( dump_path );

        isom_trak_t *trak = (isom_trak_t *)demux_file->moov->trak_list.head->data;
        isom_stbl_t *stbl = trak->mdia->minf->stbl;
        TEST_ASSERT( lsm_list_count( stbl->stts->list ) == N, "11a: stts has 10,500 entries across 42 pages" );
        TEST_ASSERT( lsm_list_count( stbl->ctts->list ) == N, "11a: ctts has 10,500 entries across 42 pages" );
        TEST_ASSERT( lsm_list_count( stbl->stsz->list ) == N, "11a: stsz has 10,500 entries across 42 pages" );
        TEST_ASSERT( lsm_list_count( stbl->sdtp->list ) == N, "11a: sdtp has 10,500 entries across 42 pages" );
        TEST_ASSERT( lsm_list_count( trak->edts->elst->list ) == 2, "11a: elst has 2 entries" );

        isom_sgpd_t *sgpd_roll = (isom_sgpd_t *)stbl->sgpd_list.head->data;
        isom_sbgp_t *sbgp_roll = (isom_sbgp_t *)stbl->sbgp_list.head->data;
        TEST_ASSERT( sgpd_roll != NULL && lsm_list_count( sgpd_roll->list ) == 1,
                     "11a: sgpd roll deduplicated to 1 unique roll_distance (+2)" );
        /* 35 cycles of (non-roll -> 30 merged post-roll samples -> non-roll) = 71 sbgp entries (instead of 35*30 = 1050!) */
        TEST_ASSERT( sbgp_roll != NULL && lsm_list_count( sbgp_roll->list ) == 71,
                     "11a: sbgp roll deduplicated 30 consecutive post-roll groups per cycle into 1 entry" );

        TEST_ASSERT( lsmash_construct_timeline( demux_root, tid ) == 0, "11a: construct 10,500-sample timeline" );
        TEST_ASSERT( lsmash_get_sample_count_in_media_timeline( demux_root, tid ) == N, "11a: timeline sample count == 10,500" );
        TEST_ASSERT( lsmash_count_explicit_timeline_map( demux_root, tid ) == 2, "11a: timeline edit count == 2" );

        uint64_t exp_dts = 0;
        for( uint32_t i = 0; i < N; i++ )
        {
            uint32_t sample_num = i + 1;
            uint32_t exp_sz     = 4 + (i % 13);
            uint32_t exp_delta  = (i + 1 < N) ? (100 + (i % 5)) : last_delta;
            uint32_t exp_off    = (i + 1 < N) ? ((i % 3) * 10) : 0;

            uint64_t got_dts = 0, got_cts = 0;
            uint32_t got_delta = 0;
            lsmash_sample_property_t got_prop = { 0 };
            TEST_ASSERT( lsmash_get_dts_from_media_timeline( demux_root, tid, sample_num, &got_dts ) == 0, "11a: get dts" );
            TEST_ASSERT( lsmash_get_cts_from_media_timeline( demux_root, tid, sample_num, &got_cts ) == 0, "11a: get cts" );
            TEST_ASSERT( lsmash_get_sample_delta_from_media_timeline( demux_root, tid, sample_num, &got_delta ) == 0, "11a: get delta" );
            TEST_ASSERT( lsmash_get_sample_property_from_media_timeline( demux_root, tid, sample_num, &got_prop ) == 0, "11a: get prop" );
            TEST_ASSERT( got_dts == exp_dts && got_cts == exp_dts + exp_off && got_delta == exp_delta,
                         "11a: timeline dts/cts/delta match across 42 pages" );

            if( (i % 300) >= 10 && (i % 300) < 40 && i + 2 < N )
            {
                TEST_ASSERT( (got_prop.ra_flags & ISOM_SAMPLE_RANDOM_ACCESS_FLAG_POST_ROLL_START) != 0
                          && got_prop.post_roll.complete == sample_num + 2,
                             "11a: deduplicated roll group preserved exact post_roll.complete" );
            }

            /* Spot-check payload bytes across page boundaries */
            if( (i & 0xFF) == 0 || (i & 0xFF) == 0xFF || i == N - 1 )
            {
                lsmash_sample_t *s = lsmash_get_sample_from_media_timeline( demux_root, tid, sample_num );
                TEST_ASSERT( s != NULL && s->length == exp_sz, "11a: boundary sample length matches" );
                TEST_ASSERT( s->data[0] == (uint8_t)((i * 17) & 0xFF), "11a: boundary sample payload matches" );
                lsmash_delete_sample( s );
            }
            exp_dts += exp_delta;
        }

        /* Verify lsmash_get_media_timestamps & lsmash_set_media_timestamps on 10,500-sample timeline */
        lsmash_media_ts_list_t ts_list = { 0 };
        TEST_ASSERT( lsmash_get_media_timestamps( demux_root, tid, &ts_list ) == 0, "11a: get_media_timestamps" );
        TEST_ASSERT( ts_list.sample_count == N && ts_list.timestamp != NULL, "11a: ts_list count == 10,500" );
        TEST_ASSERT( lsmash_set_media_timestamps( demux_root, tid, &ts_list ) == 0, "11a: set_media_timestamps" );
        lsmash_delete_media_timestamps( &ts_list );

        lsmash_destruct_timeline( demux_root, tid );
        lsmash_close_file( &demux_param );
        lsmash_destroy_root( demux_root );
        remove( mp4_path );
    }

    /* 11b. Compact Sample Size Table (stz2 4-bit field_size & lsm_list_move across 3 pages) */
    {
        const char *mp4_path = "tests/tmp_test_stz2_600.mp4";
        const uint32_t M = 600; /* 3 pages */

        lsmash_root_t *mux_root = lsmash_create_root();
        lsmash_file_parameters_t mux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( mp4_path, 0, &mux_param ) == 0, "11b: open stz2 mp4" );
        lsmash_brand_type brands[3] = { ISOM_BRAND_TYPE_ISOM, ISOM_BRAND_TYPE_ISO6, ISOM_BRAND_TYPE_AV01 };
        mux_param.major_brand = ISOM_BRAND_TYPE_ISOM;
        mux_param.brands      = brands;
        mux_param.brand_count = 3;
        TEST_ASSERT( lsmash_set_file( mux_root, &mux_param ) != NULL, "11b: set stz2 file" );

        lsmash_movie_parameters_t movie_param;
        lsmash_initialize_movie_parameters( &movie_param );
        TEST_ASSERT( lsmash_set_movie_parameters( mux_root, &movie_param ) == 0, "11b: set movie params" );

        uint32_t tid = lsmash_create_track( mux_root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        lsmash_track_parameters_t track_param;
        lsmash_initialize_track_parameters( &track_param );
        track_param.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE;
        TEST_ASSERT( lsmash_set_track_parameters( mux_root, tid, &track_param ) == 0, "11b: set track params" );

        lsmash_media_parameters_t media_param;
        lsmash_initialize_media_parameters( &media_param );
        media_param.timescale                 = 1000;
        media_param.compact_sample_size_table = 1; /* Enable stsz -> stz2 compaction via lsm_list_move */
        TEST_ASSERT( lsmash_set_media_parameters( mux_root, tid, &media_param ) == 0, "11b: set media params with compact_sample_size_table" );

        lsmash_video_summary_t *summary = create_synth_av01_summary( 64, 64 );
        TEST_ASSERT( lsmash_add_sample_entry( mux_root, tid, summary ) == 1, "11b: add sample entry" );
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );

        for( uint32_t i = 0; i < M; i++ )
        {
            uint32_t sz = 1 + (i % 15); /* 1..15 bytes -> 4-bit stz2 field_size */
            lsmash_sample_t *s = lsmash_create_sample( sz );
            memset( s->data, (int)(i & 0xFF), sz );
            s->dts   = i * 10;
            s->cts   = i * 10;
            s->index = 1;
            s->prop.ra_flags = (i % 60 == 0) ? ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC : ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE;
            TEST_ASSERT( lsmash_append_sample( mux_root, tid, s ) == 0, "11b: append 4-bit size sample" );
        }
        TEST_ASSERT( lsmash_flush_pooled_samples( mux_root, tid, 10 ) == 0, "11b: flush pooled samples" );
        TEST_ASSERT( lsmash_finish_movie( mux_root, NULL ) == 0, "11b: finish movie (triggers stsz -> stz2 lsm_list_move)" );
        lsmash_close_file( &mux_param );
        lsmash_destroy_root( mux_root );

        lsmash_root_t *demux_root = lsmash_create_root();
        lsmash_file_parameters_t demux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( mp4_path, 1, &demux_param ) == 0, "11b: open stz2 mp4 for demux" );
        lsmash_file_t *demux_file = lsmash_set_file( demux_root, &demux_param );
        TEST_ASSERT( lsmash_read_file( demux_file, &demux_param ) >= 0, "11b: read stz2 mp4" );

        isom_trak_t *trak = (isom_trak_t *)demux_file->moov->trak_list.head->data;
        isom_stbl_t *stbl = trak->mdia->minf->stbl;
        TEST_ASSERT( LSMASH_IS_NON_EXISTING_BOX( stbl->stsz ) && LSMASH_IS_EXISTING_BOX( stbl->stz2 ),
                     "11b: stsz compacted into stz2" );
        TEST_ASSERT( stbl->stz2->field_size == 4 && lsm_list_count( stbl->stz2->list ) == M,
                     "11b: stz2 has field_size=4 and 600 entries across 3 pages" );

        TEST_ASSERT( lsmash_construct_timeline( demux_root, tid ) == 0, "11b: construct timeline from stz2" );
        for( uint32_t i = 0; i < M; i++ )
        {
            lsmash_sample_t info = { 0 };
            TEST_ASSERT( lsmash_get_sample_info_from_media_timeline( demux_root, tid, i + 1, &info ) == 0, "11b: get sample info" );
            TEST_ASSERT( info.length == 1 + (i % 15), "11b: 4-bit stz2 entry_size matches across 3 pages" );
        }
        lsmash_destruct_timeline( demux_root, tid );
        lsmash_close_file( &demux_param );
        lsmash_destroy_root( demux_root );
        remove( mp4_path );
    }

    /* 11c. Multi-Page Fragmented Movie (fragment->pool, trun->optional, tfra->list, sidx->list) */
    for( int is_segment = 0; is_segment <= 1; is_segment++ )
    {
        const char *fmp4_path = "tests/tmp_test_frag_vfr_900.mp4";
        const uint32_t FRAGS = 3;
        const uint32_t SAMPLES_PER_FRAG = 300; /* >256 elements per fragment -> crosses page boundary in pool & trun->optional */

        lsmash_root_t *mux_root = lsmash_create_root();
        lsmash_file_parameters_t mux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( fmp4_path, 0, &mux_param ) == 0, "11c: open fragmented mp4" );
        mux_param.mode |= LSMASH_FILE_MODE_FRAGMENTED;
        if( is_segment )
            mux_param.mode |= LSMASH_FILE_MODE_INDEX | LSMASH_FILE_MODE_SEGMENT;
        lsmash_brand_type brands[3] = { ISOM_BRAND_TYPE_ISO6, ISOM_BRAND_TYPE_DASH, ISOM_BRAND_TYPE_AV01 };
        mux_param.major_brand = ISOM_BRAND_TYPE_DASH;
        mux_param.brands      = brands;
        mux_param.brand_count = 3;
        TEST_ASSERT( lsmash_set_file( mux_root, &mux_param ) != NULL, "11c: set fragmented file" );

        lsmash_movie_parameters_t movie_param;
        lsmash_initialize_movie_parameters( &movie_param );
        TEST_ASSERT( lsmash_set_movie_parameters( mux_root, &movie_param ) == 0, "11c: set movie params" );

        uint32_t tid = lsmash_create_track( mux_root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        lsmash_track_parameters_t track_param;
        lsmash_initialize_track_parameters( &track_param );
        track_param.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE;
        TEST_ASSERT( lsmash_set_track_parameters( mux_root, tid, &track_param ) == 0, "11c: set track params" );

        lsmash_media_parameters_t media_param;
        lsmash_initialize_media_parameters( &media_param );
        media_param.timescale = 90000;
        TEST_ASSERT( lsmash_set_media_parameters( mux_root, tid, &media_param ) == 0, "11c: set media params" );

        lsmash_video_summary_t *summary = create_synth_av01_summary( 64, 64 );
        TEST_ASSERT( lsmash_add_sample_entry( mux_root, tid, summary ) == 1, "11c: add sample entry" );
        lsmash_cleanup_summary( (lsmash_summary_t *)summary );

        uint64_t cur_dts = 0;
        for( uint32_t f = 0; f < FRAGS; f++ )
        {
            TEST_ASSERT( lsmash_create_fragment_movie( mux_root ) == 0, "11c: create fragment" );
            for( uint32_t s_idx = 0; s_idx < SAMPLES_PER_FRAG; s_idx++ )
            {
                uint32_t g_idx = f * SAMPLES_PER_FRAG + s_idx;
                uint32_t sz    = 8 + (g_idx % 19);
                uint32_t delta = 300 + (g_idx % 7);
                uint32_t off   = (g_idx % 4) * 50;

                lsmash_sample_t *s = lsmash_create_sample( sz );
                memset( s->data, (int)((g_idx * 13 + 5) & 0xFF), sz );
                s->dts   = cur_dts;
                s->cts   = cur_dts + off;
                s->index = 1;
                s->prop.ra_flags = (s_idx == 0) ? ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC : ISOM_SAMPLE_RANDOM_ACCESS_FLAG_NONE;
                TEST_ASSERT( lsmash_append_sample( mux_root, tid, s ) == 0, "11c: append fragmented VFR sample" );
                cur_dts += delta;
            }
            uint32_t last_frag_delta = 300 + (((f + 1) * SAMPLES_PER_FRAG - 1) % 7);
            TEST_ASSERT( lsmash_flush_pooled_samples( mux_root, tid, last_frag_delta ) == 0, "11c: flush fragment pool" );
        }
        lsmash_adhoc_remux_t remux = { .buffer_size = 65536 };
        TEST_ASSERT( lsmash_finish_movie( mux_root, is_segment ? &remux : NULL ) == 0, "11c: finish fragmented movie" );
        lsmash_close_file( &mux_param );
        lsmash_destroy_root( mux_root );

        lsmash_root_t *demux_root = lsmash_create_root();
        lsmash_file_parameters_t demux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( fmp4_path, 1, &demux_param ) == 0, "11c: open fragmented mp4 for demux" );
        lsmash_file_t *demux_file = lsmash_set_file( demux_root, &demux_param );
        TEST_ASSERT( lsmash_read_file( demux_file, &demux_param ) >= 0, "11c: read fragmented mp4" );

        if( is_segment )
        {
            TEST_ASSERT( demux_file->sidx_list.head != NULL, "11c: sidx_list present" );
            isom_sidx_t *sidx = (isom_sidx_t *)demux_file->sidx_list.head->data;
            TEST_ASSERT( lsm_list_count( sidx->list ) == FRAGS, "11c: sidx->list has 3 subsegment entries" );
        }
        else
        {
            TEST_ASSERT( LSMASH_IS_EXISTING_BOX( demux_file->mfra ) && demux_file->mfra->tfra_list.head != NULL, "11c: mfra/tfra present" );
            isom_tfra_t *tfra = (isom_tfra_t *)demux_file->mfra->tfra_list.head->data;
            TEST_ASSERT( lsm_list_count( tfra->list ) == FRAGS, "11c: tfra->list has 3 sync sample entries" );
        }

        TEST_ASSERT( lsmash_construct_timeline( demux_root, tid ) == 0, "11c: construct fragmented VFR timeline" );
        TEST_ASSERT( lsmash_get_sample_count_in_media_timeline( demux_root, tid ) == FRAGS * SAMPLES_PER_FRAG,
                     "11c: fragmented timeline has 900 samples" );

        uint64_t exp_dts = 0;
        for( uint32_t g_idx = 0; g_idx < FRAGS * SAMPLES_PER_FRAG; g_idx++ )
        {
            uint32_t sz    = 8 + (g_idx % 19);
            uint32_t delta = 300 + (g_idx % 7);
            uint32_t off   = (g_idx % 4) * 50;
            uint64_t got_dts = 0, got_cts = 0;
            lsmash_sample_t info = { 0 };
            TEST_ASSERT( lsmash_get_dts_from_media_timeline( demux_root, tid, g_idx + 1, &got_dts ) == 0, "11c: get frag dts" );
            TEST_ASSERT( lsmash_get_cts_from_media_timeline( demux_root, tid, g_idx + 1, &got_cts ) == 0, "11c: get frag cts" );
            TEST_ASSERT( lsmash_get_sample_info_from_media_timeline( demux_root, tid, g_idx + 1, &info ) == 0, "11c: get frag info" );
            TEST_ASSERT( got_dts == exp_dts && got_cts == exp_dts + off && info.length == sz,
                         "11c: fragmented VFR sample dts/cts/length match across page boundaries" );
            exp_dts += delta;
        }

        lsmash_destruct_timeline( demux_root, tid );
        lsmash_close_file( &demux_param );
        lsmash_destroy_root( demux_root );
        remove( fmp4_path );
    }

    printf( "[PASS] test_large_scale_paged_tables_and_roll_dedup\n" );
    return 0;
}

/*---------------------------------------------------------------------------
 * Test 14: Multi-Buffer Resize & Codec / Metadata Regressions
 *---------------------------------------------------------------------------*/
int test_multibuf_resize_and_codec_regressions( void )
{
    /* 14a. lsmash_multiple_buffers_t Creation, Expansion, Shrinkage & Data Integrity */
    {
        TEST_ASSERT( lsmash_create_multiple_buffers( 0, 32 ) == NULL, "14a: reject 0 buffers" );
        TEST_ASSERT( lsmash_create_multiple_buffers( 4, 0 ) == NULL, "14a: reject 0 buffer_size" );
        TEST_ASSERT( lsmash_create_multiple_buffers( 0x100000, 0x10000 ) == NULL, "14a: reject uint32 overflow" );

        lsmash_multiple_buffers_t *bank = lsmash_create_multiple_buffers( 4, 32 );
        TEST_ASSERT( bank != NULL && bank->number_of_buffers == 4 && bank->buffer_size == 32,
                     "14a: create 4x32 multiple buffers" );
        TEST_ASSERT( lsmash_withdraw_buffer( bank, 0 ) == NULL, "14a: reject buffer_number 0" );
        TEST_ASSERT( lsmash_withdraw_buffer( bank, 5 ) == NULL, "14a: reject buffer_number > 4" );
        TEST_ASSERT( lsmash_resize_multiple_buffers( bank, 0 ) == NULL, "14a: reject resize to 0" );

        /* Fill each of the 4 buffers with a unique byte pattern */
        for( uint32_t b = 1; b <= 4; b++ )
        {
            uint8_t *buf = (uint8_t *)lsmash_withdraw_buffer( bank, b );
            TEST_ASSERT( buf != NULL, "14a: withdraw buffer 1..4" );
            for( uint32_t k = 0; k < 32; k++ )
                buf[k] = (uint8_t)((b * 53U + k * 7U + 0x11U) & 0xFFU);
        }

        /* No-op resize to same size */
        TEST_ASSERT( lsmash_resize_multiple_buffers( bank, 32 ) == bank, "14a: same-size resize is no-op" );

        /* Expand from 32 to 96 bytes per buffer */
        TEST_ASSERT( lsmash_resize_multiple_buffers( bank, 96 ) == bank && bank->buffer_size == 96,
                     "14a: expand multiple buffers from 32 to 96 bytes" );
        for( uint32_t b = 1; b <= 4; b++ )
        {
            uint8_t *buf = (uint8_t *)lsmash_withdraw_buffer( bank, b );
            TEST_ASSERT( buf != NULL, "14a: withdraw expanded buffer" );
            for( uint32_t k = 0; k < 32; k++ )
            {
                uint8_t expected = (uint8_t)((b * 53U + k * 7U + 0x11U) & 0xFFU);
                TEST_ASSERT( buf[k] == expected,
                             "14a: expanded buffer preserves exact original data in [0..31] across all 4 buffers" );
            }
            for( uint32_t k = 32; k < 96; k++ )
            {
                TEST_ASSERT( buf[k] == 0, "14a: expanded tail [32..95] is zero-initialized" );
                buf[k] = (uint8_t)((b * 29U + k) & 0xFFU);
            }
        }

        /* Shrink from 96 to 20 bytes per buffer */
        TEST_ASSERT( lsmash_resize_multiple_buffers( bank, 20 ) == bank && bank->buffer_size == 20,
                     "14a: shrink multiple buffers from 96 to 20 bytes" );
        for( uint32_t b = 1; b <= 4; b++ )
        {
            uint8_t *buf = (uint8_t *)lsmash_withdraw_buffer( bank, b );
            TEST_ASSERT( buf != NULL, "14a: withdraw shrunk buffer" );
            for( uint32_t k = 0; k < 20; k++ )
            {
                uint8_t expected = (uint8_t)((b * 53U + k * 7U + 0x11U) & 0xFFU);
                TEST_ASSERT( buf[k] == expected,
                             "14a: shrunk buffer preserves exact prefix [0..19] across all 4 buffers" );
            }
        }

        lsmash_destroy_multiple_buffers( bank );
    }

    /* 14b. lsm_nalu_get_codeNum Exp-Golomb Valid Decoding, EOF & >=64 Zero Bits Non-Hang */
    {
        /* Valid Exp-Golomb codes packed into 1 byte:
         * '1' (0) + '010' (1) + '011' (2) + '1' (0) = 0b10100111 = 0xA7 */
        uint8_t valid_eg[1] = { 0xA7 };
        lsmash_bits_t *bits = lsmash_bits_adhoc_create();
        TEST_ASSERT( bits != NULL, "14b: create adhoc bits" );
        TEST_ASSERT( lsmash_bits_import_data( bits, valid_eg, sizeof(valid_eg) ) == 0, "14b: import valid Exp-Golomb" );
        TEST_ASSERT( lsm_nalu_get_codeNum( bits ) == 0, "14b: Exp-Golomb '1' -> 0" );
        TEST_ASSERT( lsm_nalu_get_codeNum( bits ) == 1, "14b: Exp-Golomb '010' -> 1" );
        TEST_ASSERT( lsm_nalu_get_codeNum( bits ) == 2, "14b: Exp-Golomb '011' -> 2" );
        TEST_ASSERT( lsm_nalu_get_codeNum( bits ) == 0, "14b: Exp-Golomb '1' -> 0" );
        lsmash_bits_adhoc_cleanup( bits );

        /* Truncated 1-byte all-zero stream -> hits EOB and returns 0 without infinite loop */
        uint8_t eof_zeros[1] = { 0x00 };
        bits = lsmash_bits_adhoc_create();
        TEST_ASSERT( lsmash_bits_import_data( bits, eof_zeros, sizeof(eof_zeros) ) == 0, "14b: import 1 zero byte" );
        TEST_ASSERT( lsm_nalu_get_codeNum( bits ) == 0 && bits->bs->eob == 1,
                     "14b: lsm_nalu_get_codeNum terminates on EOB and returns 0" );
        lsmash_bits_adhoc_cleanup( bits );

        /* 16-byte all-zero stream (128 zero bits) -> hits leadingZeroBits >= 64 before EOB and returns 0 */
        uint8_t long_zeros[16] = { 0 };
        bits = lsmash_bits_adhoc_create();
        TEST_ASSERT( lsmash_bits_import_data( bits, long_zeros, sizeof(long_zeros) ) == 0, "14b: import 16 zero bytes" );
        TEST_ASSERT( lsm_nalu_get_codeNum( bits ) == 0 && bits->bs->eob == 0,
                     "14b: lsm_nalu_get_codeNum terminates at 64 leading zero bits without hanging" );
        lsmash_bits_adhoc_cleanup( bits );
    }

    /* 14c. MP3 nBlockSize Calculation in lsm_qt_wfex_update_bitrate */
    {
        /* Build a minimal in-memory stbl + mdhd with a QT_CODEC_TYPE_FULLMP3_AUDIO sample entry and MPEGLAYER3WAVEFORMAT */
        lsmash_root_t *root = lsmash_create_root();
        lsmash_file_parameters_t param = { 0 };
        const char *tmp_mov = "tests/tmp_wfex_mp3.mov";
        TEST_ASSERT( lsmash_open_file( tmp_mov, 0, &param ) == 0, "14c: open tmp mov" );
        param.major_brand = ISOM_BRAND_TYPE_QT;
        lsmash_file_t *file = lsmash_set_file( root, &param );
        TEST_ASSERT( file != NULL, "14c: set file" );
        lsmash_movie_parameters_t movie_param;
        lsmash_initialize_movie_parameters( &movie_param );
        movie_param.timescale = 600;
        TEST_ASSERT( lsmash_set_movie_parameters( root, &movie_param ) == 0, "14c: set movie params" );
        uint32_t tid = lsmash_create_track( root, ISOM_MEDIA_HANDLER_TYPE_AUDIO_TRACK );
        TEST_ASSERT( tid == 1, "14c: create audio track" );
        isom_trak_t *trak = (isom_trak_t *)file->moov->trak_list.head->data;
        isom_mdhd_t *mdhd = trak->mdia->mdhd;
        mdhd->timescale   = 48000;
        mdhd->duration    = 1152;
        isom_stbl_t *stbl = trak->mdia->minf->stbl;

        isom_audio_entry_t *mp3_entry = lsm_box_add_audio_description( stbl->stsd, QT_CODEC_TYPE_FULLMP3_AUDIO );
        TEST_ASSERT( LSMASH_IS_EXISTING_BOX( mp3_entry ), "14c: add .mp3 audio description" );
        isom_wave_t *wave = lsm_box_add_wave( mp3_entry );
        TEST_ASSERT( LSMASH_IS_EXISTING_BOX( wave ), "14c: add wave box" );

        /* MPEGLAYER3WAVEFORMAT: 8 (box header) + 18 (WAVEFORMATEX) + 12 (cbSize=12) = 38 bytes
         * Note: lsm_box_add_extension_binary takes ownership of wfex_buf and frees it on root destruction. */
        uint8_t *wfex_buf = (uint8_t *)lsmash_malloc_zero( 38 );
        TEST_ASSERT( wfex_buf != NULL, "14c: alloc wfex_buf" );
        put_be32( &wfex_buf[0], 38 );
        put_be32( &wfex_buf[4], QT_CODEC_TYPE_FULLMP3_AUDIO.fourcc );
        uint8_t *ex = &wfex_buf[8];
        put_le16( &ex[0],  0x0055 ); /* wFormatTag = WAVE_FORMAT_MPEGLAYER3 */
        put_le16( &ex[2],  2 );      /* nChannels = 2 */
        put_le32( &ex[4],  48000 );  /* nSamplesPerSec = 48000 */
        put_le16( &ex[16], 12 );     /* cbSize = 12 */
        put_le16( &ex[26], 1 );      /* nFramesPerBlock = 1 */
        TEST_ASSERT( lsm_box_add_extension_binary( wave, QT_CODEC_TYPE_FULLMP3_AUDIO, 0, wfex_buf, 38 ) == 0,
                     "14c: attach MPEGLAYER3WAVEFORMAT binary box" );

        /* Add 1 sample of 384 bytes with duration 1152 at 48000 Hz -> avgBitrate = 384 * 8 * 48000 / 1152 = 128000 bps */
        isom_stts_t *stts = stbl->stts;
        isom_stsz_t *stsz = stbl->stsz;
        isom_stsc_t *stsc = stbl->stsc;
        isom_stts_entry_t *stts_e = (isom_stts_entry_t *)lsm_list_append( stts->list );
        stts_e->sample_count = 1;
        stts_e->sample_delta = 1152;
        stsz->sample_size  = 384;
        stsz->sample_count = 1;
        isom_stsc_entry_t *stsc_e = (isom_stsc_entry_t *)lsm_list_append( stsc->list );
        stsc_e->first_chunk              = 1;
        stsc_e->samples_per_chunk        = 1;
        stsc_e->sample_description_index = 1;

        TEST_ASSERT( lsm_qt_wfex_update_bitrate( stbl, mdhd, 1 ) == 0,
                     "14c: lsm_qt_wfex_update_bitrate succeeds" );
        isom_box_t *wave_ext = lsm_box_get_extension_box( &wave->extensions, QT_CODEC_TYPE_FULLMP3_AUDIO );
        TEST_ASSERT( wave_ext != NULL && wave_ext->binary != NULL, "14c: get updated wave_ext" );
        uint16_t nBlockSize = LSMASH_GET_LE16( &wave_ext->binary[8 + 24] );
        /* 144 * 128000 / 48000 = 384 (previously truncated to 144 * (128000 / 48000) = 288!) */
        TEST_ASSERT( nBlockSize == 384, "14c: MP3 nBlockSize is 384 (not 288 from premature integer division)" );

        /* Also verify cbSize < 12 is rejected without out-of-bounds write */
        put_le16( &wave_ext->binary[8 + 16], 4 );
        TEST_ASSERT( lsm_qt_wfex_update_bitrate( stbl, mdhd, 1 ) == LSMASH_ERR_INVALID_DATA,
                     "14c: lsm_qt_wfex_update_bitrate rejects MP3 wfex with cbSize < 12" );

        lsmash_close_file( &param );
        lsmash_destroy_root( root );
        remove( tmp_mov );
    }

    /* 14d. WMA cbSize 16-bit LE Reading & Bounds Check in lsm_wma_print_codec_specific */
    {
        uint8_t wma_bin[36] = { 0 };
        put_be32( &wma_bin[0], 36 );
        put_be32( &wma_bin[4], LSMASH_4CC( 'w', 'f', 'e', 'x' ) );
        uint8_t *d = &wma_bin[8];
        put_le16( &d[0],  0x0161 ); /* WAVE_FORMAT_TAG_ID_WMA_V2 */
        put_le16( &d[2],  2 );
        put_le32( &d[4],  44100 );
        put_le32( &d[8],  16000 );
        put_le16( &d[12], 743 );
        put_le16( &d[14], 16 );
        put_le16( &d[16], 10 );     /* cbSize = 10 in LE16 */
        put_le32( &d[18], 2048 );   /* dwSamplesPerBlock */

        isom_box_t box = { 0 };
        box.manager = LSMASH_BINARY_CODED_BOX;
        box.type    = ISOM_BOX_TYPE_WFEX;
        box.size    = 36;
        box.binary  = wma_bin;

        const char *tmp_wma_dump = "tests/tmp_wma_dump.txt";
        FILE *fp = fopen( tmp_wma_dump, "wb" );
        TEST_ASSERT( fp != NULL, "14d: open tmp_wma_dump" );
        TEST_ASSERT( lsm_wma_print_codec_specific( fp, NULL, &box, 0 ) == 0,
                     "14d: lsm_wma_print_codec_specific succeeds for valid WMA v2 wfex" );
        /* Now set cbSize = 0x010A (266 in LE16, whose low byte is 10) while box.size remains 36 */
        put_le16( &d[16], 0x010A );
        TEST_ASSERT( lsm_wma_print_codec_specific( fp, NULL, &box, 0 ) == LSMASH_ERR_INVALID_DATA,
                     "14d: lsm_wma_print_codec_specific reads 16-bit LE cbSize=266 and rejects truncated box" );
        fclose( fp );
        remove( tmp_wma_dump );
    }

    /* 14e. Opus 2ch & 8ch Channel Order Lookup in lsm_opus_print_codec_specific */
    {
        lsmash_opus_specific_parameters_t opus_param = { 0 };
        opus_param.Version              = 0;
        opus_param.OutputChannelCount   = 8;
        opus_param.PreSkip              = 312;
        opus_param.InputSampleRate      = 48000;
        opus_param.OutputGain           = 0;
        opus_param.ChannelMappingFamily = 1;
        opus_param.StreamCount          = 5;
        opus_param.CoupledCount         = 3;
        for( uint8_t i = 0; i < 8; i++ )
            opus_param.ChannelMapping[i] = i;

        uint32_t dops_len = 0;
        uint8_t *dops_bin = lsmash_create_opus_specific_info( &opus_param, &dops_len );
        TEST_ASSERT( dops_bin != NULL && dops_len == 29, "14e: create 8ch dOps box" );

        lsmash_file_t dummy_file = { 0 };
        isom_box_t box = { 0 };
        box.manager = LSMASH_BINARY_CODED_BOX;
        box.type    = ISOM_BOX_TYPE_DOPS;
        box.size    = dops_len;
        box.binary  = dops_bin;

        const char *tmp_opus_dump = "tests/tmp_opus_dump.txt";
        FILE *fp = fopen( tmp_opus_dump, "wb" );
        TEST_ASSERT( fp != NULL, "14e: open tmp_opus_dump" );
        TEST_ASSERT( lsm_opus_print_codec_specific( fp, &dummy_file, &box, 0 ) == 0,
                     "14e: lsm_opus_print_codec_specific succeeds for 8ch ChannelMappingFamily=1" );
        fclose( fp );
        lsmash_free( dops_bin );

        FILE *rfp = fopen( tmp_opus_dump, "rb" );
        TEST_ASSERT( rfp != NULL, "14e: open opus dump for read" );
        fseek( rfp, 0, SEEK_END );
        size_t dump_len = (size_t)ftell( rfp );
        fseek( rfp, 0, SEEK_SET );
        char *dump_str = (char *)lsmash_malloc_zero( dump_len + 1 );
        TEST_ASSERT( dump_str != NULL && fread( dump_str, 1, dump_len, rfp ) == dump_len, "14e: read opus dump" );
        fclose( rfp );
        TEST_ASSERT( strstr( dump_str, "1 -> 1: front center" ) != NULL,
                     "14e: 8ch Opus channel 1 printed as 'front center' (not 'fron center')" );
        TEST_ASSERT( strstr( dump_str, "7 -> 7: LFE" ) != NULL,
                     "14e: 8ch Opus channel 7 printed as 'LFE' via channel_order[OutputChannelCount - 1]" );
        lsmash_free( dump_str );
        remove( tmp_opus_dump );
    }

    /* 14f. Custom iTunes Metadata Retrieval Type & lsmash_count_tyrant_chapter */
    {
        TEST_ASSERT( lsmash_count_tyrant_chapter( NULL ) == 0,
                     "14f: lsmash_count_tyrant_chapter(NULL) safely returns 0 without NULL deref" );

        const char *chap_path = "tests/tmp_tyrant_chapters.txt";
        const char *m4a_path  = "tests/tmp_itunes_meta.m4a";
        FILE *cfp = fopen( chap_path, "wb" );
        TEST_ASSERT( cfp != NULL, "14f: create chapter file" );
        fprintf( cfp, "CHAPTER01=00:00:00.000\nCHAPTER01NAME=Intro\nCHAPTER02=00:00:01.000\nCHAPTER02NAME=Main\n" );
        fclose( cfp );

        lsmash_root_t *mux_root = lsmash_create_root();
        lsmash_file_parameters_t mux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( m4a_path, 0, &mux_param ) == 0, "14f: open m4a for mux" );
        lsmash_brand_type brands[3] = { ISOM_BRAND_TYPE_M4A, ISOM_BRAND_TYPE_MP42, ISOM_BRAND_TYPE_ISOM };
        mux_param.major_brand = ISOM_BRAND_TYPE_M4A;
        mux_param.brands      = brands;
        mux_param.brand_count = 3;
        TEST_ASSERT( lsmash_set_file( mux_root, &mux_param ) != NULL, "14f: set m4a file" );

        lsmash_movie_parameters_t movie_param;
        lsmash_initialize_movie_parameters( &movie_param );
        lsmash_set_movie_parameters( mux_root, &movie_param );

        uint32_t tid = lsmash_create_track( mux_root, ISOM_MEDIA_HANDLER_TYPE_VIDEO_TRACK );
        lsmash_track_parameters_t track_param;
        lsmash_initialize_track_parameters( &track_param );
        track_param.mode = ISOM_TRACK_ENABLED | ISOM_TRACK_IN_MOVIE;
        lsmash_set_track_parameters( mux_root, tid, &track_param );
        lsmash_media_parameters_t media_param;
        lsmash_initialize_media_parameters( &media_param );
        media_param.timescale = 1000;
        lsmash_set_media_parameters( mux_root, tid, &media_param );

        lsmash_video_summary_t *vsum = create_synth_av01_summary( 64, 64 );
        TEST_ASSERT( lsmash_add_sample_entry( mux_root, tid, vsum ) == 1, "14f: add sample entry" );
        lsmash_cleanup_summary( (lsmash_summary_t *)vsum );

        for( uint32_t i = 0; i < 2; i++ )
        {
            lsmash_sample_t *s = lsmash_create_sample( 8 );
            memset( s->data, (int)(i + 1), 8 );
            s->dts   = (uint64_t)i * 1000;
            s->cts   = (uint64_t)i * 1000;
            s->index = 1;
            s->prop.ra_flags = ISOM_SAMPLE_RANDOM_ACCESS_FLAG_SYNC;
            TEST_ASSERT( lsmash_append_sample( mux_root, tid, s ) == 0, "14f: append sample" );
        }
        TEST_ASSERT( lsmash_flush_pooled_samples( mux_root, tid, 1000 ) == 0, "14f: flush samples" );
        TEST_ASSERT( lsmash_update_track_duration( mux_root, tid, 1000 ) == 0, "14f: update duration" );

        /* Set 2 chapters in chpl */
        TEST_ASSERT( lsmash_set_tyrant_chapter( mux_root, (char *)chap_path, 0 ) == 0, "14f: set tyrant chapter" );

        /* Set custom STRING and custom INTEGER iTunes metadata */
        lsmash_itunes_metadata_t custom_str = { 0 };
        custom_str.item         = ITUNES_METADATA_ITEM_CUSTOM;
        custom_str.type         = ITUNES_METADATA_TYPE_STRING;
        custom_str.meaning      = "com.apple.iTunes";
        custom_str.name         = "CUSTOM_TAG_STR";
        custom_str.value.string = "L-SMASH Custom String";
        TEST_ASSERT( lsmash_set_itunes_metadata( mux_root, custom_str ) == 0, "14f: set custom string metadata" );

        lsmash_itunes_metadata_t custom_int = { 0 };
        custom_int.item          = ITUNES_METADATA_ITEM_CUSTOM;
        custom_int.type          = ITUNES_METADATA_TYPE_INTEGER;
        custom_int.meaning       = "com.apple.iTunes";
        custom_int.name          = "CUSTOM_TAG_INT";
        custom_int.value.integer = 20261021ULL;
        TEST_ASSERT( lsmash_set_itunes_metadata( mux_root, custom_int ) == 0, "14f: set custom int metadata" );

        TEST_ASSERT( lsmash_finish_movie( mux_root, NULL ) == 0, "14f: finish m4a" );
        lsmash_close_file( &mux_param );
        lsmash_destroy_root( mux_root );
        remove( chap_path );

        /* Reopen and verify lsmash_count_tyrant_chapter & lsmash_get_itunes_metadata */
        lsmash_root_t *demux_root = lsmash_create_root();
        lsmash_file_parameters_t demux_param = { 0 };
        TEST_ASSERT( lsmash_open_file( m4a_path, 1, &demux_param ) == 0, "14f: open m4a for read" );
        lsmash_file_t *demux_file = lsmash_set_file( demux_root, &demux_param );
        TEST_ASSERT( lsmash_read_file( demux_file, &demux_param ) >= 0, "14f: read m4a" );

        TEST_ASSERT( lsmash_count_tyrant_chapter( demux_root ) == 2,
                     "14f: lsmash_count_tyrant_chapter returns 2" );
        double chap_ts = -1.0;
        char *chap_name = lsmash_get_tyrant_chapter( demux_root, 2, &chap_ts );
        TEST_ASSERT( chap_name != NULL && strcmp( chap_name, "Main" ) == 0 && chap_ts == 1.0,
                     "14f: lsmash_get_tyrant_chapter(2) matches 'Main' at 1.0s" );

        TEST_ASSERT( lsmash_count_itunes_metadata( demux_root ) == 2, "14f: count_itunes_metadata == 2" );

        lsmash_itunes_metadata_t got_str = { 0 };
        TEST_ASSERT( lsmash_get_itunes_metadata( demux_root, 1, &got_str ) == 0, "14f: get custom string metadata" );
        TEST_ASSERT( got_str.item == ITUNES_METADATA_ITEM_CUSTOM
                  && got_str.type == ITUNES_METADATA_TYPE_STRING
                  && got_str.value.string != NULL
                  && strcmp( got_str.value.string, "L-SMASH Custom String" ) == 0,
                     "14f: custom string metadata retrieved with ITUNES_METADATA_TYPE_STRING" );
        lsmash_cleanup_itunes_metadata( &got_str );

        lsmash_itunes_metadata_t got_int = { 0 };
        TEST_ASSERT( lsmash_get_itunes_metadata( demux_root, 2, &got_int ) == 0, "14f: get custom int metadata" );
        TEST_ASSERT( got_int.item == ITUNES_METADATA_ITEM_CUSTOM
                  && got_int.type == ITUNES_METADATA_TYPE_INTEGER
                  && got_int.value.integer == 20261021ULL,
                     "14f: custom integer metadata retrieved with ITUNES_METADATA_TYPE_INTEGER" );
        lsmash_cleanup_itunes_metadata( &got_int );

        lsmash_close_file( &demux_param );
        lsmash_destroy_root( demux_root );
        remove( m4a_path );
    }

    printf( "[PASS] test_multibuf_resize_and_codec_regressions\n" );
    return 0;
}
