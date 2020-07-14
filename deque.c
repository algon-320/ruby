#include "internal.h"
#include "internal/gc.h"
#include "internal/numeric.h"
#include "internal/object.h"
#include "ruby/encoding.h"
#include "ruby/util.h"
#include "builtin.h"

#if !DEQUE_DEBUG
#undef NDEBUG
#define NDEBUG
#endif
#include "ruby_assert.h"

#define DEQ_MAX_SIZE (LONG_MAX / (int)sizeof(VALUE))

#define DEQ_LEN(deq) (RDEQUE(deq)->len)
#define DEQ_TABLE_CAP(deq) (RDEQUE(deq)->table_cap)
#define DEQ_TABLE_HEADER_PTR(deq) (RDEQUE(deq)->ptr)

static void
deq_heap_free(VALUE deq);

// -------------------------------------------------------------------------

void
rb_deq_free(VALUE deq)
{
    deq_heap_free(deq);
}

RUBY_FUNC_EXPORTED size_t
rb_deq_memsize(VALUE deq)
{
    struct RDequeChunkTableHeader *header = RDEQUE_TABLE_HEADER_PTR(deq);
    long used_chunk = header->last_chunk_idx - header->first_chunk_idx + 1;
    return RDEQUE_CHUNK_TABLE_SIZE(DEQ_TABLE_CAP(deq))
            + used_chunk * RDEQUE_CHUNK_SIZE * sizeof(VALUE);
}

static inline void
memfill(register VALUE *mem, register long size, register VALUE val)
{
    while (size--) {
        *mem++ = val;
    }
}

static inline unsigned long
deq_calc_new_table_capa(VALUE deq)
{
    unsigned long old_capa = RDEQUE_TABLE_CAP(deq);
    unsigned long new_capa = old_capa * 2;
    if (new_capa > RDEQUE_MAX_LEN / RDEQUE_CHUNK_SIZE) {
        new_capa = RDEQUE_MAX_LEN / RDEQUE_CHUNK_SIZE;
    }
    return new_capa;
}

static inline void
rb_deq_modify_check(VALUE ary)
{
    rb_check_frozen(ary);
}

static VALUE
empty_deq_alloc(VALUE klass)
{
    NEWOBJ_OF(deq, struct RDeque, klass, T_DEQUE);
    RDEQUE(deq)->len = 0;
    RDEQUE(deq)->table_cap = 0;
    RDEQUE(deq)->ptr = NULL;
    return (VALUE)deq;
}

static VALUE *
deq_heap_alloc_chunk()
{
    return ALLOC_N(VALUE, RDEQUE_CHUNK_SIZE);
}

static struct RDequeChunkTableHeader *
deq_heap_alloc_table_with_header(long cap)
{
    return (struct RDequeChunkTableHeader *)ALLOC_N(VALUE, RDEQUE_CHUNK_TABLE_SIZE(cap) / sizeof(VALUE));
}

static void
deq_heap_realloc_table(VALUE deq, long new_cap)
{
    assert(!OBJ_FROZEN(deq));
    long old_cap = DEQ_TABLE_CAP(deq);
    struct RDequeChunkTableHeader *new_header, *old_header;
    old_header = DEQ_TABLE_HEADER_PTR(deq);
    new_header = (struct RDequeChunkTableHeader *)ALLOC_N(VALUE, RDEQUE_CHUNK_TABLE_SIZE(new_cap) / sizeof(VALUE));
    VALUE **new_table_ptr = RDEQUE_TABLE_FROM_HEADER(new_header);
    long used_chunk = old_header->last_chunk_idx - old_header->first_chunk_idx + 1;
    
    // copy header
    *new_header = *old_header;

    // copy table contents
    long offset = (new_cap - used_chunk) / 2 - old_header->first_chunk_idx;  // centering
    MEMCPY(new_table_ptr + offset, RDEQUE_TABLE_PTR(deq), VALUE, old_cap);

    new_header->first_chunk_idx += offset;
    new_header->last_chunk_idx += offset;

    DEQ_TABLE_CAP(deq) = new_cap;
    DEQ_TABLE_HEADER_PTR(deq) = new_header;
    ruby_sized_xfree(old_header, RDEQUE_CHUNK_TABLE_SIZE(old_cap));
}

static void
deq_heap_free(VALUE deq)
{
    struct RDequeChunkTableHeader *table_header = RDEQUE_TABLE_HEADER_PTR(deq);
    long first = table_header->first_chunk_idx;
    long last = table_header->last_chunk_idx;
    for (long i = first; i <= last; i++) {
        ruby_sized_xfree((void *)RDEQUE_CHUNK_PTR(deq, i), RDEQUE_CHUNK_SIZE * sizeof(VALUE));
        RDEQUE_TABLE_PTR(deq)[i] = (VALUE *)NULL;
    }
    ruby_sized_xfree(table_header, RDEQUE_CHUNK_TABLE_SIZE(RDEQUE_TABLE_CAP(deq)));

    DEQ_LEN(deq) = 0;
    DEQ_TABLE_CAP(deq) = 0;
    DEQ_TABLE_HEADER_PTR(deq) = (struct RDequeChunkTableHeader *)NULL;
}

static VALUE
rb_deq_initialize(int argc, VALUE *argv, VALUE deq)
{
    rb_deq_modify_check(deq);
    if (argc >= 3) {
        rb_raise(rb_eArgError, "too many arguments");
    }

    VALUE size, fill_val;
    if (argc == 0) {
        size = LONG2FIX(0);
        fill_val = Qnil;
    } else {
        rb_scan_args(argc, argv, "02", &size, &fill_val);
    }

    long len = NUM2LONG(size);
    if (len < 0) rb_raise(rb_eArgError, "negative size");
    if (len > DEQ_MAX_SIZE) rb_raise(rb_eArgError, "too large deque");

    if (DEQ_TABLE_HEADER_PTR(deq) != NULL) {
        deq_heap_free(deq);
    }

    long top_half = len / 2;
    long top_half_cap = (top_half + RDEQUE_CHUNK_SIZE - 1) / RDEQUE_CHUNK_SIZE;
    if (top_half_cap == 0) top_half_cap = 1;
    long bot_half = len - top_half;
    long bot_half_cap = (bot_half + RDEQUE_CHUNK_SIZE - 1) / RDEQUE_CHUNK_SIZE;
    if (bot_half_cap == 0) bot_half_cap = 1;
    
    long table_cap = top_half_cap + bot_half_cap;
    struct RDequeChunkTableHeader *new_header;
    new_header = deq_heap_alloc_table_with_header(table_cap);
    
    VALUE **table = RDEQUE_TABLE_FROM_HEADER(new_header);
    long rem;

    for (long i = 0; i < table_cap; i++) {
        table[i] = deq_heap_alloc_chunk();
    }

    // initialize top half
    rem = top_half % RDEQUE_CHUNK_SIZE;
    memfill(table[0] + (RDEQUE_CHUNK_SIZE - rem), rem, fill_val);
    for (long i = 1; i < top_half_cap; i++) {
        memfill(table[i], RDEQUE_CHUNK_SIZE, fill_val);
    }
    new_header->first_chunk_idx = 0;
    new_header->first_chunk_size = rem;

    // initialize bottom half
    long bot_begin = top_half_cap;
    for (long i = 0; i + 1 < bot_half_cap; i++) {
        memfill(table[bot_begin + i], RDEQUE_CHUNK_SIZE, fill_val);
    }
    rem = bot_half % RDEQUE_CHUNK_SIZE;
    memfill(table[bot_begin + bot_half_cap - 1], rem, fill_val);
    new_header->last_chunk_idx = bot_begin + bot_half_cap - 1;
    new_header->last_chunk_size = rem;

    DEQ_LEN(deq) = len;
    DEQ_TABLE_CAP(deq) = table_cap;
    DEQ_TABLE_HEADER_PTR(deq) = new_header;
    return deq;
}

static inline VALUE *
rb_deq_ref_ptr(VALUE deq, long idx)
{
    struct RDequeChunkTableHeader *header = RDEQUE_TABLE_HEADER_PTR(deq);
    long total_size = DEQ_LEN(deq);
    if (idx >= total_size || idx < -total_size) {
        return NULL; // out of range
    }
    if (idx < 0) {
        idx += total_size;
    }

    if ((unsigned long)idx < header->first_chunk_size) {
        long space = RDEQUE_CHUNK_SIZE - header->first_chunk_size;
        return &RDEQUE_CHUNK_PTR(deq, header->first_chunk_idx)[space + idx];
    }

    idx -= header->first_chunk_size;
    long chunk = header->first_chunk_idx + 1 + idx / RDEQUE_CHUNK_SIZE;
    idx %= RDEQUE_CHUNK_SIZE;
    return &RDEQUE_CHUNK_PTR(deq, chunk)[idx];
}
static inline VALUE
rb_deq_ref(VALUE deq, long idx)
{
    VALUE *ptr = rb_deq_ref_ptr(deq, idx);
    if (ptr) return *ptr;
    return Qnil;
}

static VALUE
rb_deq_at(VALUE deq, VALUE offset)
{
    return rb_deq_ref(deq, NUM2LONG(offset));
}

static VALUE
rb_deq_at_write(VALUE deq, VALUE offset, VALUE value)
{
    VALUE *ptr = rb_deq_ref_ptr(deq, NUM2LONG(offset));
    if (ptr) {
        *ptr = value;
    } else {
        rb_raise(rb_eArgError, "index ouf of range");
    }
    return value;
}

static VALUE
inspect_deq(VALUE deq, VALUE dummy, int recur)
{
    unsigned long i;
    VALUE s, str;

    if (recur) return rb_usascii_str_new2(">[...]<");
    str = rb_str_buf_new2(">[");
    for (i = 0; i < RDEQUE_LEN(deq); i++) {
        s = rb_inspect(rb_deq_ref(deq, i));
        if (i > 0) rb_str_buf_cat2(str, ", ");
        else rb_enc_copy(str, s);
        rb_str_buf_append(str, s);
    }
    rb_str_buf_cat2(str, "]<");
    return str;
}

static VALUE
rb_deq_inspect(VALUE deq)
{
    if (RDEQUE_LEN(deq) == 0) rb_usascii_str_new2(">[]<");
    return rb_exec_recursive(inspect_deq, deq, 0);
}

static VALUE
rb_deq_length(VALUE deq)
{
    return LONG2NUM(RDEQUE_LEN(deq));
}

static VALUE
rb_deq_push_back(VALUE deq, VALUE item)
{
    rb_deq_modify_check(deq);
    struct RDequeChunkTableHeader *header = RDEQUE_TABLE_HEADER_PTR(deq);
    VALUE **table;
    if (header->last_chunk_size == RDEQUE_CHUNK_SIZE) {
        VALUE *new_chunk = deq_heap_alloc_chunk();
        if (RDEQUE_TABLE_CAP(deq) == header->last_chunk_idx + 1) {
            long new_capa = deq_calc_new_table_capa(deq);
            deq_heap_realloc_table(deq, new_capa);
            header = RDEQUE_TABLE_HEADER_PTR(deq);
        }
        assert(RDEQUE_TABLE_CAP(deq) > header->last_chunk_idx + 1);
        table = RDEQUE_TABLE_PTR(deq);
        table[header->last_chunk_idx + 1] = new_chunk;
        header->last_chunk_idx += 1;
        header->last_chunk_size = 0;
    }
    assert(header->last_chunk_size + 1 < RDEQUE_CHUNK_SIZE);
    table = RDEQUE_TABLE_PTR(deq);
    long chunk = header->last_chunk_idx;
    long idx = header->last_chunk_size;
    table[chunk][idx] = item;
    header->last_chunk_size += 1;
    DEQ_LEN(deq) += 1;
    return deq;
}
static VALUE
rb_deq_push_front(VALUE deq, VALUE item)
{
    rb_deq_modify_check(deq);
    struct RDequeChunkTableHeader *header = RDEQUE_TABLE_HEADER_PTR(deq);
    VALUE **table;
    if (header->first_chunk_size == RDEQUE_CHUNK_SIZE) {
        VALUE *new_chunk = deq_heap_alloc_chunk();
        if (header->first_chunk_idx == 0) {
            long new_capa = deq_calc_new_table_capa(deq);
            deq_heap_realloc_table(deq, new_capa);
            header = RDEQUE_TABLE_HEADER_PTR(deq);
        }
        assert(header->first_chunk_idx >= 1);
        table = RDEQUE_TABLE_PTR(deq);
        table[header->first_chunk_idx - 1] = new_chunk;
        header->first_chunk_idx -= 1;
        header->first_chunk_size = 0;
    }
    assert(header->first_chunk_size + 1 < RDEQUE_CHUNK_SIZE);
    table = RDEQUE_TABLE_PTR(deq);
    long chunk = header->first_chunk_idx;
    long idx = RDEQUE_CHUNK_SIZE - header->first_chunk_size - 1;
    table[chunk][idx] = item;
    header->first_chunk_size += 1;
    DEQ_LEN(deq) += 1;
    return deq;
}

void
Init_Deque(void)
{
    VALUE rb_cDeque = rb_define_class("Deque", rb_cObject);
    rb_define_alloc_func(rb_cDeque, empty_deq_alloc);
    rb_define_method(rb_cDeque, "initialize", rb_deq_initialize, -1);
    rb_define_method(rb_cDeque, "inspect", rb_deq_inspect, 0);
    rb_define_alias(rb_cDeque,  "to_s", "inspect");
    rb_define_method(rb_cDeque, "length", rb_deq_length, 0);
    rb_define_alias(rb_cDeque,  "size", "length");
    rb_define_method(rb_cDeque, "push_back", rb_deq_push_back, 1);
    rb_define_method(rb_cDeque, "push_front", rb_deq_push_front, 1);
    rb_define_alias(rb_cDeque,  "push", "push_back");
    rb_define_alias(rb_cDeque,  "unshift", "push_front");
    rb_define_method(rb_cDeque, "at", rb_deq_at, 1);
    rb_define_method(rb_cDeque, "[]=", rb_deq_at_write, 2);
    rb_define_alias(rb_cDeque,  "[]", "at");
}